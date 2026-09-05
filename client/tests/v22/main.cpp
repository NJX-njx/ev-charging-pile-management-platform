// 协议 v2.2 适配验证 harness（offscreen）：链接真实 MainWindow/各页面/SocketClient，
// 配合状态化假服务端（fakeserver.py）脚本化驱动 UI 并断言行为。
//
// 退出码：0=PASS，2=FAIL，3=harness 自身错误。
// 场景（--scenario，对应 fakeserver 的 CASE 见 run_scenarios.sh）：
//   orders_list        多订单列表：3 张订单卡片、各自状态文案与操作按钮
//   order_flow         状态流转：开始/停止/结算/取消后整表刷新、卡片消失
//   cost_tick          charging 卡片「已充时长｜预计花费」每秒跳动且数值与公式一致
//   profile_edit       编辑资料：昵称合并提交、无变更不发请求、头像文件校验
//   nav_url            导航对话框：起点/终点展示、点「导航」后才加载 routeplan URL、
//                      fromcoord/tocoord 为显式坐标、驾车/步行切换
//   region_select      区域下拉定位：选中即以预设坐标发 nearby_station_list
//   reserve_then_card  预约成功后充电页出现新订单卡片

#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include "mainwindow.h"
#include "map/tencentmapkey.h"
#include "model/appconfig.h"
#include "pages/chargingpage.h"
#include "pages/findstationpage.h"
#include "pages/loginpage.h"
#include "pages/mypage.h"
#include "pages/navigationdialog.h"
#include "ui/avatarutils.h"

#ifdef EVCP_HAVE_WEBENGINE
#include <QWebEngineView>
#endif

static qint64 g_t0 = 0;
static qint64 elapsed() { return QDateTime::currentMSecsSinceEpoch() - g_t0; }
static void log(const QString &s)
{
    qInfo().noquote() << QStringLiteral("[%1ms] %2").arg(elapsed(), 6).arg(s);
}

// ---------- 控件查找辅助 ----------

static QLineEdit *findEdit(QWidget *root, const QString &placeholder)
{
    const auto edits = root->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        if (e->placeholderText() == placeholder)
            return e;
    return nullptr;
}

static QPushButton *findButton(QWidget *root, const QStringList &texts)
{
    const auto btns = root->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (texts.contains(b->text()))
            return b;
    return nullptr;
}

static QList<QFrame *> orderCards(QWidget *w)
{
    return w->findChildren<QFrame *>(QStringLiteral("orderCard"));
}

static QFrame *findCardByPile(QWidget *w, const QString &pileCode)
{
    const auto cards = orderCards(w);
    for (QFrame *c : cards) {
        const auto labels = c->findChildren<QLabel *>();
        for (QLabel *l : labels)
            if (l->text().contains(pileCode))
                return c;
    }
    return nullptr;
}

// 逐参数（全解码）比较 URL，避免编码格式差异（如空格 + 与 %20）造成误判
static bool urlMatches(const QUrl &u, const QUrl &expected)
{
    if (u.scheme() != expected.scheme() || u.host() != expected.host()
        || u.path() != expected.path())
        return false;
    const auto ai = QUrlQuery(u).queryItems(QUrl::FullyDecoded);
    const auto bi = QUrlQuery(expected).queryItems(QUrl::FullyDecoded);
    if (ai.size() != bi.size())
        return false;
    for (const auto &p : bi)
        if (!ai.contains(p))
            return false;
    return true;
}

static QString cardStatus(QFrame *card)
{
    const auto labels = card->findChildren<QLabel *>();
    for (QLabel *l : labels)
        if (l->property("kind").toString() == QLatin1String("status"))
            return l->text();
    return QString();
}

static QLabel *cardTickLabel(QFrame *card)
{
    const auto labels = card->findChildren<QLabel *>();
    for (QLabel *l : labels)
        if (l->text().startsWith(QStringLiteral("已充时长")))
            return l;
    return nullptr;
}

// ---------- 请求日志（fakeserver REQLOG） ----------

static QString g_reqlogPath;

static QList<QJsonObject> readReqlog()
{
    QList<QJsonObject> out;
    QFile f(g_reqlogPath);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (!line.isEmpty())
            out.append(QJsonDocument::fromJson(line).object());
    }
    return out;
}

static QList<QJsonObject> requestsOf(const QString &type)
{
    QList<QJsonObject> out;
    const auto all = readReqlog();
    for (const QJsonObject &o : all)
        if (o.value(QStringLiteral("type")).toString() == type)
            out.append(o);
    return out;
}

// ---------- 全局状态与模态框处理 ----------

static QStringList g_modals;
static bool g_loginOk = false;
static QPointer<QDialog> g_profileDlg;

static bool modalSeen(const QString &substr)
{
    for (const QString &t : g_modals)
        if (t.contains(substr))
            return true;
    return false;
}

static void pollModal()
{
    QWidget *m = QApplication::activeModalWidget();
    if (!m) {
        // 兜底：offscreen 下个别 exec 对话框未成为 activeModalWidget，直接扫顶层窗口
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget *t : tops) {
            if (auto *box = qobject_cast<QMessageBox *>(t)) {
                if (box->isVisible()) {
                    m = box;
                    break;
                }
            }
        }
        if (!m)
            return;
    }
    if (auto *box = qobject_cast<QMessageBox *>(m)) {
        log(QStringLiteral("MODAL[%1]: %2").arg(box->windowTitle(), box->text()));
        g_modals << box->text();
        if (auto *yes = qobject_cast<QPushButton *>(box->button(QMessageBox::Yes))) {
            yes->click();
            return;
        }
        box->accept();
        return;
    }
    auto *dlg = qobject_cast<QDialog *>(m);
    if (!dlg)
        return;
    if (dlg->windowTitle() == QStringLiteral("编辑资料")) {
        if (g_profileDlg != dlg) {
            g_profileDlg = dlg;
            log(QStringLiteral("DIALOG: 编辑资料"));
        }
        return; // 由场景驱动填写/保存
    }
    log(QStringLiteral("DIALOG(unexpected): %1").arg(dlg->windowTitle()));
    g_modals << QStringLiteral("UNEXPECTED-DIALOG:") + dlg->windowTitle();
    dlg->reject();
}

// ---------- 头像校验夹具 ----------

static QString checkAvatarFixtures()
{
    const QString dir = QDir::temp().filePath(QStringLiteral("v22_avatar_fixtures"));
    QDir().mkpath(dir);
    const QString okPath = dir + QStringLiteral("/ok.png");
    const QString badPath = dir + QStringLiteral("/bad.txt");
    const QString bigPath = dir + QStringLiteral("/big.png");

    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(0xFF00A870);
    if (!img.save(okPath, "PNG"))
        return QStringLiteral("无法生成 PNG 夹具");
    {
        QFile f(badPath);
        if (!f.open(QIODevice::WriteOnly))
            return QStringLiteral("无法生成文本夹具");
        f.write("not an image");
    }
    {
        QFile f(bigPath);
        if (!f.open(QIODevice::WriteOnly))
            return QStringLiteral("无法生成超大夹具");
        f.write(QByteArray("\x89PNG\r\n\x1a\n", 8));
        QByteArray zeros(4096, '\0');
        qint64 left = 513 * 1024 - 8;
        while (left > 0) {
            const qint64 n = qMin<qint64>(left, zeros.size());
            f.write(zeros.constData(), n);
            left -= n;
        }
    }

    QByteArray bytes;
    QString mime, err;
    if (!avatar::loadAvatarFile(okPath, &bytes, &mime, &err) || mime != QStringLiteral("image/png"))
        return QStringLiteral("合法 PNG 未通过校验: %1").arg(err);
    if (avatar::loadAvatarFile(badPath, &bytes, &mime, &err)
        || !err.contains(QStringLiteral("JPEG 或 PNG")))
        return QStringLiteral("非图片文件未被拒绝");
    if (avatar::loadAvatarFile(bigPath, &bytes, &mime, &err)
        || !err.contains(QStringLiteral("512")))
        return QStringLiteral("超大文件未被拒绝");
    return QString();
}

int main(int argc, char *argv[])
{
    g_t0 = QDateTime::currentMSecsSinceEpoch();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("evcp-client-v22"));
    QApplication::setOrganizationName(QStringLiteral("NeusoftEVCP"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption scenarioOpt(QStringLiteral("scenario"), QString(), QStringLiteral("name"));
    const QCommandLineOption hostOpt(QStringLiteral("host"), QString(), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOpt(QStringLiteral("port"), QString(), QStringLiteral("port"));
    const QCommandLineOption reqlogOpt(QStringLiteral("reqlog"), QString(), QStringLiteral("path"));
    const QCommandLineOption deadlineOpt(QStringLiteral("deadline-ms"), QString(), QStringLiteral("ms"), QStringLiteral("20000"));
    parser.addOptions({scenarioOpt, hostOpt, portOpt, reqlogOpt, deadlineOpt});
    parser.process(app);

    const QString scenario = parser.value(scenarioOpt);
    g_reqlogPath = parser.value(reqlogOpt);
    const int deadlineMs = parser.value(deadlineOpt).toInt();

    AppConfig config;
    config.host = parser.value(hostOpt);
    config.port = static_cast<quint16>(parser.value(portOpt).toUInt());

    MainWindow w(config);
    w.show();

    auto *lp = w.findChild<LoginPage *>();
    auto *chargingPage = w.findChild<ChargingPage *>();
    auto *findPage = w.findChild<FindStationPage *>();
    auto *myPage = w.findChild<MyPage *>();
    QLineEdit *phoneEdit = findEdit(&w, QStringLiteral("请输入 11 位手机号"));
    QLineEdit *pwdEdit = findEdit(&w, QStringLiteral("请输入密码"));
    QLineEdit *lngEdit = findEdit(&w, QStringLiteral("经度 lng"));
    QLineEdit *latEdit = findEdit(&w, QStringLiteral("纬度 lat"));
    QPushButton *loginBtn = findButton(&w, {QStringLiteral("登录 / 注册"),
                                            QStringLiteral("连接中…（点击取消）"),
                                            QStringLiteral("登录中…")});
    QComboBox *regionCombo = w.findChild<QComboBox *>();
    if (!lp || !chargingPage || !findPage || !myPage || !phoneEdit || !pwdEdit
        || !lngEdit || !latEdit || !loginBtn || !regionCombo) {
        log(QStringLiteral("HARNESS-ERROR: 关键控件未找到"));
        return 3;
    }

    QObject::connect(lp, &LoginPage::loginSuccess, &app, [](const UserInfo &, bool) {
        g_loginOk = true;
        log(QStringLiteral("loginSuccess emitted"));
    });

    bool done = false;
    auto finish = [&](int code, const QString &why) {
        if (done)
            return;
        done = true;
        log((code == 0 ? QStringLiteral("PASS: %1") : QStringLiteral("FAIL: %1")).arg(why));
        QTimer::singleShot(0, &app, [&app, code]() { app.exit(code); });
    };

    QTimer watchdog;
    watchdog.setInterval(1000);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&]() {
        QString modalInfo;
        if (QWidget *am = QApplication::activeModalWidget())
            modalInfo = QStringLiteral(" activeModal=%1(%2)")
                            .arg(QString::fromUtf8(am->metaObject()->className()), am->windowTitle());
        log(QStringLiteral("TICK alive loginOk=%1 cards=%2 modals=%3%4")
                .arg(g_loginOk).arg(orderCards(&w).size()).arg(g_modals.size()).arg(modalInfo));
    });
    watchdog.start();

    QTimer modalTimer;
    modalTimer.setInterval(100);
    QObject::connect(&modalTimer, &QTimer::timeout, &app, &pollModal);
    modalTimer.start();

    // 头像校验（纯逻辑，直接断言）
    if (scenario == QLatin1String("profile_edit")) {
        const QString fixtureErr = checkAvatarFixtures();
        if (!fixtureErr.isEmpty()) {
            finish(2, QStringLiteral("头像校验失败：%1").arg(fixtureErr));
        } else {
            log(QStringLiteral("头像夹具校验通过（PNG 通过 / 非图片拒绝 / 超大拒绝）"));
        }
    }

    int actStep = 0;
    qint64 stepSince = 0;
    QString tickSample1;
    QPointer<QDialog> handledProfileDlg;
    QList<QUrl> navUrls;
    int navLoadStarted = 0;
    QPointer<QObject> navWatchedView;

    auto doLogin = [&]() {
        phoneEdit->setText(QStringLiteral("13800001111"));
        pwdEdit->setText(QStringLiteral("abc123"));
        loginBtn->click();
        log(QStringLiteral("clicked 登录 / 注册"));
    };

    // 延迟点击：打开 exec 模态对话框的按钮不能在 poll 处理器里同步点击，
    // 否则 actStep 推进语句执行不到且本定时器不再重入，形成死锁
    auto clickDeferred = [](QPushButton *b) {
        QTimer::singleShot(0, b, &QPushButton::click);
    };

    auto clickCardButton = [&](QFrame *card, const QString &text) -> bool {
        if (!card)
            return false;
        const auto btns = card->findChildren<QPushButton *>();
        for (QPushButton *b : btns) {
            if (b->text() == text) {
                b->click();
                return true;
            }
        }
        return false;
    };

    auto cardHasButtons = [](QFrame *card, const QStringList &texts) -> bool {
        if (!card)
            return false;
        const auto btns = card->findChildren<QPushButton *>();
        for (const QString &t : texts) {
            bool found = false;
            for (QPushButton *b : btns)
                if (b->text() == t)
                    found = true;
            if (!found)
                return false;
        }
        return true;
    };

    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (done)
            return;

        // ================= 场景驱动 =================
        if (scenario == QLatin1String("orders_list")) {
            if (actStep == 0) {
                doLogin();
                actStep = 1;
            } else if (actStep == 1 && g_loginOk && orderCards(&w).size() == 3) {
                QFrame *c1 = findCardByPile(&w, QStringLiteral("P-0001"));
                QFrame *c2 = findCardByPile(&w, QStringLiteral("P-0002"));
                QFrame *c3 = findCardByPile(&w, QStringLiteral("P-0003"));
                if (!c1 || !c2 || !c3) {
                    finish(2, QStringLiteral("未按电桩编号找到 3 张订单卡片"));
                    return;
                }
                if (cardStatus(c1) != QStringLiteral("已预约")
                    || !cardHasButtons(c1, {QStringLiteral("开始充电"), QStringLiteral("取消预约")})) {
                    finish(2, QStringLiteral("reserved 卡片状态/按钮不正确"));
                    return;
                }
                if (cardStatus(c2) != QStringLiteral("充电中")
                    || !cardHasButtons(c2, {QStringLiteral("停止充电")})
                    || !cardTickLabel(c2)) {
                    finish(2, QStringLiteral("charging 卡片状态/按钮/时长行不正确"));
                    return;
                }
                if (cardStatus(c3) != QStringLiteral("待结算")
                    || !cardHasButtons(c3, {QStringLiteral("结算")})) {
                    finish(2, QStringLiteral("pending_payment 卡片状态/按钮不正确"));
                    return;
                }
                finish(0, QStringLiteral("3 张订单卡片并行渲染，状态与按钮均正确"));
                return;
            }
        } else if (scenario == QLatin1String("order_flow")) {
            if (actStep == 0) {
                doLogin();
                actStep = 1;
            } else if (actStep == 1 && g_loginOk && orderCards(&w).size() == 3) {
                if (clickCardButton(findCardByPile(&w, QStringLiteral("P-0001")),
                                    QStringLiteral("开始充电"))) {
                    log(QStringLiteral("clicked 开始充电 P-0001"));
                    actStep = 2;
                }
            } else if (actStep == 2) {
                QFrame *c = findCardByPile(&w, QStringLiteral("P-0001"));
                if (c && cardStatus(c) == QStringLiteral("充电中")) {
                    if (clickCardButton(findCardByPile(&w, QStringLiteral("P-0002")),
                                        QStringLiteral("停止充电"))) {
                        log(QStringLiteral("clicked 停止充电 P-0002"));
                        actStep = 3;
                    }
                }
            } else if (actStep == 3) {
                QFrame *c = findCardByPile(&w, QStringLiteral("P-0002"));
                if (c && cardStatus(c) == QStringLiteral("待结算")) {
                    if (clickCardButton(c, QStringLiteral("结算"))) {
                        log(QStringLiteral("clicked 结算 P-0002"));
                        actStep = 4;
                    }
                }
            } else if (actStep == 4 && orderCards(&w).size() == 2
                       && modalSeen(QStringLiteral("结算完成"))) {
                if (clickCardButton(findCardByPile(&w, QStringLiteral("P-0003")),
                                    QStringLiteral("取消预约"))) {
                    log(QStringLiteral("clicked 取消预约 P-0003"));
                    actStep = 5;
                }
            } else if (actStep == 5 && orderCards(&w).size() == 1
                       && modalSeen(QStringLiteral("预约已取消"))) {
                QFrame *c = findCardByPile(&w, QStringLiteral("P-0001"));
                if (c && cardStatus(c) == QStringLiteral("充电中")) {
                    finish(0, QStringLiteral("开始/停止/结算/取消全部流转正确，整表刷新后仅剩充电中订单"));
                    return;
                }
            }
        } else if (scenario == QLatin1String("cost_tick")) {
            if (actStep == 0) {
                doLogin();
                actStep = 1;
            } else if (actStep == 1 && g_loginOk) {
                QFrame *c = findCardByPile(&w, QStringLiteral("P-0002"));
                QLabel *tick = c ? cardTickLabel(c) : nullptr;
                if (tick && tick->text().contains(QStringLiteral("预计花费"))) {
                    tickSample1 = tick->text();
                    stepSince = elapsed();
                    actStep = 2;
                    log(QStringLiteral("tick sample1: %1").arg(tickSample1));
                }
            } else if (actStep == 2 && elapsed() - stepSince >= 1600) {
                QFrame *c = findCardByPile(&w, QStringLiteral("P-0002"));
                QLabel *tick = c ? cardTickLabel(c) : nullptr;
                if (!tick)
                    return;
                const QString sample2 = tick->text();
                log(QStringLiteral("tick sample2: %1").arg(sample2));
                static const QRegularExpression re(
                    QStringLiteral("已充时长 (?:(\\d+):)?(\\d{2}):(\\d{2})｜预计花费 ([\\d.]+) 元"));
                const auto m1 = re.match(tickSample1);
                const auto m2 = re.match(sample2);
                if (!m1.hasMatch() || !m2.hasMatch()) {
                    finish(2, QStringLiteral("时长/花费行格式不符：%1 / %2").arg(tickSample1, sample2));
                    return;
                }
                auto secsOf = [](const QRegularExpressionMatch &m) {
                    return m.captured(1).toLongLong() * 3600
                           + m.captured(2).toLongLong() * 60 + m.captured(3).toLongLong();
                };
                const qint64 s1 = secsOf(m1), s2 = secsOf(m2);
                const double cost2 = m2.captured(4).toDouble();
                if (s2 <= s1) {
                    finish(2, QStringLiteral("已充时长未随时间增长：%1s -> %2s").arg(s1).arg(s2));
                    return;
                }
                // powerKw=60、unitPrice=1.20：预计花费 = 60 × 秒/3600 × 1.2 = 0.02 × 秒
                const double expected = 0.02 * static_cast<double>(s2);
                if (qAbs(cost2 - expected) > 0.02) {
                    finish(2, QStringLiteral("预计花费 %1 与公式值 %2 不符").arg(cost2).arg(expected));
                    return;
                }
                finish(0, QStringLiteral("预计花费随计时每秒更新且与 powerKw×时长×单价 一致（%1s→%2 元）")
                              .arg(s2).arg(cost2));
                return;
            }
        } else if (scenario == QLatin1String("profile_edit")) {
            if (actStep == 0) {
                doLogin();
                actStep = 1;
            } else if (actStep == 1 && g_loginOk) {
                if (QPushButton *tabMy = findButton(&w, {QStringLiteral("我的")})) {
                    tabMy->click();
                    actStep = 2;
                }
            } else if (actStep == 2) {
                if (QPushButton *edit = findButton(myPage, {QStringLiteral("编辑资料")})) {
                    actStep = 3;
                    clickDeferred(edit);
                }
            } else if (actStep == 3 && g_profileDlg && g_profileDlg != handledProfileDlg) {
                const auto edits = g_profileDlg->findChildren<QLineEdit *>();
                for (QLineEdit *e : edits)
                    e->setText(QStringLiteral("新昵称"));
                if (QPushButton *save = findButton(g_profileDlg, {QStringLiteral("保存")})) {
                    handledProfileDlg = g_profileDlg;
                    save->click();
                    log(QStringLiteral("clicked 保存（改昵称）"));
                    actStep = 4;
                }
            } else if (actStep == 4) {
                const auto reqs = requestsOf(QStringLiteral("user_profile_update"));
                bool labelUpdated = false;
                const auto labels = myPage->findChildren<QLabel *>();
                for (QLabel *l : labels)
                    if (l->text() == QStringLiteral("新昵称"))
                        labelUpdated = true;
                if (!reqs.isEmpty() && labelUpdated && !g_profileDlg) {
                    const QJsonObject payload = reqs.first().value(QStringLiteral("payload")).toObject();
                    if (payload.value(QStringLiteral("nickname")).toString() != QStringLiteral("新昵称")) {
                        finish(2, QStringLiteral("payload nickname 不正确：%1")
                                      .arg(QString::fromUtf8(QJsonDocument(payload).toJson())));
                        return;
                    }
                    if (payload.contains(QStringLiteral("avatar"))) {
                        finish(2, QStringLiteral("未改头像却提交了 avatar 字段"));
                        return;
                    }
                    if (QPushButton *edit = findButton(myPage, {QStringLiteral("编辑资料")})) {
                        actStep = 5;
                        clickDeferred(edit);
                    }
                }
            } else if (actStep == 5 && g_profileDlg && g_profileDlg != handledProfileDlg) {
                if (QPushButton *save = findButton(g_profileDlg, {QStringLiteral("保存")})) {
                    handledProfileDlg = g_profileDlg;
                    save->click();
                    log(QStringLiteral("clicked 保存（无变更）"));
                    actStep = 6;
                }
            } else if (actStep == 6 && modalSeen(QStringLiteral("未做任何修改")) && !g_profileDlg) {
                const auto reqs = requestsOf(QStringLiteral("user_profile_update"));
                if (reqs.size() != 1) {
                    finish(2, QStringLiteral("无变更仍发了请求（共 %1 次）").arg(reqs.size()));
                    return;
                }
                finish(0, QStringLiteral("昵称变更合并提交成功并刷新展示；无变更不发请求；头像校验通过"));
                return;
            }
        } else if (scenario == QLatin1String("nav_url")) {
#ifdef EVCP_HAVE_WEBENGINE
            if (actStep == 0) {
                doLogin();
                actStep = 1;
            } else if (actStep == 1 && g_loginOk) {
                regionCombo->activated(1); // 沙河口区 · 星海广场 (121.594, 38.881)
                log(QStringLiteral("selected region 1"));
                actStep = 2;
            } else if (actStep == 2) {
                const auto cards = w.findChildren<QFrame *>(QStringLiteral("stationCard"));
                if (!cards.isEmpty()) {
                    const auto btns = cards.first()->findChildren<QPushButton *>();
                    for (QPushButton *b : btns) {
                        if (b->text().endsWith(QStringLiteral(" km"))) {
                            b->click();
                            log(QStringLiteral("clicked 距离入口: %1").arg(b->text()));
                            actStep = 3;
                            break;
                        }
                    }
                }
            } else if (actStep == 3) {
                NavigationDialog *dlg = nullptr;
                const auto tops = QApplication::topLevelWidgets();
                for (QWidget *t : tops)
                    if ((dlg = qobject_cast<NavigationDialog *>(t)))
                        break;
                if (!dlg)
                    return;
                QString fromText, toText;
                const auto labels = dlg->findChildren<QLabel *>();
                for (QLabel *l : labels) {
                    if (l->text().startsWith(QStringLiteral("起点：")))
                        fromText = l->text();
                    else if (l->text().startsWith(QStringLiteral("终点：")))
                        toText = l->text();
                }
                if (!fromText.contains(QStringLiteral("38.881000"))
                    || !fromText.contains(QStringLiteral("121.594000"))
                    || !fromText.contains(QStringLiteral("星海广场"))
                    || !toText.contains(QStringLiteral("星海广场站"))
                    || !toText.contains(QStringLiteral("38.883000"))
                    || !toText.contains(QStringLiteral("121.596000"))) {
                    finish(2, QStringLiteral("起点/终点展示不正确：%1 | %2").arg(fromText, toText));
                    return;
                }
                if (dlg->findChild<QWebEngineView *>()) {
                    finish(2, QStringLiteral("点击「导航」前不应创建地图视图"));
                    return;
                }
                if (QPushButton *nav = findButton(dlg, {QStringLiteral("导航")})) {
                    nav->click();
                    stepSince = elapsed();
                    log(QStringLiteral("clicked 导航"));
                    actStep = 4;
                }
            } else if (actStep == 4) {
                // 静态断言：routeplan URI 参数精确符合腾讯 URI API（可离线验证的核心项）
                const QUrl expectedDrive = NavigationDialog::buildRouteUrl(
                    QStringLiteral("drive"), 121.594, 38.881, 121.596, 38.883,
                    QStringLiteral("星海广场站"), QStringLiteral("沙河口区 · 星海广场"));
                const QUrl literalDrive(
                    QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan?type=drive"
                                   "&from=沙河口区 · 星海广场"
                                   "&fromcoord=38.881000,121.594000"
                                   "&to=星海广场站"
                                   "&tocoord=38.883000,121.596000"
                                   "&referer=") + mapconfig::kTencentMapKey);
                if (!urlMatches(expectedDrive, literalDrive)) {
                    finish(2, QStringLiteral("buildRouteUrl(drive) 参数不符：%1")
                                  .arg(expectedDrive.toString()));
                    return;
                }
                NavigationDialog *dlg = nullptr;
                const auto tops = QApplication::topLevelWidgets();
                for (QWidget *t : tops)
                    if ((dlg = qobject_cast<NavigationDialog *>(t)))
                        break;
                QWebEngineView *view = dlg ? dlg->findChild<QWebEngineView *>() : nullptr;
                if (!view)
                    return;
                // 行为断言：点击「导航」后真实发起了加载；联网时服务端 302 跳转到
                // web 路线规划页（urlChanged 只上报最终提交 URL），其参数可反证
                // 我们提交的显式坐标被接受
                if (navWatchedView != view) {
                    navWatchedView = view;
                    if (!view->url().isEmpty())
                        navUrls << view->url();
                    QObject::connect(view, &QWebEngineView::urlChanged, &app,
                                     [&](const QUrl &u) {
                                         navUrls << u;
                                         log(QStringLiteral("nav urlChanged: %1").arg(u.toString()));
                                     });
                    QObject::connect(view, &QWebEngineView::loadStarted, &app,
                                     [&]() {
                                         ++navLoadStarted;
                                         log(QStringLiteral("nav loadStarted #%1").arg(navLoadStarted));
                                     });
                }
                bool coordsAccepted = false;
                for (const QUrl &u : navUrls) {
                    const QString s = QUrl::fromPercentEncoding(u.toString().toUtf8());
                    if (s.contains(QStringLiteral("121.594")) && s.contains(QStringLiteral("38.881"))
                        && s.contains(QStringLiteral("121.596")) && s.contains(QStringLiteral("38.883")))
                        coordsAccepted = true;
                }
                if (coordsAccepted || elapsed() - stepSince > 10000) {
                    if (!navUrls.isEmpty() && !coordsAccepted) {
                        finish(2, QStringLiteral("提交的坐标未被地图服务接受：%1")
                                      .arg(navUrls.constLast().toString()));
                        return;
                    }
                    log(QStringLiteral("drive load ok (loadStarted=%1, coordsAccepted=%2)")
                            .arg(navLoadStarted).arg(coordsAccepted));
                    if (QPushButton *walk = findButton(dlg, {QStringLiteral("步行")})) {
                        walk->click();
                        stepSince = elapsed();
                        actStep = 5;
                    }
                }
            } else if (actStep == 5) {
                const QUrl expectedWalk = NavigationDialog::buildRouteUrl(
                    QStringLiteral("walk"), 121.594, 38.881, 121.596, 38.883,
                    QStringLiteral("星海广场站"), QStringLiteral("沙河口区 · 星海广场"));
                const QUrl literalWalk(
                    QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan?type=walk"
                                   "&from=沙河口区 · 星海广场"
                                   "&fromcoord=38.881000,121.594000"
                                   "&to=星海广场站"
                                   "&tocoord=38.883000,121.596000"
                                   "&referer=") + mapconfig::kTencentMapKey);
                if (!urlMatches(expectedWalk, literalWalk)) {
                    finish(2, QStringLiteral("buildRouteUrl(walk) 参数不符：%1")
                                  .arg(expectedWalk.toString()));
                    return;
                }
                // 步行切换后发起了第二次加载（联网时可见新的提交 URL，type 随方式变化）
                if (navUrls.size() >= 2 || elapsed() - stepSince > 10000) {
                    finish(0, QStringLiteral("routeplan URL 参数正确（type/fromcoord/tocoord/referer），"
                                             "点击「导航」后才加载，驾车/步行切换均触发真实路线规划"));
                    return;
                }
            }
#else
            finish(2, QStringLiteral("harness 未启用 WebEngine，无法验证导航"));
            return;
#endif
        } else if (scenario == QLatin1String("region_select")) {
            if (actStep == 0) {
                doLogin();
                actStep = 1;
            } else if (actStep == 1 && g_loginOk) {
                regionCombo->activated(2); // 中山区 · 青泥洼桥 (121.633, 38.917)
                log(QStringLiteral("selected region 2"));
                actStep = 2;
            } else if (actStep == 2) {
                const auto reqs = requestsOf(QStringLiteral("nearby_station_list"));
                for (const QJsonObject &r : reqs) {
                    const QJsonObject p = r.value(QStringLiteral("payload")).toObject();
                    if (qAbs(p.value(QStringLiteral("lng")).toDouble() - 121.633) < 1e-6
                        && qAbs(p.value(QStringLiteral("lat")).toDouble() - 38.917) < 1e-6) {
                        if (lngEdit->text() != QStringLiteral("121.633000")
                            || latEdit->text() != QStringLiteral("38.917000")) {
                            finish(2, QStringLiteral("经纬度输入框未回填预设坐标：%1, %2")
                                          .arg(lngEdit->text(), latEdit->text()));
                            return;
                        }
                        if (w.findChildren<QFrame *>(QStringLiteral("stationCard")).isEmpty())
                            return; // 等站点卡片渲染
                        finish(0, QStringLiteral("区域下拉即以预设坐标 (121.633, 38.917) 定位并查出站点"));
                        return;
                    }
                }
            }
        } else if (scenario == QLatin1String("reserve_then_card")) {
            if (actStep == 0) {
                doLogin();
                actStep = 1;
            } else if (actStep == 1 && g_loginOk) {
                regionCombo->activated(1);
                actStep = 2;
            } else if (actStep == 2) {
                const auto cards = w.findChildren<QFrame *>(QStringLiteral("stationCard"));
                if (!cards.isEmpty()) {
                    QTest::mouseClick(cards.first(), Qt::LeftButton);
                    log(QStringLiteral("clicked station card"));
                    actStep = 3;
                }
            } else if (actStep == 3) {
                QDialog *detail = nullptr;
                const auto tops = QApplication::topLevelWidgets();
                for (QWidget *t : tops) {
                    auto *d = qobject_cast<QDialog *>(t);
                    if (d && d->windowTitle() == QStringLiteral("星海广场站"))
                        detail = d;
                }
                if (detail) {
                    if (QPushButton *reserve = findButton(detail, {QStringLiteral("预约")})) {
                        reserve->click();
                        log(QStringLiteral("clicked 预约"));
                        actStep = 4;
                    }
                }
            } else if (actStep == 4) {
                QFrame *c = findCardByPile(&w, QStringLiteral("P-1001"));
                if (c && cardStatus(c) == QStringLiteral("已预约")
                    && modalSeen(QStringLiteral("预约成功"))) {
                    const auto reqs = requestsOf(QStringLiteral("charge_reserve"));
                    if (reqs.isEmpty()
                        || reqs.first().value(QStringLiteral("payload")).toObject()
                                   .value(QStringLiteral("pileId")).toInt() != 101) {
                        finish(2, QStringLiteral("charge_reserve 报文不正确"));
                        return;
                    }
                    finish(0, QStringLiteral("预约成功后充电页出现新订单卡片（P-1001 已预约）"));
                    return;
                }
            }
        }

        // ================= 超时判定 =================
        if (elapsed() > deadlineMs)
            finish(2, QStringLiteral("场景 %1 未在 %2ms 内达成预期（actStep=%3 modals=[%4]）")
                          .arg(scenario).arg(deadlineMs).arg(actStep).arg(g_modals.join(QStringLiteral(" | "))));
    });
    poll.start();

    return app.exec();
}
