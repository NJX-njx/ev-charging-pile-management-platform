// admin 模块 p7 改版验证 harness（offscreen）：链接真实 MainWindow/各页面/SocketClient，
// 配合 tools/mock_server_v23.py 状态化假服务端，脚本化驱动 UI 并断言行为。
//
// 退出码：0=PASS，2=FAIL，3=harness 自身错误。
// 场景（--scenario）：
//   nav         导航 6 项在最小尺寸 1280×800 下完整可见、默认 1440×900、基准字体 11pt
//   merged      站点与电桩合并页：选中站点联动电桩列表、按电桩状态的按钮使能
//   stationops  站点全部操作：搜索/新增/修改/导入/删除/显示已删除/已删除站点联动
//   pileops     电桩全部操作：新增/修改/禁用/重启/删除/占用详情/显示已删除
//   filtersort  Excel 式筛选排序：升降/不排序、筛选/清除、叠加、跨页面（站点/用户/订单/管理员）

#include <QApplication>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

#include "net/socketclient.h"
#include "ui/mainwindow.h"
#include "ui/stationpilepage.h"

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            qWarning("CHECK FAILED %s:%d: %s", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

static void log(const QString &s)
{
    qInfo().noquote() << QStringLiteral("  %1").arg(s);
}

static bool waitFor(const std::function<bool()> &cond, int timeoutMs = 10000)
{
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(timeoutMs)) {
        if (cond())
            return true;
        QTest::qWait(25);
    }
    return cond();
}

// ---------- 模态弹窗自动应答 ----------

// QDialog 处理器队列（FIFO）：触发会开对话框的操作前先入队一个处理器
static QList<std::function<void(QDialog *)>> g_dialogHandlers;

static void pollModalWidgets()
{
    QWidget *w = QApplication::activeModalWidget();
    if (!w || w->property("p7handled").toBool())
        return;
    if (qobject_cast<QProgressDialog *>(w))
        return; // 导入进度条非应答对象
    if (w->objectName() == QStringLiteral("filterPopup"))
        return; // 筛选弹窗由场景脚本显式操作
    if (QMessageBox *mb = qobject_cast<QMessageBox *>(w)) {
        w->setProperty("p7handled", true);
        if (QAbstractButton *yes = mb->button(QMessageBox::Yes))
            yes->click(); // 确认类问题一律「是」
        else if (QAbstractButton *ok = mb->button(QMessageBox::Ok))
            ok->click();
        return;
    }
    if (QDialog *d = qobject_cast<QDialog *>(w)) {
        w->setProperty("p7handled", true);
        if (!g_dialogHandlers.isEmpty()) {
            auto handler = g_dialogHandlers.takeFirst();
            handler(d);
        } else {
            qWarning().noquote() << QStringLiteral("未预期的模态对话框，拒绝：%1").arg(d->windowTitle());
            d->reject();
        }
    }
}

// ---------- 通用辅助 ----------

struct Resp {
    bool done = false;
    int code = -1;
    QString msg;
    QJsonObject data;
};

static std::shared_ptr<Resp> request(SocketClient *client, const QString &type,
                                     const QJsonObject &payload)
{
    auto r = std::make_shared<Resp>();
    client->sendRequest(type, payload, [r](int code, const QString &msg, const QJsonObject &data) {
        r->done = true;
        r->code = code;
        r->msg = msg;
        r->data = data;
    });
    waitFor([r] { return r->done; });
    return r;
}

static bool connectAndLogin(SocketClient &client, const QString &host, quint16 port)
{
    client.connectToServer(host, port);
    if (!waitFor([&client] { return client.isConnected(); }))
        return false;
    auto r = request(&client, QStringLiteral("admin_login"),
                     QJsonObject{{QStringLiteral("username"), QStringLiteral("admin")},
                                 {QStringLiteral("password"), QStringLiteral("123456")}});
    return r->code == 0;
}

static QPushButton *findButton(QWidget *root, const QString &text)
{
    const auto btns = root->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (b->text() == text)
            return b;
    return nullptr;
}

// 对话框里的普通 QLineEdit（排除 QSpinBox/QDoubleSpinBox 内部的行编辑子控件）
static QList<QLineEdit *> plainEdits(QWidget *root)
{
    QList<QLineEdit *> out;
    const auto edits = root->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        if (!qobject_cast<QAbstractSpinBox *>(e->parentWidget()))
            out << e;
    return out;
}

static bool clickButton(QWidget *root, const QString &text)
{
    QPushButton *btn = findButton(root, text);
    if (!btn || !btn->isEnabled())
        return false;
    QTest::mouseClick(btn, Qt::LeftButton, Qt::NoModifier, btn->rect().center());
    return true;
}

static bool clickRowButton(QTableWidget *table, int row, int col, const QString &text)
{
    QWidget *cell = table->cellWidget(row, col);
    if (!cell)
        return false;
    QPushButton *btn = findButton(cell, text);
    if (!btn)
        return false;
    QTest::mouseClick(btn, Qt::LeftButton, Qt::NoModifier, btn->rect().center());
    return true;
}

static int rowOfText(QTableWidget *table, int col, const QString &text)
{
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->item(r, col) && table->item(r, col)->text() == text)
            return r;
    return -1;
}

static QStringList columnTexts(QTableWidget *table, int col, bool visibleOnly)
{
    QStringList out;
    for (int r = 0; r < table->rowCount(); ++r) {
        if (visibleOnly && table->isRowHidden(r))
            continue;
        if (table->item(r, col))
            out << table->item(r, col)->text();
    }
    return out;
}

static int visibleRows(QTableWidget *table)
{
    int n = 0;
    for (int r = 0; r < table->rowCount(); ++r)
        if (!table->isRowHidden(r))
            ++n;
    return n;
}

// 表头列名点击（避开右侧排序箭头/漏斗图标与节区分界线拖拽热区）
static void headerSortClick(QTableWidget *table, int col)
{
    QHeaderView *h = table->horizontalHeader();
    const int x = h->sectionViewportPosition(col) + 8;
    QTest::mouseClick(h->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, h->height() / 2));
    QTest::qWait(60);
}

// 表头右侧漏斗图标点击
static void headerFunnelClick(QTableWidget *table, int col)
{
    QHeaderView *h = table->horizontalHeader();
    const int x = h->sectionViewportPosition(col) + h->sectionSize(col) - 11;
    QTest::mouseClick(h->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, h->height() / 2));
}

static QWidget *waitPopup(int timeoutMs = 5000)
{
    QWidget *popup = nullptr;
    waitFor([&popup] {
        popup = QApplication::activePopupWidget();
        return popup != nullptr;
    }, timeoutMs);
    return popup;
}

// 在筛选弹窗中取消勾选指定值后点「确定」
static bool popupDenyValueAndOk(const QString &value)
{
    QWidget *popup = waitPopup();
    if (!popup)
        return false;
    QListWidget *list = popup->findChild<QListWidget *>(QStringLiteral("filterValueList"));
    QPushButton *okBtn = popup->findChild<QPushButton *>(QStringLiteral("filterOk"));
    if (!list || !okBtn)
        return false;
    bool found = false;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (item->data(Qt::UserRole).toString() == value) {
            item->setCheckState(Qt::Unchecked);
            found = true;
        }
    }
    if (!found)
        return false;
    QTest::mouseClick(okBtn, Qt::LeftButton, Qt::NoModifier, okBtn->rect().center());
    return waitFor([] { return QApplication::activePopupWidget() == nullptr; });
}

// 在筛选弹窗中点「清除筛选」
static bool popupClearFilter()
{
    QWidget *popup = waitPopup();
    if (!popup)
        return false;
    QPushButton *clearBtn = popup->findChild<QPushButton *>(QStringLiteral("filterClear"));
    if (!clearBtn)
        return false;
    QTest::mouseClick(clearBtn, Qt::LeftButton, Qt::NoModifier, clearBtn->rect().center());
    return waitFor([] { return QApplication::activePopupWidget() == nullptr; });
}

// ---------------- 场景：导航与窗口尺寸 ----------------

static void scenarioNav()
{
    log(QStringLiteral("构造主窗口（不连接服务器）"));
    SocketClient client;
    MainWindow win(&client, QStringLiteral("admin"), QStringLiteral("123456"));
    win.show();
    CHECK(waitFor([&win] { return win.isVisible(); }));

    CHECK(QApplication::font().pointSize() == 11);
    CHECK(win.minimumSize() == QSize(1280, 800));
    CHECK(win.width() == 1440 && win.height() == 900);

    QListWidget *nav = win.findChild<QListWidget *>(QStringLiteral("listWidgetNav"));
    QStackedWidget *stack = win.findChild<QStackedWidget *>(QStringLiteral("stackedWidget"));
    CHECK(nav != nullptr && stack != nullptr);
    if (!nav || !stack)
        return;
    CHECK(nav->count() == 6);
    CHECK(stack->count() == 6);
    CHECK(nav->item(2)->text() == QStringLiteral("站点与电桩"));

    // 最小窗口尺寸下全部导航项完整可见
    win.resize(1280, 800);
    QTest::qWait(200);
    CHECK(nav->width() == 200);
    for (int i = 0; i < nav->count(); ++i) {
        const QRect r = nav->visualItemRect(nav->item(i));
        CHECK(r.isValid() && r.height() >= 28 && nav->viewport()->contentsRect().contains(r));
    }
    log(QStringLiteral("导航 %1 项在 1280×800 下全部可见").arg(nav->count()));
}

// ---------------- 场景：合并页左右联动 ----------------

static void scenarioMerged(const QString &host, quint16 port)
{
    SocketClient client;
    CHECK(connectAndLogin(client, host, port));
    MainWindow win(&client, QStringLiteral("admin"), QStringLiteral("123456"));
    win.show();
    CHECK(waitFor([&win] { return win.isVisible(); }));
    QListWidget *nav = win.findChild<QListWidget *>(QStringLiteral("listWidgetNav"));
    CHECK(nav != nullptr);
    nav->setCurrentRow(2);

    QTableWidget *stationTable = win.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QTableWidget *pileTable = win.findChild<QTableWidget *>(QStringLiteral("pileTable"));
    QLabel *stationLabel = win.findChild<QLabel *>(QStringLiteral("labelCurrentStation"));
    QPushButton *disableBtn = win.findChild<QPushButton *>(QStringLiteral("btnDisablePile"));
    QPushButton *activeOrderBtn = win.findChild<QPushButton *>(QStringLiteral("btnActiveOrder"));
    QPushButton *addPileBtn = findButton(&win, QStringLiteral("新增电桩"));
    QPushButton *editPileBtn = findButton(&win, QStringLiteral("修改电桩"));
    QPushButton *deletePileBtn = findButton(&win, QStringLiteral("删除电桩"));
    QPushButton *restartBtn = findButton(&win, QStringLiteral("远程重启"));
    CHECK(stationTable && pileTable && stationLabel && disableBtn && activeOrderBtn
          && addPileBtn && editPileBtn && deletePileBtn && restartBtn);
    if (!stationTable || !pileTable)
        return;

    // 初始：2 个站点，自动选中第一站 → 右侧显示其 3 个电桩
    CHECK(waitFor([&] { return stationTable->rowCount() == 2; }));
    CHECK(waitFor([&] { return pileTable->rowCount() == 3; }));
    CHECK(columnTexts(pileTable, 0, false)
          == (QStringList{QStringLiteral("P-0101"), QStringLiteral("P-0102"), QStringLiteral("P-0103")}));
    CHECK(stationLabel->text().contains(QStringLiteral("良乡大学城北站")));
    log(QStringLiteral("初始联动：站点1 → 3 个电桩"));

    // 切换站点 → 电桩联动刷新
    stationTable->selectRow(1);
    CHECK(waitFor([&] { return pileTable->rowCount() == 2; }));
    CHECK(columnTexts(pileTable, 0, false)
          == (QStringList{QStringLiteral("P-0201"), QStringLiteral("P-0202")}));
    CHECK(stationLabel->text().contains(QStringLiteral("长阳地铁站充电站")));
    log(QStringLiteral("切换站点2 → 2 个电桩"));

    stationTable->selectRow(0);
    CHECK(waitFor([&] { return pileTable->rowCount() == 3; }));

    // 按电桩状态校验操作按钮使能（v2.3：重启 idle+fault、禁用仅 idle、占用详情仅 in_use）
    auto selectPile = [&](const QString &code) {
        const int row = rowOfText(pileTable, 0, code);
        CHECK(row >= 0);
        if (row >= 0)
            pileTable->selectRow(row);
        QTest::qWait(60);
    };
    selectPile(QStringLiteral("P-0101")); // idle
    CHECK(disableBtn->isEnabled() && restartBtn->isEnabled() && editPileBtn->isEnabled()
          && deletePileBtn->isEnabled() && !activeOrderBtn->isEnabled());
    selectPile(QStringLiteral("P-0102")); // in_use
    CHECK(activeOrderBtn->isEnabled() && !disableBtn->isEnabled() && !restartBtn->isEnabled());
    selectPile(QStringLiteral("P-0103")); // fault
    CHECK(restartBtn->isEnabled() && !disableBtn->isEnabled() && !activeOrderBtn->isEnabled());
    CHECK(addPileBtn->isEnabled());
    log(QStringLiteral("idle/in_use/fault 三态按钮使能正确"));
}

// ---------------- 场景：站点全部操作 ----------------

static void scenarioStationOps(const QString &host, quint16 port)
{
    SocketClient client;
    CHECK(connectAndLogin(client, host, port));
    MainWindow win(&client, QStringLiteral("admin"), QStringLiteral("123456"));
    win.show();
    CHECK(waitFor([&win] { return win.isVisible(); }));
    QListWidget *nav = win.findChild<QListWidget *>(QStringLiteral("listWidgetNav"));
    nav->setCurrentRow(2);

    StationPilePage *page = win.findChild<StationPilePage *>();
    QTableWidget *stationTable = win.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QTableWidget *pileTable = win.findChild<QTableWidget *>(QStringLiteral("pileTable"));
    QLabel *stationLabel = win.findChild<QLabel *>(QStringLiteral("labelCurrentStation"));
    QPushButton *addPileBtn = findButton(&win, QStringLiteral("新增电桩"));
    CHECK(page && stationTable && pileTable && stationLabel && addPileBtn);
    if (!page || !stationTable)
        return;
    CHECK(waitFor([&] { return stationTable->rowCount() == 2; }));

    // 站名搜索
    QLineEdit *searchEdit = page->findChild<QLineEdit *>();
    CHECK(searchEdit != nullptr);
    searchEdit->setText(QStringLiteral("长阳"));
    CHECK(clickButton(page, QStringLiteral("查询")));
    CHECK(waitFor([&] { return stationTable->rowCount() == 1; }));
    CHECK(stationTable->item(0, 1)->text().contains(QStringLiteral("长阳")));
    searchEdit->clear();
    CHECK(clickButton(page, QStringLiteral("查询")));
    CHECK(waitFor([&] { return stationTable->rowCount() == 2; }));
    log(QStringLiteral("搜索/清空搜索"));

    // 新增站点（对话框填写四个输入框）
    g_dialogHandlers << [](QDialog *d) {
        const auto edits = plainEdits(d);
        CHECK(edits.size() == 4); // 站名/地址/经度/纬度
        edits[0]->setText(QStringLiteral("自动化测试站"));
        edits[1]->setText(QStringLiteral("测试地址 1 号"));
        edits[2]->setText(QStringLiteral("116.300000"));
        edits[3]->setText(QStringLiteral("39.950000"));
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    CHECK(clickButton(page, QStringLiteral("新增站点")));
    CHECK(waitFor([&] { return stationTable->rowCount() == 3; }));
    CHECK(rowOfText(stationTable, 1, QStringLiteral("自动化测试站")) >= 0);
    log(QStringLiteral("新增站点"));

    // 修改站点（行内按钮 + 对话框改名）
    int row = rowOfText(stationTable, 1, QStringLiteral("良乡大学城北站"));
    CHECK(row >= 0);
    g_dialogHandlers << [](QDialog *d) {
        const auto edits = plainEdits(d);
        // 创建顺序：ID(禁)/站名/地址/经度(禁)/纬度(禁)
        CHECK(edits.size() == 5);
        edits[1]->setText(QStringLiteral("良乡大学城北站-改"));
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    CHECK(clickRowButton(stationTable, row, 7, QStringLiteral("修改")));
    CHECK(waitFor([&] { return rowOfText(stationTable, 1, QStringLiteral("良乡大学城北站-改")) >= 0; }));
    log(QStringLiteral("修改站点"));

    // 从 JSON 文件导入站点（进度条自动进行，完成弹窗自动确认）
    QFile f(QStringLiteral("/tmp/p7_import_stations.json"));
    CHECK(f.open(QIODevice::WriteOnly));
    f.write("[{\"name\":\"导入测试站\",\"address\":\"导入地址\",\"lng\":116.4,\"lat\":39.9,"
            "\"pricePerKwh\":1.35,\"pileCount\":2}]");
    f.close();
    QMetaObject::invokeMethod(page, "importStationsFromFile",
                              Q_ARG(QString, QStringLiteral("/tmp/p7_import_stations.json")));
    CHECK(waitFor([&] { return stationTable->rowCount() == 4; }, 20000));
    CHECK(rowOfText(stationTable, 1, QStringLiteral("导入测试站")) >= 0);
    log(QStringLiteral("导入站点"));

    // 删除导入的站点（行内删除 + 确认弹窗自动「是」）
    row = rowOfText(stationTable, 1, QStringLiteral("导入测试站"));
    CHECK(row >= 0);
    CHECK(clickRowButton(stationTable, row, 7, QStringLiteral("删除")));
    CHECK(waitFor([&] {
        return rowOfText(stationTable, 1, QStringLiteral("导入测试站")) < 0
               && stationTable->rowCount() == 3;
    }));
    log(QStringLiteral("删除站点"));

    // 显示已删除：出现「已删除」状态行
    QCheckBox *showDeleted = win.findChild<QCheckBox *>(QStringLiteral("checkShowDeletedStations"));
    CHECK(showDeleted != nullptr);
    showDeleted->setChecked(true);
    CHECK(waitFor([&] { return stationTable->rowCount() == 4; }));
    int delRow = -1;
    for (int r = 0; r < stationTable->rowCount(); ++r)
        if (stationTable->item(r, 6) && stationTable->item(r, 6)->text() == QStringLiteral("已删除"))
            delRow = r;
    CHECK(delRow >= 0);

    // 选中已删除站点：右侧电桩清空、提示仅历史查看、新增电桩禁用
    stationTable->selectRow(delRow);
    QTest::qWait(150);
    CHECK(pileTable->rowCount() == 0);
    CHECK(stationLabel->text().contains(QStringLiteral("已删除")));
    CHECK(!addPileBtn->isEnabled());
    log(QStringLiteral("显示已删除 + 已删除站点联动"));

    showDeleted->setChecked(false);
    CHECK(waitFor([&] { return stationTable->rowCount() == 3; }));
}

// ---------------- 场景：电桩全部操作 ----------------

static void scenarioPileOps(const QString &host, quint16 port)
{
    SocketClient client;
    CHECK(connectAndLogin(client, host, port));
    MainWindow win(&client, QStringLiteral("admin"), QStringLiteral("123456"));
    win.show();
    CHECK(waitFor([&win] { return win.isVisible(); }));
    QListWidget *nav = win.findChild<QListWidget *>(QStringLiteral("listWidgetNav"));
    nav->setCurrentRow(2);

    QTableWidget *pileTable = win.findChild<QTableWidget *>(QStringLiteral("pileTable"));
    QPushButton *addPileBtn = findButton(&win, QStringLiteral("新增电桩"));
    QPushButton *editPileBtn = findButton(&win, QStringLiteral("修改电桩"));
    QPushButton *deletePileBtn = findButton(&win, QStringLiteral("删除电桩"));
    QPushButton *restartBtn = findButton(&win, QStringLiteral("远程重启"));
    QPushButton *disableBtn = win.findChild<QPushButton *>(QStringLiteral("btnDisablePile"));
    QPushButton *activeOrderBtn = win.findChild<QPushButton *>(QStringLiteral("btnActiveOrder"));
    CHECK(pileTable && addPileBtn && editPileBtn && deletePileBtn && restartBtn && disableBtn
          && activeOrderBtn);
    if (!pileTable)
        return;
    CHECK(waitFor([&] { return pileTable->rowCount() == 3; }));

    auto selectPile = [&](const QString &code) {
        const int row = rowOfText(pileTable, 0, code);
        CHECK(row >= 0);
        if (row >= 0)
            pileTable->selectRow(row);
        QTest::qWait(60);
    };
    auto statusOf = [&](const QString &code) {
        const int row = rowOfText(pileTable, 0, code);
        return row >= 0 ? pileTable->item(row, 3)->text() : QString();
    };

    // 新增电桩（对话框只填编号，所属站点默认当前选中站点）
    g_dialogHandlers << [](QDialog *d) {
        const auto edits = plainEdits(d);
        CHECK(edits.size() == 1); // 电桩编号
        edits[0]->setText(QStringLiteral("P-9001"));
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    CHECK(clickButton(&win, QStringLiteral("新增电桩")));
    CHECK(waitFor([&] { return pileTable->rowCount() == 4; }));
    CHECK(rowOfText(pileTable, 0, QStringLiteral("P-9001")) >= 0);
    log(QStringLiteral("新增电桩"));

    // 修改电桩（功率改为 30 后保存）
    selectPile(QStringLiteral("P-9001"));
    g_dialogHandlers << [](QDialog *d) {
        const auto boxes = d->findChildren<QDoubleSpinBox *>();
        CHECK(boxes.size() == 1);
        boxes[0]->setValue(30.0);
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    QTest::mouseClick(editPileBtn, Qt::LeftButton, Qt::NoModifier, editPileBtn->rect().center());
    CHECK(waitFor([&] {
        const int r = rowOfText(pileTable, 0, QStringLiteral("P-9001"));
        return r >= 0 && pileTable->item(r, 2)->text() == QStringLiteral("30");
    }));
    log(QStringLiteral("修改电桩"));

    // 禁用（idle → fault）
    selectPile(QStringLiteral("P-9001"));
    CHECK(disableBtn->isEnabled());
    QTest::mouseClick(disableBtn, Qt::LeftButton, Qt::NoModifier, disableBtn->rect().center());
    CHECK(waitFor([&] { return statusOf(QStringLiteral("P-9001")) == QStringLiteral("故障"); }));
    log(QStringLiteral("禁用电桩"));

    // 远程重启（fault → idle）
    selectPile(QStringLiteral("P-9001"));
    CHECK(restartBtn->isEnabled());
    QTest::mouseClick(restartBtn, Qt::LeftButton, Qt::NoModifier, restartBtn->rect().center());
    CHECK(waitFor([&] { return statusOf(QStringLiteral("P-9001")) == QStringLiteral("空闲"); }));
    log(QStringLiteral("远程重启"));

    // 占用详情（in_use 行弹出占用订单对话框）
    selectPile(QStringLiteral("P-0102"));
    CHECK(activeOrderBtn->isEnabled());
    bool sawOrderDialog = false;
    bool sawOrderId = false;
    g_dialogHandlers << [&sawOrderDialog, &sawOrderId](QDialog *d) {
        sawOrderDialog = d->windowTitle().contains(QStringLiteral("P-0102"));
        const auto labels = d->findChildren<QLabel *>();
        for (QLabel *l : labels)
            if (l->text() == QStringLiteral("10001"))
                sawOrderId = true;
        d->accept();
    };
    QTest::mouseClick(activeOrderBtn, Qt::LeftButton, Qt::NoModifier, activeOrderBtn->rect().center());
    CHECK(waitFor([&] { return sawOrderDialog; }));
    CHECK(sawOrderId);
    log(QStringLiteral("占用详情"));

    // 删除电桩（仅 idle 可删）
    selectPile(QStringLiteral("P-9001"));
    CHECK(deletePileBtn->isEnabled());
    QTest::mouseClick(deletePileBtn, Qt::LeftButton, Qt::NoModifier, deletePileBtn->rect().center());
    CHECK(waitFor([&] { return pileTable->rowCount() == 3; }));
    log(QStringLiteral("删除电桩"));

    // 显示已删除：含「已删除」状态行，选中后操作按钮全禁
    QCheckBox *showDeletedPiles = win.findChild<QCheckBox *>(QStringLiteral("checkShowDeletedPiles"));
    CHECK(showDeletedPiles != nullptr);
    showDeletedPiles->setChecked(true);
    CHECK(waitFor([&] { return pileTable->rowCount() == 4; }));
    int delRow = -1;
    for (int r = 0; r < pileTable->rowCount(); ++r)
        if (pileTable->item(r, 3) && pileTable->item(r, 3)->text() == QStringLiteral("已删除"))
            delRow = r;
    CHECK(delRow >= 0);
    pileTable->selectRow(delRow);
    QTest::qWait(100);
    CHECK(!editPileBtn->isEnabled() && !deletePileBtn->isEnabled() && !restartBtn->isEnabled()
          && !disableBtn->isEnabled() && !activeOrderBtn->isEnabled());
    log(QStringLiteral("显示已删除电桩"));

    showDeletedPiles->setChecked(false);
    CHECK(waitFor([&] { return pileTable->rowCount() == 3; }));
}

// ---------------- 场景：Excel 式筛选与排序 ----------------

static void scenarioFilterSort(const QString &host, quint16 port)
{
    SocketClient client;
    CHECK(connectAndLogin(client, host, port));
    MainWindow win(&client, QStringLiteral("admin"), QStringLiteral("123456"));
    win.show();
    CHECK(waitFor([&win] { return win.isVisible(); }));
    QListWidget *nav = win.findChild<QListWidget *>(QStringLiteral("listWidgetNav"));
    nav->setCurrentRow(2);

    QTableWidget *stationTable = win.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QTableWidget *pileTable = win.findChild<QTableWidget *>(QStringLiteral("pileTable"));
    CHECK(stationTable && pileTable);
    if (!stationTable || !pileTable)
        return;
    CHECK(waitFor([&] { return pileTable->rowCount() == 3; }));

    // ---- 电桩表排序：功率 升序→降序→不排序（不排序还原装入顺序） ----
    headerSortClick(pileTable, 2);
    CHECK(columnTexts(pileTable, 0, false)
          == (QStringList{QStringLiteral("P-0102"), QStringLiteral("P-0101"), QStringLiteral("P-0103")}));
    headerSortClick(pileTable, 2);
    CHECK(columnTexts(pileTable, 0, false)
          == (QStringList{QStringLiteral("P-0103"), QStringLiteral("P-0101"), QStringLiteral("P-0102")}));
    headerSortClick(pileTable, 2);
    CHECK(columnTexts(pileTable, 0, false)
          == (QStringList{QStringLiteral("P-0101"), QStringLiteral("P-0102"), QStringLiteral("P-0103")}));
    log(QStringLiteral("电桩表 功率 升/降/不排序"));

    // ---- 电桩表筛选：类型列去掉「慢充」→ P-0102 隐藏 ----
    headerFunnelClick(pileTable, 1);
    CHECK(popupDenyValueAndOk(QStringLiteral("慢充")));
    CHECK(visibleRows(pileTable) == 2);
    CHECK(columnTexts(pileTable, 0, true)
          == (QStringList{QStringLiteral("P-0101"), QStringLiteral("P-0103")}));
    log(QStringLiteral("电桩表 类型筛选（隐藏慢充）"));

    // ---- 筛选与排序叠加：保持筛选，功率降序 ----
    headerSortClick(pileTable, 2); // 升序
    headerSortClick(pileTable, 2); // 降序
    CHECK(columnTexts(pileTable, 0, true)
          == (QStringList{QStringLiteral("P-0103"), QStringLiteral("P-0101")}));
    headerSortClick(pileTable, 2); // 不排序
    log(QStringLiteral("筛选+排序叠加"));

    // ---- 清除筛选 → 全部恢复可见 ----
    headerFunnelClick(pileTable, 1);
    CHECK(popupClearFilter());
    CHECK(visibleRows(pileTable) == 3);
    log(QStringLiteral("清除筛选"));

    // ---- 站点表排序与 tooltip 作用范围说明 ----
    CHECK(stationTable->horizontalHeaderItem(1)->toolTip().contains(QStringLiteral("当前页")));
    headerSortClick(stationTable, 4); // 总桩数 升序：2 个桩的站点在前
    CHECK(columnTexts(stationTable, 0, true)
          == (QStringList{QStringLiteral("2"), QStringLiteral("1")}));
    headerSortClick(stationTable, 4); // 降序
    CHECK(columnTexts(stationTable, 0, true)
          == (QStringList{QStringLiteral("1"), QStringLiteral("2")}));
    headerSortClick(stationTable, 4); // 不排序还原
    log(QStringLiteral("站点表 总桩数排序（含操作列单元格控件整行搬运）"));

    // ---- 用户管理页 ----
    nav->setCurrentRow(3);
    QTableWidget *userTable = win.findChild<QTableWidget *>(QStringLiteral("userTable"));
    CHECK(userTable != nullptr);
    CHECK(waitFor([&] { return userTable->rowCount() == 2; }));
    headerSortClick(userTable, 1); // 手机号 升序
    CHECK(columnTexts(userTable, 1, false).first() == QStringLiteral("13800001234"));
    headerSortClick(userTable, 1); // 降序
    CHECK(columnTexts(userTable, 1, false).first() == QStringLiteral("13900005678"));
    headerSortClick(userTable, 1); // 不排序
    // 状态列筛选去掉「冻结」
    headerFunnelClick(userTable, 5);
    CHECK(popupDenyValueAndOk(QStringLiteral("冻结")));
    CHECK(visibleRows(userTable) == 1);
    // 显示已删除：筛选跨数据重载仍生效（冻结行保持隐藏，已删除行可见）
    QCheckBox *showDeletedUsers = win.findChild<QCheckBox *>(QStringLiteral("checkShowDeleted"));
    CHECK(showDeletedUsers != nullptr);
    showDeletedUsers->setChecked(true);
    CHECK(waitFor([&] { return userTable->rowCount() == 3; }));
    CHECK(visibleRows(userTable) == 2);
    headerFunnelClick(userTable, 5);
    CHECK(popupClearFilter());
    CHECK(visibleRows(userTable) == 3);
    showDeletedUsers->setChecked(false);
    log(QStringLiteral("用户页 排序/筛选/跨重载保持"));

    // ---- 订单管理页 ----
    nav->setCurrentRow(4);
    QTableWidget *orderTable = win.findChild<QTableWidget *>(QStringLiteral("orderTable"));
    CHECK(orderTable != nullptr);
    CHECK(waitFor([&] { return orderTable->rowCount() == 3; }));
    CHECK(orderTable->horizontalHeaderItem(0)->toolTip().contains(QStringLiteral("当前页")));
    headerSortClick(orderTable, 0); // 订单号 升序
    CHECK(columnTexts(orderTable, 0, false)
          == (QStringList{QStringLiteral("10001"), QStringLiteral("10002"), QStringLiteral("10003")}));
    headerSortClick(orderTable, 0); // 降序
    CHECK(columnTexts(orderTable, 0, false)
          == (QStringList{QStringLiteral("10003"), QStringLiteral("10002"), QStringLiteral("10001")}));
    headerSortClick(orderTable, 0); // 不排序
    // 状态列筛选去掉「已完成」
    headerFunnelClick(orderTable, 4);
    CHECK(popupDenyValueAndOk(QStringLiteral("已完成")));
    CHECK(visibleRows(orderTable) == 2);
    headerFunnelClick(orderTable, 4);
    CHECK(popupClearFilter());
    CHECK(visibleRows(orderTable) == 3);
    log(QStringLiteral("订单页 排序/筛选/清除"));

    // ---- 系统管理页：管理员列表 + 账号操作 ----
    nav->setCurrentRow(5);
    QTableWidget *adminTable = win.findChild<QTableWidget *>(QStringLiteral("tableAdmins"));
    CHECK(adminTable != nullptr);
    CHECK(waitFor([&] { return adminTable->rowCount() == 1; }));
    // 新增管理员（对话框填用户名/密码/确认密码）
    g_dialogHandlers << [](QDialog *d) {
        const auto edits = plainEdits(d);
        CHECK(edits.size() == 3);
        edits[0]->setText(QStringLiteral("testadmin"));
        edits[1]->setText(QStringLiteral("abc12345"));
        edits[2]->setText(QStringLiteral("abc12345"));
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    CHECK(clickButton(&win, QStringLiteral("新增管理员")));
    CHECK(waitFor([&] { return adminTable->rowCount() == 2; }));
    // 用户名降序 → testadmin 在前
    headerSortClick(adminTable, 1); // 升序
    CHECK(adminTable->item(0, 1)->text() == QStringLiteral("admin（本人）"));
    headerSortClick(adminTable, 1); // 降序
    CHECK(adminTable->item(0, 1)->text() == QStringLiteral("testadmin"));
    // 删除 testadmin（确认弹窗自动「是」）
    const int row = rowOfText(adminTable, 1, QStringLiteral("testadmin"));
    CHECK(row >= 0);
    adminTable->selectRow(row);
    QTest::qWait(60);
    CHECK(clickButton(&win, QStringLiteral("删除管理员")));
    CHECK(waitFor([&] { return adminTable->rowCount() == 1; }));
    log(QStringLiteral("系统页 新增/排序/删除管理员"));
}

// ---------------- main ----------------

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NeusoftEVCP"));
    QApplication::setApplicationName(QStringLiteral("evcp-admin-p7-harness"));
    // 与 admin/main.cpp 一致的基准字号
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    QCommandLineParser parser;
    parser.addOptions({
        {QStringLiteral("scenario"), QStringLiteral("场景名"), QStringLiteral("name")},
        {QStringLiteral("host"), QStringLiteral("mock 地址"), QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), QStringLiteral("mock 端口"), QStringLiteral("port"), QStringLiteral("18893")},
    });
    parser.process(app);
    const QString scenario = parser.value(QStringLiteral("scenario"));
    const QString host = parser.value(QStringLiteral("host"));
    const quint16 port = static_cast<quint16>(parser.value(QStringLiteral("port")).toUShort());

    QTimer modalTimer;
    QObject::connect(&modalTimer, &QTimer::timeout, &pollModalWidgets);
    modalTimer.start(40);

    qInfo().noquote() << QStringLiteral("scenario: %1").arg(scenario);
    if (scenario == QStringLiteral("nav"))
        scenarioNav();
    else if (scenario == QStringLiteral("merged"))
        scenarioMerged(host, port);
    else if (scenario == QStringLiteral("stationops"))
        scenarioStationOps(host, port);
    else if (scenario == QStringLiteral("pileops"))
        scenarioPileOps(host, port);
    else if (scenario == QStringLiteral("filtersort"))
        scenarioFilterSort(host, port);
    else {
        qWarning().noquote() << QStringLiteral("未知场景：%1").arg(scenario);
        return 3;
    }

    if (g_failures == 0) {
        qInfo() << "RESULT: PASS";
        return 0;
    }
    qWarning() << "RESULT: FAIL failures=" << g_failures;
    return 2;
}
