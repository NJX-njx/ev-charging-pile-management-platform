// admin 模块 v2.4 协议验证 harness（offscreen）：链接真实 MainWindow/各页面/SocketClient。
// v2.5 起 station_add 改为显式 piles 清单，真实服务端（v2.4，127.0.0.1:8888）不再兼容，
// 故改为直连 ../mock_server_v25.py 假服务端（v2.4 语义 + v2.5 station_add，
// run_scenarios.sh 每场景独立实例）；每场景用独立的第二连接以用户身份
// 自建站点/用户/订单数据，再驱动管理端 UI 做结构化断言并截图。
//
// 退出码：0=PASS，2=FAIL，3=harness 自身错误。
// 场景（--scenario）：
//   pileocc   站点与电桩页：in_use 按 occupancy 区分「预约中/充电中」（橙/蓝语义色）、
//             待结算不占桩；占用桩可重启/禁用（确认文案说明占用订单、结果带关联订单信息）、
//             故障桩禁用置灰、重启后恢复空闲
//   useredit  用户管理页：编辑对话框经 user_detail 显示当前头像与余额，改昵称/余额/换头像/
//             清头像经 user_update 生效并就地刷新列表行；超 512KiB 图片被拒绝
//   orderops  订单管理页：reserved 可「取消预约」、charging 可「停止充电」并生效；
//             completed/pending_payment/cancelled 两按钮皆禁用
// 截图输出到 --shotdir（默认 /tmp），文件名 v24_<scenario>_<name>.png。
// 用法见同目录 run_scenarios.sh。

#include <QApplication>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

#include "net/socketclient.h"
#include "ui/mainwindow.h"
#include "ui/orderpage.h"
#include "ui/stationpilepage.h"
#include "ui/userpage.h"

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

static bool waitFor(const std::function<bool()> &cond, int timeoutMs = 15000)
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

// ---------- 模态弹窗自动应答（记录全部 QMessageBox 文本供断言） ----------

static QStringList g_messages;
static QList<std::function<void(QDialog *)>> g_dialogHandlers;

static void pollModalWidgets()
{
    QWidget *w = QApplication::activeModalWidget();
    if (!w || w->property("v24handled").toBool())
        return;
    if (QMessageBox *mb = qobject_cast<QMessageBox *>(w)) {
        w->setProperty("v24handled", true);
        g_messages << (mb->windowTitle() + QStringLiteral(" | ") + mb->text());
        qInfo().noquote() << QStringLiteral("  [msgbox] %1 : %2").arg(mb->windowTitle(), mb->text());
        if (QAbstractButton *yes = mb->button(QMessageBox::Yes))
            yes->click(); // 确认类问题一律「是」
        else if (QAbstractButton *ok = mb->button(QMessageBox::Ok))
            ok->click();
        return;
    }
    if (QDialog *d = qobject_cast<QDialog *>(w)) {
        w->setProperty("v24handled", true);
        if (!g_dialogHandlers.isEmpty()) {
            auto handler = g_dialogHandlers.takeFirst();
            handler(d);
        } else {
            qWarning().noquote() << QStringLiteral("未预期的模态对话框，拒绝：%1").arg(d->windowTitle());
            d->reject();
        }
    }
}

static bool hasMessage(const QString &needle)
{
    for (const QString &m : g_messages)
        if (m.contains(needle))
            return true;
    return false;
}

// ---------- 协议与 UI 通用辅助 ----------

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

static bool clickButton(QWidget *root, const QString &text)
{
    QPushButton *btn = findButton(root, text);
    if (!btn || !btn->isEnabled())
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

static void saveShot(QWidget *w, const QString &shotdir, const QString &scenario, const QString &name)
{
    const QString path = QStringLiteral("%1/v24_%2_%3.png").arg(shotdir, scenario, name);
    if (w->grab().save(path))
        log(QStringLiteral("截图 %1").arg(path));
    else
        CHECK(!"截图保存失败");
}

// ---------- 数据构造（第二连接以用户身份操作；命名带时间戳避免与联调库既有数据冲突） ----------

struct SeedStation {
    int stationId = 0;
    QString name;
    QList<QPair<int, QString>> piles; // (pileId, code)，按服务端返回顺序（code 升序）
};

static bool seedStation(SocketClient *admin, int pileCount, SeedStation *out)
{
    out->name = QStringLiteral("v24自测站%1").arg(QDateTime::currentMSecsSinceEpoch() % 1000000000);
    // v2.5：station_add 改为显式 piles 电桩清单（协议 7.8，pileCount 废弃）
    const QString prefix = QStringLiteral("V%1").arg(QDateTime::currentMSecsSinceEpoch() % 1000000);
    QJsonArray pileSpecs;
    for (int i = 0; i < pileCount; ++i) {
        pileSpecs.append(QJsonObject{{QStringLiteral("code"),
                                  QStringLiteral("%1-%2").arg(prefix).arg(i, 2, 10, QLatin1Char('0'))},
                                 {QStringLiteral("type"), i % 2 == 0 ? QStringLiteral("fast")
                                                                     : QStringLiteral("slow")},
                                 {QStringLiteral("powerKw"), i % 2 == 0 ? 60.0 : 7.0}});
    }
    auto r = request(admin, QStringLiteral("station_add"),
                     QJsonObject{{QStringLiteral("name"), out->name},
                                 {QStringLiteral("address"), QStringLiteral("v24自测地址")},
                                 {QStringLiteral("lng"), 121.5},
                                 {QStringLiteral("lat"), 38.9},
                                 {QStringLiteral("pricePerKwh"), 1.20},
                                 {QStringLiteral("piles"), pileSpecs}});
    if (r->code != 0)
        return false;
    out->stationId = r->data[QStringLiteral("station")].toObject()[QStringLiteral("stationId")].toInt();
    auto rl = request(admin, QStringLiteral("pile_list"),
                      QJsonObject{{QStringLiteral("stationId"), out->stationId}});
    if (rl->code != 0)
        return false;
    const QJsonArray piles = rl->data[QStringLiteral("piles")].toArray();
    for (const auto &v : piles) {
        const QJsonObject p = v.toObject();
        out->piles << qMakePair(p[QStringLiteral("pileId")].toInt(), p[QStringLiteral("code")].toString());
    }
    return out->piles.size() == pileCount;
}

static bool seedUser(SocketClient *user, const QString &host, quint16 port, double balance,
                     QString *phoneOut, int *userIdOut)
{
    const QString phone = QStringLiteral("137%1")
                              .arg(QDateTime::currentMSecsSinceEpoch() % 100000000, 8, 10, QLatin1Char('0'));
    user->connectToServer(host, port);
    if (!waitFor([user] { return user->isConnected(); }))
        return false;
    auto r = request(user, QStringLiteral("user_login"),
                     QJsonObject{{QStringLiteral("phone"), phone},
                                 {QStringLiteral("password"), QStringLiteral("abc123")}});
    if (r->code != 0)
        return false;
    if (balance > 0) {
        auto rr = request(user, QStringLiteral("wallet_recharge"),
                          QJsonObject{{QStringLiteral("amount"), balance}});
        if (rr->code != 0)
            return false;
    }
    *phoneOut = phone;
    *userIdOut = r->data[QStringLiteral("user")].toObject()[QStringLiteral("userId")].toInt();
    return true;
}

static int reserveOrder(SocketClient *user, int pileId)
{
    auto r = request(user, QStringLiteral("charge_reserve"),
                     QJsonObject{{QStringLiteral("pileId"), pileId}});
    return r->code == 0 ? r->data[QStringLiteral("order")].toObject()[QStringLiteral("orderId")].toInt() : -1;
}

static bool startOrder(SocketClient *user, int orderId)
{
    return request(user, QStringLiteral("charge_start"),
                   QJsonObject{{QStringLiteral("orderId"), orderId}})->code == 0;
}

static bool stopOrder(SocketClient *user, int orderId)
{
    return request(user, QStringLiteral("charge_stop"),
                   QJsonObject{{QStringLiteral("orderId"), orderId}})->code == 0;
}

static bool settleOrder(SocketClient *user, int orderId)
{
    return request(user, QStringLiteral("charge_settle"),
                   QJsonObject{{QStringLiteral("orderId"), orderId}})->code == 0;
}

// 协议层复核：电桩状态与占用类型
static QJsonObject pileById(SocketClient *admin, int stationId, int pileId)
{
    auto r = request(admin, QStringLiteral("pile_list"),
                     QJsonObject{{QStringLiteral("stationId"), stationId}});
    const QJsonArray piles = r->data[QStringLiteral("piles")].toArray();
    for (const auto &v : piles) {
        const QJsonObject p = v.toObject();
        if (p[QStringLiteral("pileId")].toInt() == pileId)
            return p;
    }
    return QJsonObject();
}

static QString orderStatusById(SocketClient *admin, int orderId)
{
    auto r = request(admin, QStringLiteral("admin_order_detail"),
                     QJsonObject{{QStringLiteral("orderId"), orderId}});
    return r->data[QStringLiteral("order")].toObject()[QStringLiteral("status")].toString();
}

// 生成测试图片：64×64 合法 PNG 与 1024×1024 噪声 PNG（>512KiB，用于超限拒绝分支）
static bool makeTestImages(const QString &pngPath, const QString &bigPath)
{
    QImage img(64, 64, QImage::Format_RGB32);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            img.setPixelColor(x, y, QColor((x * 4) % 256, (y * 4) % 256, (x + y) * 2 % 256));
    if (!img.save(pngPath, "PNG"))
        return false;
    QImage big(1024, 1024, QImage::Format_RGB32);
    QRandomGenerator rng(12345);
    for (int y = 0; y < 1024; ++y)
        for (int x = 0; x < 1024; ++x)
            big.setPixelColor(x, y, QColor::fromRgb(rng.generate()));
    if (!big.save(bigPath, "PNG"))
        return false;
    return QFileInfo(bigPath).size() > 512 * 1024;
}

static MainWindow *openMainWindow(SocketClient *admin, int navRow)
{
    auto *win = new MainWindow(admin, QStringLiteral("admin"), QStringLiteral("123456"));
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    waitFor([win] { return win->isVisible(); });
    QListWidget *nav = win->findChild<QListWidget *>(QStringLiteral("listWidgetNav"));
    if (nav)
        nav->setCurrentRow(navRow);
    QTest::qWait(150);
    return win;
}

// ---------------- 场景：电桩占用区分显示 + 占用桩重启/禁用 ----------------

static void scenarioPileOcc(const QString &host, quint16 port, const QString &shotdir)
{
    SocketClient admin;
    CHECK(connectAndLogin(admin, host, port));
    SeedStation st;
    CHECK(seedStation(&admin, 4, &st));

    SocketClient user;
    QString phone;
    int userId = 0;
    CHECK(seedUser(&user, host, port, 100.0, &phone, &userId));

    // 占用构造：p0 预约占用、p1 充电占用、p2 完整周期（待结算不占桩，回到空闲）、p3 保持空闲
    const int orderR = reserveOrder(&user, st.piles[0].first);
    const int orderC = reserveOrder(&user, st.piles[1].first);
    CHECK(orderR > 0 && orderC > 0);
    CHECK(startOrder(&user, orderC));
    const int orderP = reserveOrder(&user, st.piles[2].first);
    CHECK(orderP > 0);
    CHECK(startOrder(&user, orderP) && stopOrder(&user, orderP));

    // 协议层复核 pile_list 的 occupancy 口径（v2.4）
    const QJsonObject p0 = pileById(&admin, st.stationId, st.piles[0].first);
    const QJsonObject p1 = pileById(&admin, st.stationId, st.piles[1].first);
    const QJsonObject p2 = pileById(&admin, st.stationId, st.piles[2].first);
    const QJsonObject p3 = pileById(&admin, st.stationId, st.piles[3].first);
    CHECK(p0[QStringLiteral("status")].toString() == QStringLiteral("in_use")
          && p0[QStringLiteral("occupancy")].toString() == QStringLiteral("reserved"));
    CHECK(p1[QStringLiteral("occupancy")].toString() == QStringLiteral("charging"));
    CHECK(p2[QStringLiteral("status")].toString() == QStringLiteral("idle")
          && p2[QStringLiteral("occupancy")].isNull());
    CHECK(p3[QStringLiteral("status")].toString() == QStringLiteral("idle"));
    log(QStringLiteral("协议复核：reserved/charging 占用与待结算释放电桩口径正确"));

    MainWindow *win = openMainWindow(&admin, 1);
    StationPilePage *page = win->findChild<StationPilePage *>();
    QTableWidget *stationTable = win->findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QTableWidget *pileTable = win->findChild<QTableWidget *>(QStringLiteral("pileTable"));
    QPushButton *restartBtn = findButton(page, QStringLiteral("远程重启"));
    QPushButton *disableBtn = win->findChild<QPushButton *>(QStringLiteral("btnDisablePile"));
    QPushButton *activeOrderBtn = win->findChild<QPushButton *>(QStringLiteral("btnActiveOrder"));
    CHECK(page && stationTable && pileTable && restartBtn && disableBtn && activeOrderBtn);
    if (!page || !stationTable || !pileTable || !restartBtn || !disableBtn || !activeOrderBtn)
        return;

    // 联调库可能有历史站点，按站名搜索定位自建站点
    QLineEdit *searchEdit = page->findChild<QLineEdit *>();
    CHECK(searchEdit != nullptr);
    searchEdit->setText(st.name);
    CHECK(clickButton(page, QStringLiteral("查询")));
    CHECK(waitFor([&] { return stationTable->rowCount() == 1; }));
    CHECK(waitFor([&] { return pileTable->rowCount() == 4; }));

    auto statusItemOf = [&](const QString &code) -> QTableWidgetItem * {
        const int row = rowOfText(pileTable, 0, code);
        return row >= 0 ? pileTable->item(row, 4) : nullptr;
    };
    auto statusOf = [&](const QString &code) {
        QTableWidgetItem *it = statusItemOf(code);
        return it ? it->text() : QString();
    };
    // 占用类型区分显示：预约中/充电中/空闲（待结算不占桩）
    CHECK(statusOf(st.piles[0].second) == QStringLiteral("预约中"));
    CHECK(statusOf(st.piles[1].second) == QStringLiteral("充电中"));
    CHECK(statusOf(st.piles[2].second) == QStringLiteral("空闲"));
    CHECK(statusOf(st.piles[3].second) == QStringLiteral("空闲"));
    // 语义色：预约中 警告橙 #ED6C02、充电中 信息蓝 #1565C0（色板值，见 uienums.h）
    if (QTableWidgetItem *it = statusItemOf(st.piles[0].second))
        CHECK(it->foreground().color() == QColor(237, 108, 2));
    if (QTableWidgetItem *it = statusItemOf(st.piles[1].second))
        CHECK(it->foreground().color() == QColor(21, 101, 192));
    log(QStringLiteral("电桩状态列：预约中(橙)/充电中(蓝)/空闲 显示与配色正确"));
    saveShot(win, shotdir, QStringLiteral("pileocc"), QStringLiteral("1_occupancy"));

    auto selectPile = [&](const QString &code) {
        const int row = rowOfText(pileTable, 0, code);
        CHECK(row >= 0);
        if (row >= 0)
            pileTable->selectRow(row);
        QTest::qWait(60);
    };

    // 预约占用桩：重启/禁用/占用详情均可用；重启强制终结预约订单（已取消）
    selectPile(st.piles[0].second);
    CHECK(restartBtn->isEnabled() && disableBtn->isEnabled() && activeOrderBtn->isEnabled());
    g_messages.clear();
    QTest::mouseClick(restartBtn, Qt::LeftButton, Qt::NoModifier, restartBtn->rect().center());
    CHECK(waitFor([&] { return statusOf(st.piles[0].second) == QStringLiteral("空闲"); }));
    CHECK(hasMessage(QStringLiteral("该桩存在占用订单（预约中），重启将强制终结该订单")));
    CHECK(hasMessage(QStringLiteral("关联订单 #%1 已取消").arg(orderR)));
    CHECK(orderStatusById(&admin, orderR) == QStringLiteral("cancelled"));
    CHECK(pileById(&admin, st.stationId, st.piles[0].first)[QStringLiteral("status")].toString()
          == QStringLiteral("idle"));
    log(QStringLiteral("预约占用桩远程重启：订单被取消、电桩释放"));

    // 充电占用桩：禁用可用；禁用强制终结充电订单（计费转入待结算）
    selectPile(st.piles[1].second);
    CHECK(restartBtn->isEnabled() && disableBtn->isEnabled() && activeOrderBtn->isEnabled());
    g_messages.clear();
    QTest::mouseClick(disableBtn, Qt::LeftButton, Qt::NoModifier, disableBtn->rect().center());
    CHECK(waitFor([&] { return statusOf(st.piles[1].second) == QStringLiteral("故障"); }));
    CHECK(hasMessage(QStringLiteral("该桩存在占用订单（充电中），禁用将强制终结该订单")));
    CHECK(hasMessage(QStringLiteral("关联订单 #%1 已转入待结算").arg(orderC)));
    CHECK(orderStatusById(&admin, orderC) == QStringLiteral("pending_payment"));
    log(QStringLiteral("充电占用桩禁用：订单计费转入待结算、电桩故障下线"));
    saveShot(win, shotdir, QStringLiteral("pileocc"), QStringLiteral("2_disabled"));

    // 故障桩：禁用置灰、重启可用；无占用订单时结果不带关联订单
    selectPile(st.piles[1].second);
    CHECK(!disableBtn->isEnabled() && restartBtn->isEnabled() && !activeOrderBtn->isEnabled());
    g_messages.clear();
    QTest::mouseClick(restartBtn, Qt::LeftButton, Qt::NoModifier, restartBtn->rect().center());
    CHECK(waitFor([&] { return statusOf(st.piles[1].second) == QStringLiteral("空闲"); }));
    CHECK(hasMessage(QStringLiteral("重启成功，电桩已恢复空闲")));
    CHECK(!g_messages.join(QStringLiteral("\n")).contains(QStringLiteral("关联订单")));
    CHECK(pileById(&admin, st.stationId, st.piles[1].first)[QStringLiteral("status")].toString()
          == QStringLiteral("idle"));
    log(QStringLiteral("故障桩重启：恢复空闲，无关联订单信息"));
    saveShot(win, shotdir, QStringLiteral("pileocc"), QStringLiteral("3_after"));
    win->close();
}

// ---------------- 场景：用户资料编辑（头像/余额/昵称） ----------------

static void scenarioUserEdit(const QString &host, quint16 port, const QString &shotdir)
{
    SocketClient admin;
    CHECK(connectAndLogin(admin, host, port));
    SocketClient user;
    QString phone;
    int userId = 0;
    CHECK(seedUser(&user, host, port, 100.0, &phone, &userId));

    const QString pngPath = shotdir + QStringLiteral("/v24_avatar.png");
    const QString bigPath = shotdir + QStringLiteral("/v24_big.png");
    CHECK(makeTestImages(pngPath, bigPath));

    MainWindow *win = openMainWindow(&admin, 2);
    UserPage *page = win->findChild<UserPage *>();
    QTableWidget *userTable = win->findChild<QTableWidget *>(QStringLiteral("userTable"));
    QPushButton *editBtn = findButton(page, QStringLiteral("修改"));
    CHECK(page && userTable && editBtn);
    if (!page || !userTable || !editBtn)
        return;

    QLineEdit *searchEdit = page->findChild<QLineEdit *>();
    CHECK(searchEdit != nullptr);
    searchEdit->setText(phone);
    CHECK(clickButton(page, QStringLiteral("查询")));
    CHECK(waitFor([&] { return userTable->rowCount() == 1; }));
    userTable->selectRow(0);
    QTest::qWait(60);
    CHECK(editBtn->isEnabled());
    // 初始行：余额 100.00、昵称为服务端默认「用户+后4位」
    CHECK(userTable->item(0, 3)->text() == QStringLiteral("100.00"));

    // 第一次编辑：无头像占位 → 超限图被拒 → 选 PNG 成功 → 改昵称/余额 → 保存
    g_dialogHandlers << [&, pngPath, bigPath](QDialog *d) {
        CHECK(d->objectName() == QStringLiteral("userEditDialog"));
        QLabel *preview = d->findChild<QLabel *>(QStringLiteral("avatarPreview"));
        QDoubleSpinBox *balanceBox = d->findChild<QDoubleSpinBox *>(QStringLiteral("spinBalance"));
        QLineEdit *nicknameEdit = d->findChild<QLineEdit *>(QStringLiteral("editNickname"));
        QLineEdit *phoneEdit = d->findChild<QLineEdit *>(QStringLiteral("editPhone"));
        QPushButton *clearBtn = d->findChild<QPushButton *>(QStringLiteral("btnClearAvatar"));
        const bool widgetsOk = preview && balanceBox && nicknameEdit && phoneEdit && clearBtn;
        CHECK(widgetsOk);
        if (widgetsOk) {
            CHECK(phoneEdit->text() == phone);
            CHECK(qAbs(balanceBox->value() - 100.0) < 0.001); // 对话框显示当前余额
            CHECK(preview->text() == QStringLiteral("（无头像）")); // 占位文字
            CHECK(!clearBtn->isEnabled());
        }
        // 嵌套模态（超限图的警告 QMessageBox）必须延迟到 poll 调用栈之外触发，
        // 否则 poll 计时器被嵌套 exec 卡住、无法再自动应答该警告框
        QTimer::singleShot(0, d, [=]() {
            if (!widgetsOk) {
                d->reject();
                return;
            }
            bool okBig = true;
            QMetaObject::invokeMethod(d, "chooseAvatarFromFile", Q_RETURN_ARG(bool, okBig),
                                      Q_ARG(QString, bigPath));
            CHECK(!okBig); // >512KiB 拒绝（拒绝提示由自动应答关闭并记录）
            CHECK(preview->text() == QStringLiteral("（无头像）"));
            bool okPng = false;
            QMetaObject::invokeMethod(d, "chooseAvatarFromFile", Q_RETURN_ARG(bool, okPng),
                                      Q_ARG(QString, pngPath));
            CHECK(okPng);
            CHECK(preview->text().isEmpty()); // 已显示所选图片
            CHECK(clearBtn->isEnabled());
            nicknameEdit->setText(QStringLiteral("v24新昵称"));
            balanceBox->setValue(200.50);
            d->grab().save(shotdir + QStringLiteral("/v24_useredit_1_dialog.png"));
            d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
    };
    g_messages.clear();
    QTest::mouseClick(editBtn, Qt::LeftButton, Qt::NoModifier, editBtn->rect().center());
    CHECK(waitFor([] { return hasMessage(QStringLiteral("修改用户 | 保存成功")); }));
    CHECK(hasMessage(QStringLiteral("图片大小不能超过 512 KiB")));
    // 列表行就地刷新（响应完整资料）
    CHECK(userTable->item(0, 2)->text() == QStringLiteral("v24新昵称"));
    CHECK(userTable->item(0, 3)->text() == QStringLiteral("200.50"));
    // 协议复核：头像与余额已保存
    {
        auto r = request(&admin, QStringLiteral("user_detail"),
                         QJsonObject{{QStringLiteral("userId"), userId}});
        const QJsonObject u = r->data[QStringLiteral("user")].toObject();
        CHECK(qAbs(u[QStringLiteral("balance")].toDouble() - 200.50) < 0.001);
        CHECK(u[QStringLiteral("nickname")].toString() == QStringLiteral("v24新昵称"));
        CHECK(u[QStringLiteral("avatar")].toObject()[QStringLiteral("mime")].toString()
              == QStringLiteral("image/png"));
        CHECK(!u[QStringLiteral("avatar")].toObject()[QStringLiteral("base64")].toString().isEmpty());
    }
    log(QStringLiteral("编辑用户：选图/改昵称/改余额保存成功，列表行与 user_detail 一致"));
    saveShot(win, shotdir, QStringLiteral("useredit"), QStringLiteral("2_saved"));

    // 第二次编辑：对话框经 user_detail 显示当前头像 → 清除头像 → 保存
    g_dialogHandlers << [&, shotdir](QDialog *d) {
        QLabel *preview = d->findChild<QLabel *>(QStringLiteral("avatarPreview"));
        QPushButton *clearBtn = d->findChild<QPushButton *>(QStringLiteral("btnClearAvatar"));
        QDoubleSpinBox *balanceBox = d->findChild<QDoubleSpinBox *>(QStringLiteral("spinBalance"));
        CHECK(preview && clearBtn && balanceBox);
        if (!preview || !clearBtn) {
            d->reject();
            return;
        }
        CHECK(preview->text().isEmpty()); // 显示服务端返回的当前头像
        CHECK(qAbs(balanceBox->value() - 200.50) < 0.001);
        CHECK(clearBtn->isEnabled());
        d->grab().save(shotdir + QStringLiteral("/v24_useredit_3_avatar.png"));
        QTest::mouseClick(clearBtn, Qt::LeftButton, Qt::NoModifier, clearBtn->rect().center());
        CHECK(preview->text() == QStringLiteral("（已清除）"));
        CHECK(!clearBtn->isEnabled());
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    g_messages.clear();
    userTable->selectRow(0);
    QTest::qWait(60);
    QTest::mouseClick(editBtn, Qt::LeftButton, Qt::NoModifier, editBtn->rect().center());
    CHECK(waitFor([] { return hasMessage(QStringLiteral("修改用户 | 保存成功")); }));
    {
        auto r = request(&admin, QStringLiteral("user_detail"),
                         QJsonObject{{QStringLiteral("userId"), userId}});
        CHECK(r->data[QStringLiteral("user")].toObject()[QStringLiteral("avatar")].isNull());
        // 未改字段保持不变（余额/昵称不回退）
        CHECK(qAbs(r->data[QStringLiteral("user")].toObject()[QStringLiteral("balance")].toDouble() - 200.50) < 0.001);
    }
    log(QStringLiteral("编辑用户：显示当前头像、清除头像（显式 null）生效"));

    // 第三次编辑：未做任何修改 → 不发请求，提示「未做任何修改」
    g_dialogHandlers << [](QDialog *d) {
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    g_messages.clear();
    userTable->selectRow(0);
    QTest::qWait(60);
    QTest::mouseClick(editBtn, Qt::LeftButton, Qt::NoModifier, editBtn->rect().center());
    CHECK(waitFor([] { return hasMessage(QStringLiteral("未做任何修改")); }));
    log(QStringLiteral("编辑用户：无改动时不上送 user_update"));
    win->close();
}

// ---------------- 场景：订单干预（取消预约/停止充电） ----------------

static void scenarioOrderOps(const QString &host, quint16 port, const QString &shotdir)
{
    SocketClient admin;
    CHECK(connectAndLogin(admin, host, port));
    SeedStation st;
    CHECK(seedStation(&admin, 4, &st));
    SocketClient user;
    QString phone;
    int userId = 0;
    CHECK(seedUser(&user, host, port, 200.0, &phone, &userId));

    // 订单构造（时间序）：F 已完成、P 待结算、C 充电中、R 已预约
    const int orderF = reserveOrder(&user, st.piles[0].first);
    CHECK(orderF > 0 && startOrder(&user, orderF) && stopOrder(&user, orderF) && settleOrder(&user, orderF));
    const int orderP = reserveOrder(&user, st.piles[1].first);
    CHECK(orderP > 0 && startOrder(&user, orderP) && stopOrder(&user, orderP));
    const int orderC = reserveOrder(&user, st.piles[2].first);
    CHECK(orderC > 0 && startOrder(&user, orderC));
    const int orderR = reserveOrder(&user, st.piles[3].first);
    CHECK(orderR > 0);

    MainWindow *win = openMainWindow(&admin, 3);
    OrderPage *page = win->findChild<OrderPage *>();
    QTableWidget *orderTable = win->findChild<QTableWidget *>(QStringLiteral("orderTable"));
    QPushButton *cancelBtn = win->findChild<QPushButton *>(QStringLiteral("btnCancelOrder"));
    QPushButton *stopBtn = win->findChild<QPushButton *>(QStringLiteral("btnStopCharge"));
    CHECK(page && orderTable && cancelBtn && stopBtn);
    if (!page || !orderTable || !cancelBtn || !stopBtn)
        return;

    QLineEdit *phoneEdit = page->findChild<QLineEdit *>();
    CHECK(phoneEdit != nullptr);
    phoneEdit->setText(phone);
    CHECK(clickButton(page, QStringLiteral("查询")));
    CHECK(waitFor([&] { return orderTable->rowCount() == 4; }));

    auto selectOrder = [&](int orderId) {
        const int row = rowOfText(orderTable, 0, QString::number(orderId));
        CHECK(row >= 0);
        if (row >= 0)
            orderTable->selectRow(row);
        QTest::qWait(60);
    };
    auto statusOfOrder = [&](int orderId) {
        const int row = rowOfText(orderTable, 0, QString::number(orderId));
        return row >= 0 ? orderTable->item(row, 4)->text() : QString();
    };

    // reserved 行：「取消预约」可用并生效（订单取消 + 电桩释放）
    selectOrder(orderR);
    CHECK(cancelBtn->isEnabled() && !stopBtn->isEnabled());
    saveShot(win, shotdir, QStringLiteral("orderops"), QStringLiteral("1_reserved_selected"));
    g_messages.clear();
    QTest::mouseClick(cancelBtn, Qt::LeftButton, Qt::NoModifier, cancelBtn->rect().center());
    CHECK(waitFor([&] { return statusOfOrder(orderR) == QStringLiteral("已取消"); }));
    CHECK(hasMessage(QStringLiteral("确定要取消订单 #%1 的预约吗").arg(orderR)));
    CHECK(hasMessage(QStringLiteral("订单已取消，电桩已释放为空闲")));
    CHECK(orderStatusById(&admin, orderR) == QStringLiteral("cancelled"));
    CHECK(pileById(&admin, st.stationId, st.piles[3].first)[QStringLiteral("status")].toString()
          == QStringLiteral("idle"));
    log(QStringLiteral("取消预约：reserved → cancelled，电桩释放"));

    // charging 行：「停止充电」可用并生效（计费进入待结算 + 电桩释放）
    selectOrder(orderC);
    CHECK(!cancelBtn->isEnabled() && stopBtn->isEnabled());
    g_messages.clear();
    QTest::mouseClick(stopBtn, Qt::LeftButton, Qt::NoModifier, stopBtn->rect().center());
    CHECK(waitFor([&] { return statusOfOrder(orderC) == QStringLiteral("待结算"); }));
    CHECK(hasMessage(QStringLiteral("将按已充时长计费，订单进入待结算")));
    CHECK(hasMessage(QStringLiteral("已停止充电，订单转入待结算")));
    CHECK(orderStatusById(&admin, orderC) == QStringLiteral("pending_payment"));
    CHECK(pileById(&admin, st.stationId, st.piles[2].first)[QStringLiteral("status")].toString()
          == QStringLiteral("idle"));
    log(QStringLiteral("停止充电：charging → pending_payment，电桩释放"));

    // completed / pending_payment / cancelled 行：两按钮皆禁用
    selectOrder(orderF);
    CHECK(!cancelBtn->isEnabled() && !stopBtn->isEnabled());
    selectOrder(orderP);
    CHECK(!cancelBtn->isEnabled() && !stopBtn->isEnabled());
    selectOrder(orderR);
    CHECK(!cancelBtn->isEnabled() && !stopBtn->isEnabled());
    log(QStringLiteral("completed/pending_payment/cancelled 行两按钮皆禁用"));
    saveShot(win, shotdir, QStringLiteral("orderops"), QStringLiteral("2_after"));
    win->close();
}

// ---------------- main ----------------

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NeusoftEVCP"));
    QApplication::setApplicationName(QStringLiteral("evcp-admin-v24-harness"));
    // 与 admin/main.cpp 一致的基准字号
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    QCommandLineParser parser;
    parser.addOptions({
        {QStringLiteral("scenario"), QStringLiteral("场景名"), QStringLiteral("name")},
        {QStringLiteral("host"), QStringLiteral("服务端地址"), QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), QStringLiteral("服务端端口"), QStringLiteral("port"), QStringLiteral("8888")},
        {QStringLiteral("shotdir"), QStringLiteral("截图输出目录"), QStringLiteral("dir"), QStringLiteral("/tmp")},
    });
    parser.process(app);
    const QString scenario = parser.value(QStringLiteral("scenario"));
    const QString host = parser.value(QStringLiteral("host"));
    const quint16 port = static_cast<quint16>(parser.value(QStringLiteral("port")).toUShort());
    const QString shotdir = parser.value(QStringLiteral("shotdir"));
    QDir().mkpath(shotdir);

    QTimer modalTimer;
    QObject::connect(&modalTimer, &QTimer::timeout, &pollModalWidgets);
    modalTimer.start(40);

    qInfo().noquote() << QStringLiteral("scenario: %1 server: %2:%3").arg(scenario, host).arg(port);
    if (scenario == QStringLiteral("pileocc"))
        scenarioPileOcc(host, port, shotdir);
    else if (scenario == QStringLiteral("useredit"))
        scenarioUserEdit(host, port, shotdir);
    else if (scenario == QStringLiteral("orderops"))
        scenarioOrderOps(host, port, shotdir);
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
