// admin 模块 v2.5 协议验证 harness（offscreen）：链接真实 MainWindow/各页面/SocketClient。
//
// 场景（--scenario）：
//   validate    离线层：StationImport 预校验（v2.5 协议 7.8 piles 清单规则）纯断言，
//               不需要服务端
//   addstation  联调层：新增站点对话框（电桩清单表格编辑、添加/删除行、类型联动功率默认值、
//               空表/重复编号预校验），提交 station_add 到 mock_server_v25.py
//   importflow  联调层：导入站点（混合合法/非法 JSON 文件逐条预校验原因、
//               合法条目透传创建、非法文件/空数组提示）
// 截图输出到 --shotdir（默认 /tmp），文件名 v25_<scenario>_<name>.png。
// 退出码：0=PASS，2=FAIL，3=harness 自身错误。用法见同目录 run_scenarios.sh。

#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

#include "net/socketclient.h"
#include "ui/mainwindow.h"
#include "ui/stationimport.h"
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
    if (!w || w->property("v25handled").toBool())
        return;
    if (qobject_cast<QProgressDialog *>(w))
        return; // 导入进度条非应答对象
    if (QMessageBox *mb = qobject_cast<QMessageBox *>(w)) {
        w->setProperty("v25handled", true);
        g_messages << (mb->windowTitle() + QStringLiteral(" | ") + mb->text());
        qInfo().noquote() << QStringLiteral("  [msgbox] %1 : %2").arg(mb->windowTitle(), mb->text());
        if (QAbstractButton *yes = mb->button(QMessageBox::Yes))
            yes->click();
        else if (QAbstractButton *ok = mb->button(QMessageBox::Ok))
            ok->click();
        return;
    }
    if (QDialog *d = qobject_cast<QDialog *>(w)) {
        w->setProperty("v25handled", true);
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

static bool clickButton(QWidget *root, const QString &text)
{
    QPushButton *btn = findButton(root, text);
    if (!btn || !btn->isEnabled())
        return false;
    QTest::mouseClick(btn, Qt::LeftButton, Qt::NoModifier, btn->rect().center());
    return true;
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

static int rowOfText(QTableWidget *table, int col, const QString &text)
{
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->item(r, col) && table->item(r, col)->text() == text)
            return r;
    return -1;
}

static void saveShot(QWidget *w, const QString &shotdir, const QString &scenario, const QString &name)
{
    const QString path = QStringLiteral("%1/v25_%2_%3.png").arg(shotdir, scenario, name);
    if (w->grab().save(path))
        log(QStringLiteral("截图 %1").arg(path));
    else
        CHECK(!"截图保存失败");
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

// ---------------- 场景：离线预校验（StationImport，v2.5 协议 7.8） ----------------

static QJsonObject stationEntry(const QJsonValue &piles)
{
    return QJsonObject{{QStringLiteral("name"), QStringLiteral("校验站")},
                       {QStringLiteral("address"), QStringLiteral("校验地址")},
                       {QStringLiteral("lng"), 116.3},
                       {QStringLiteral("lat"), 39.9},
                       {QStringLiteral("pricePerKwh"), 1.2},
                       {QStringLiteral("piles"), piles}};
}

static QJsonObject pileEntry(const QString &code, const QString &type, double power)
{
    return QJsonObject{{QStringLiteral("code"), code},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("powerKw"), power}};
}

static void scenarioValidate()
{
    QJsonObject payload;
    QString error;

    // 合法条目：code 去空白、type 中文映射为英文枚举、其余字段原样透传
    {
        const QJsonObject good = stationEntry(QJsonArray{
            pileEntry(QStringLiteral(" P-0001 "), QStringLiteral("快充"), 60.0),
            pileEntry(QStringLiteral("P-0002"), QStringLiteral("slow"), 7.0)});
        CHECK(StationImport::validateStationEntry(good, &payload, &error));
        const QJsonArray piles = payload[QStringLiteral("piles")].toArray();
        CHECK(piles.size() == 2);
        CHECK(piles.at(0).toObject()[QStringLiteral("code")].toString() == QStringLiteral("P-0001"));
        CHECK(piles.at(0).toObject()[QStringLiteral("type")].toString() == QStringLiteral("fast"));
        CHECK(piles.at(1).toObject()[QStringLiteral("type")].toString() == QStringLiteral("slow"));
        CHECK(qAbs(piles.at(0).toObject()[QStringLiteral("powerKw")].toDouble() - 60.0) < 1e-9);
        CHECK(payload[QStringLiteral("name")].toString() == QStringLiteral("校验站"));
        CHECK(qAbs(payload[QStringLiteral("lng")].toDouble() - 116.3) < 1e-9);
        log(QStringLiteral("合法条目：透传成功，中文类型映射 fast，code 去空白"));
    }

    // piles 缺失 / 非数组 / 空数组 / 101 条
    for (const QJsonValue &piles : {QJsonValue(), QJsonValue(5), QJsonValue(QJsonArray())}) {
        CHECK(!StationImport::validateStationEntry(stationEntry(piles), &payload, &error));
        CHECK(error == QStringLiteral("piles 必须为 1 至 100 条的数组"));
    }
    {
        QJsonArray tooMany;
        for (int i = 0; i < 101; ++i)
            tooMany.append(pileEntry(QStringLiteral("P-%1").arg(i, 4, 10, QLatin1Char('0')),
                                     QStringLiteral("fast"), 60.0));
        CHECK(!StationImport::validateStationEntry(stationEntry(tooMany), &payload, &error));
        CHECK(error == QStringLiteral("piles 必须为 1 至 100 条的数组"));
        // 恰好 100 条合法
        QJsonArray hundred = tooMany;
        hundred.removeAt(100);
        CHECK(StationImport::validateStationEntry(stationEntry(hundred), &payload, &error));
        log(QStringLiteral("piles 条数边界：缺失/非数组/空/101 条拒绝，100 条通过"));
    }

    // code：非字符串、空白、超 20 字符
    for (const QJsonValue &code : {QJsonValue(123), QJsonValue(QStringLiteral("  ")),
                                   QJsonValue(QStringLiteral("X").repeated(21))}) {
        const QJsonArray piles{QJsonObject{{QStringLiteral("code"), code},
                                           {QStringLiteral("type"), QStringLiteral("fast")},
                                           {QStringLiteral("powerKw"), 60.0}}};
        CHECK(!StationImport::validateStationEntry(stationEntry(piles), &payload, &error));
        CHECK(error.contains(QStringLiteral("code 必须为 1 至 20 字符的字符串")));
    }
    log(QStringLiteral("code 校验：非字符串/空白/超长均拒绝"));

    // type：fast/slow 与中文 快充/慢充 之外的值报错
    {
        const QJsonArray piles{pileEntry(QStringLiteral("P-1"), QStringLiteral("超充"), 60.0)};
        CHECK(!StationImport::validateStationEntry(stationEntry(piles), &payload, &error));
        CHECK(error.contains(QStringLiteral("type 必须为 fast/slow")));
        const QJsonArray cn{pileEntry(QStringLiteral("P-1"), QStringLiteral("慢充"), 7.0)};
        CHECK(StationImport::validateStationEntry(stationEntry(cn), &payload, &error));
        CHECK(payload[QStringLiteral("piles")].toArray().at(0).toObject()
                  [QStringLiteral("type")].toString() == QStringLiteral("slow"));
        log(QStringLiteral("type 校验：非法值拒绝，中文 慢充 映射 slow"));
    }

    // powerKw：缺失 / 0 / 负数 / 非数字
    for (const QJsonValue &power : {QJsonValue(), QJsonValue(0), QJsonValue(-3.5),
                                    QJsonValue(QStringLiteral("60"))}) {
        const QJsonArray piles{QJsonObject{{QStringLiteral("code"), QStringLiteral("P-1")},
                                           {QStringLiteral("type"), QStringLiteral("fast")},
                                           {QStringLiteral("powerKw"), power}}};
        CHECK(!StationImport::validateStationEntry(stationEntry(piles), &payload, &error));
        CHECK(error.contains(QStringLiteral("powerKw 必须为大于 0 的数字")));
    }
    log(QStringLiteral("powerKw 校验：缺失/0/负数/非数字均拒绝"));

    // 同站 piles 内 code 重复（含去空白后重复），指出重复值
    {
        const QJsonArray piles{pileEntry(QStringLiteral("P-1"), QStringLiteral("fast"), 60.0),
                               pileEntry(QStringLiteral(" P-1 "), QStringLiteral("slow"), 7.0)};
        CHECK(!StationImport::validateStationEntry(stationEntry(piles), &payload, &error));
        CHECK(error == QStringLiteral("电桩编号 P-1 重复"));
        log(QStringLiteral("重复编号：%1").arg(error));
    }

    // 站点字段级校验（沿用 v2.4 规则）
    {
        QJsonObject bad = stationEntry(QJsonArray{pileEntry(QStringLiteral("P-1"),
                                                            QStringLiteral("fast"), 60.0)});
        bad[QStringLiteral("name")] = QStringLiteral("  ");
        CHECK(!StationImport::validateStationEntry(bad, &payload, &error));
        CHECK(error == QStringLiteral("name/address 不能为空"));
        bad[QStringLiteral("name")] = QStringLiteral("校验站");
        bad[QStringLiteral("lng")] = 181.0;
        CHECK(!StationImport::validateStationEntry(bad, &payload, &error));
        CHECK(error == QStringLiteral("lng/lat 非法或超出范围"));
        bad[QStringLiteral("lng")] = 116.3;
        bad[QStringLiteral("pricePerKwh")] = 0.0;
        CHECK(!StationImport::validateStationEntry(bad, &payload, &error));
        CHECK(error == QStringLiteral("pricePerKwh 必须大于 0"));
        log(QStringLiteral("站点字段校验：name/lng/price 规则不变"));
    }
}

// ---------------- 场景：新增站点对话框（电桩清单编辑 + 提交） ----------------

static void fillStationForm(QDialog *d, const QString &name)
{
    const auto edits = plainEdits(d);
    CHECK(edits.size() >= 4); // 站名/地址/经度/纬度（之后是表格内的编号编辑框）
    edits[0]->setText(name);
    edits[1]->setText(QStringLiteral("v25 测试地址"));
    edits[2]->setText(QStringLiteral("116.300000"));
    edits[3]->setText(QStringLiteral("39.950000"));
}

static void scenarioAddStation(const QString &host, quint16 port, const QString &shotdir)
{
    SocketClient admin;
    CHECK(connectAndLogin(admin, host, port));
    MainWindow *win = openMainWindow(&admin, 1);
    StationPilePage *page = win->findChild<StationPilePage *>();
    QTableWidget *stationTable = win->findChild<QTableWidget *>(QStringLiteral("stationTable"));
    CHECK(page && stationTable);
    if (!page || !stationTable)
        return;
    CHECK(waitFor([&] { return stationTable->rowCount() == 2; }));

    // 对话框默认带 2 行电桩（编号占位 P-0001/P-0002、类型快充、功率 60）
    // 全部删除后提交 → 预校验拒绝「请至少添加 1 个电桩」，不发请求
    g_dialogHandlers << [](QDialog *d) {
        QTableWidget *t = d->findChild<QTableWidget *>(QStringLiteral("addStationPileTable"));
        CHECK(t != nullptr);
        if (!t) {
            d->reject();
            return;
        }
        CHECK(t->columnCount() == 3);
        CHECK(t->rowCount() == 2);
        QLineEdit *code0 = qobject_cast<QLineEdit *>(t->cellWidget(0, 0));
        QComboBox *type0 = qobject_cast<QComboBox *>(t->cellWidget(0, 1));
        QDoubleSpinBox *power0 = qobject_cast<QDoubleSpinBox *>(t->cellWidget(0, 2));
        CHECK(code0 && type0 && power0);
        if (code0 && type0 && power0) {
            CHECK(code0->text() == QStringLiteral("P-0001"));
            CHECK(type0->currentData().toString() == QStringLiteral("fast"));
            CHECK(qAbs(power0->value() - 60.0) < 1e-9);
        }
        fillStationForm(d, QStringLiteral("v25空表站"));
        t->selectRow(1);
        clickButton(d, QStringLiteral("删除选中"));
        t->selectRow(0);
        clickButton(d, QStringLiteral("删除选中"));
        CHECK(t->rowCount() == 0);
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    g_messages.clear();
    CHECK(clickButton(page, QStringLiteral("新增站点")));
    CHECK(waitFor([] { return hasMessage(QStringLiteral("请至少添加 1 个电桩")); }));
    log(QStringLiteral("空电桩清单提交被拒：请至少添加 1 个电桩"));

    // 两行编号改相同 → 预校验拒绝「电桩编号 P-0001 重复」，不发请求
    g_dialogHandlers << [](QDialog *d) {
        QTableWidget *t = d->findChild<QTableWidget *>(QStringLiteral("addStationPileTable"));
        CHECK(t != nullptr);
        if (!t) {
            d->reject();
            return;
        }
        qobject_cast<QLineEdit *>(t->cellWidget(1, 0))->setText(QStringLiteral("P-0001"));
        fillStationForm(d, QStringLiteral("v25重复站"));
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    g_messages.clear();
    CHECK(clickButton(page, QStringLiteral("新增站点")));
    CHECK(waitFor([] { return hasMessage(QStringLiteral("电桩编号 P-0001 重复")); }));
    log(QStringLiteral("重复编号提交被拒：电桩编号 P-0001 重复"));

    // 正常提交：改编号、添加一行、类型切慢充联动功率默认 7，共 3 桩
    const QString stationName =
        QStringLiteral("v25新增站%1").arg(QDateTime::currentMSecsSinceEpoch() % 1000000);
    g_dialogHandlers << [shotdir, stationName](QDialog *d) {
        QTableWidget *t = d->findChild<QTableWidget *>(QStringLiteral("addStationPileTable"));
        CHECK(t != nullptr);
        if (!t) {
            d->reject();
            return;
        }
        fillStationForm(d, stationName);
        qobject_cast<QLineEdit *>(t->cellWidget(0, 0))->setText(QStringLiteral("V25-1001"));
        qobject_cast<QLineEdit *>(t->cellWidget(1, 0))->setText(QStringLiteral("V25-1002"));
        // 添加电桩：追加第三行，编号占位 P-0003
        CHECK(clickButton(d, QStringLiteral("添加电桩")));
        CHECK(t->rowCount() == 3);
        QLineEdit *code2 = qobject_cast<QLineEdit *>(t->cellWidget(2, 0));
        CHECK(code2 && code2->text() == QStringLiteral("P-0003"));
        if (code2)
            code2->setText(QStringLiteral("V25-1003"));
        // 类型切慢充 → 功率默认值联动为 7
        QComboBox *type2 = qobject_cast<QComboBox *>(t->cellWidget(2, 1));
        QDoubleSpinBox *power2 = qobject_cast<QDoubleSpinBox *>(t->cellWidget(2, 2));
        CHECK(type2 && power2);
        if (type2 && power2) {
            type2->setCurrentIndex(1); // 慢充
            CHECK(type2->currentData().toString() == QStringLiteral("slow"));
            CHECK(qAbs(power2->value() - 7.0) < 1e-9);
        }
        d->grab().save(shotdir + QStringLiteral("/v25_addstation_1_dialog.png"));
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    g_messages.clear();
    CHECK(clickButton(page, QStringLiteral("新增站点")));
    CHECK(waitFor([] { return hasMessage(QStringLiteral("已创建 3 个电桩")); }));
    CHECK(hasMessage(QStringLiteral("创建成功，站点ID：")));
    CHECK(waitFor([&] { return stationTable->rowCount() == 3; }));
    const int row = rowOfText(stationTable, 1, stationName);
    CHECK(row >= 0);
    // 桩数/在线率合并列显示 3 桩
    if (row >= 0)
        CHECK(stationTable->item(row, 4)->text().contains(QStringLiteral("3")));
    log(QStringLiteral("新增站点成功：3 桩（2 快充 + 1 慢充），列表已刷新"));
    saveShot(win, shotdir, QStringLiteral("addstation"), QStringLiteral("2_created"));

    // 协议复核：mock 侧站点按 piles 清单建桩
    auto rl = request(&admin, QStringLiteral("station_list"), QJsonObject{});
    const QJsonArray stations = rl->data[QStringLiteral("stations")].toArray();
    bool found = false;
    for (const auto &v : stations) {
        const QJsonObject s = v.toObject();
        if (s[QStringLiteral("name")].toString() == stationName) {
            found = true;
            CHECK(s[QStringLiteral("pileTotal")].toInt() == 3);
            auto pl = request(&admin, QStringLiteral("pile_list"),
                              QJsonObject{{QStringLiteral("stationId"),
                                           s[QStringLiteral("stationId")].toInt()}});
            const QJsonArray piles = pl->data[QStringLiteral("piles")].toArray();
            CHECK(piles.size() == 3);
            CHECK(piles.at(0).toObject()[QStringLiteral("code")].toString()
                  == QStringLiteral("V25-1001"));
            CHECK(piles.at(2).toObject()[QStringLiteral("type")].toString()
                  == QStringLiteral("slow"));
            CHECK(qAbs(piles.at(2).toObject()[QStringLiteral("powerKw")].toDouble() - 7.0) < 1e-9);
        }
    }
    CHECK(found);
    log(QStringLiteral("协议复核：站点 3 桩与界面提交一致"));
    win->close();
}

// ---------------- 场景：导入站点（文件预校验 + 逐条导入） ----------------

static void scenarioImportFlow(const QString &host, quint16 port, const QString &shotdir)
{
    SocketClient admin;
    CHECK(connectAndLogin(admin, host, port));
    MainWindow *win = openMainWindow(&admin, 1);
    StationPilePage *page = win->findChild<StationPilePage *>();
    QTableWidget *stationTable = win->findChild<QTableWidget *>(QStringLiteral("stationTable"));
    CHECK(page && stationTable);
    if (!page || !stationTable)
        return;
    CHECK(waitFor([&] { return stationTable->rowCount() == 2; }));

    // 非法文件：非 JSON 数组 → 格式提示与新格式字段对齐（提 piles，不提 pileCount）
    {
        QFile f(QStringLiteral("/tmp/v25_import_bad.json"));
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("{\"name\":\"不是数组\"}");
        f.close();
    }
    g_messages.clear();
    QMetaObject::invokeMethod(page, "importStationsFromFile",
                              Q_ARG(QString, QStringLiteral("/tmp/v25_import_bad.json")));
    CHECK(waitFor([] { return hasMessage(QStringLiteral("文件格式非法")); }));
    CHECK(hasMessage(QStringLiteral("piles")));
    CHECK(!hasMessage(QStringLiteral("pileCount")));
    log(QStringLiteral("非法文件提示对齐新格式（含 piles，不含 pileCount）"));

    // 空数组文件
    {
        QFile f(QStringLiteral("/tmp/v25_import_empty.json"));
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("[]");
        f.close();
    }
    g_messages.clear();
    QMetaObject::invokeMethod(page, "importStationsFromFile",
                              Q_ARG(QString, QStringLiteral("/tmp/v25_import_empty.json")));
    CHECK(waitFor([] { return hasMessage(QStringLiteral("文件为空数组")); }));
    log(QStringLiteral("空数组文件提示"));

    // 混合文件：2 条合法（含中文类型与 code 空白）+ 7 条各类非法
    const QString suffix = QString::number(QDateTime::currentMSecsSinceEpoch() % 1000000);
    const QString name1 = QStringLiteral("v25导入站甲%1").arg(suffix);
    const QString name2 = QStringLiteral("v25导入站乙%1").arg(suffix);
    {
        QJsonArray items;
        items.append(stationEntry(QJsonArray{
            pileEntry(QStringLiteral(" V25-A01 "), QStringLiteral("快充"), 60.0),
            pileEntry(QStringLiteral("V25-A02"), QStringLiteral("slow"), 7.0)}));
        items.append(stationEntry(
            QJsonArray{pileEntry(QStringLiteral("V25-B01"), QStringLiteral("slow"), 7.0)}));
        // 非法：piles 缺失 / 101 条 / 同站编号重复 / 非法类型 / 功率为 0 / 空编号 / 非对象
        QJsonObject e3 = stationEntry(QJsonValue());
        e3.remove(QStringLiteral("piles"));
        items.append(e3);
        QJsonArray tooMany;
        for (int i = 0; i < 101; ++i)
            tooMany.append(pileEntry(QStringLiteral("M-%1").arg(i, 3, 10, QLatin1Char('0')),
                                     QStringLiteral("fast"), 60.0));
        items.append(stationEntry(tooMany));
        items.append(stationEntry(QJsonArray{
            pileEntry(QStringLiteral("DUP-1"), QStringLiteral("fast"), 60.0),
            pileEntry(QStringLiteral("DUP-1"), QStringLiteral("slow"), 7.0)}));
        items.append(stationEntry(
            QJsonArray{pileEntry(QStringLiteral("T-1"), QStringLiteral("超充"), 60.0)}));
        items.append(stationEntry(
            QJsonArray{pileEntry(QStringLiteral("W-1"), QStringLiteral("fast"), 0.0)}));
        items.append(stationEntry(
            QJsonArray{pileEntry(QStringLiteral(""), QStringLiteral("fast"), 60.0)}));
        items.append(42);
        // 合法条目改用含时间戳的唯一站名，非法条目统一命名
        QJsonArray finalItems;
        for (int i = 0; i < items.size(); ++i) {
            QJsonValue v = items.at(i);
            if (v.isObject()) {
                QJsonObject o = v.toObject();
                if (i == 0)
                    o[QStringLiteral("name")] = name1;
                else if (i == 1)
                    o[QStringLiteral("name")] = name2;
                else
                    o[QStringLiteral("name")] = QStringLiteral("v25坏条目%1").arg(i);
                v = o;
            }
            finalItems.append(v);
        }
        QFile f(QStringLiteral("/tmp/v25_import_mixed.json"));
        CHECK(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(finalItems).toJson(QJsonDocument::Compact));
        f.close();
    }
    g_messages.clear();
    QMetaObject::invokeMethod(page, "importStationsFromFile",
                              Q_ARG(QString, QStringLiteral("/tmp/v25_import_mixed.json")));
    // 汇总：成功 2 条、失败 7 条，失败明细逐条原因
    CHECK(waitFor([] { return hasMessage(QStringLiteral("导入完成：成功 2 条，失败 7 条")); },
                  30000));
    CHECK(hasMessage(QStringLiteral("piles 必须为 1 至 100 条的数组")));
    CHECK(hasMessage(QStringLiteral("电桩编号 DUP-1 重复")));
    CHECK(hasMessage(QStringLiteral("type 必须为 fast/slow")));
    CHECK(hasMessage(QStringLiteral("powerKw 必须为大于 0 的数字")));
    CHECK(hasMessage(QStringLiteral("code 必须为 1 至 20 字符的字符串")));
    CHECK(hasMessage(QStringLiteral("不是 JSON 对象")));
    log(QStringLiteral("混合文件：成功 2 / 失败 7，各失败原因断言通过"));
    CHECK(waitFor([&] { return stationTable->rowCount() == 4; }));
    CHECK(rowOfText(stationTable, 1, name1) >= 0 && rowOfText(stationTable, 1, name2) >= 0);
    saveShot(win, shotdir, QStringLiteral("importflow"), QStringLiteral("1_done"));

    // 协议复核：导入站按文件清单建桩（中文类型已映射、code 已去空白）
    auto rl = request(&admin, QStringLiteral("station_list"),
                      QJsonObject{{QStringLiteral("nameKeyword"), QStringLiteral("v25导入站甲")}});
    const QJsonArray stations = rl->data[QStringLiteral("stations")].toArray();
    CHECK(stations.size() == 1);
    if (stations.size() == 1) {
        const QJsonObject s = stations.at(0).toObject();
        CHECK(s[QStringLiteral("pileTotal")].toInt() == 2);
        auto pl = request(&admin, QStringLiteral("pile_list"),
                          QJsonObject{{QStringLiteral("stationId"),
                                       s[QStringLiteral("stationId")].toInt()}});
        const QJsonArray piles = pl->data[QStringLiteral("piles")].toArray();
        CHECK(piles.size() == 2);
        CHECK(piles.at(0).toObject()[QStringLiteral("code")].toString()
              == QStringLiteral("V25-A01"));
        CHECK(piles.at(0).toObject()[QStringLiteral("type")].toString()
              == QStringLiteral("fast"));
        CHECK(piles.at(1).toObject()[QStringLiteral("type")].toString()
              == QStringLiteral("slow"));
    }
    log(QStringLiteral("协议复核：导入站 2 桩，编号去空白、中文快充映射 fast"));
    win->close();
}

// ---------------- main ----------------

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NeusoftEVCP"));
    QApplication::setApplicationName(QStringLiteral("evcp-admin-v25-harness"));
    // 与 admin/main.cpp 一致的基准字号
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    QCommandLineParser parser;
    parser.addOptions({
        {QStringLiteral("scenario"), QStringLiteral("场景名"), QStringLiteral("name")},
        {QStringLiteral("host"), QStringLiteral("mock 地址"), QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), QStringLiteral("mock 端口"), QStringLiteral("port"), QStringLiteral("8893")},
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
    if (scenario == QStringLiteral("validate"))
        scenarioValidate();
    else if (scenario == QStringLiteral("addstation"))
        scenarioAddStation(host, port, shotdir);
    else if (scenario == QStringLiteral("importflow"))
        scenarioImportFlow(host, port, shotdir);
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
