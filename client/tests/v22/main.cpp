// 协议 v2.2 适配验证 harness（offscreen）：链接真实 MainWindow/各页面/SocketClient，
// 配合状态化假服务端（fakeserver.py）脚本化驱动 UI 并断言行为。
//
// 退出码：0=PASS，2=FAIL，3=harness 自身错误。
// 场景（--scenario，对应 fakeserver 的 CASE 见 run_scenarios.sh）：
//   orders_list        多订单列表：3 张订单卡片、各自状态文案与操作按钮
//   order_flow         状态流转：开始/停止/结算/取消后整表刷新、卡片消失
//   cost_tick          charging 卡片「已充时长｜预计花费」每秒跳动且数值与公式一致
//   profile_edit       编辑资料：昵称合并提交、无变更不发请求、头像文件校验
//   nav_url            导航对话框：默认定位（北京市中心）直接导航，起点/终点展示、
//                      点「导航」后才加载 routeplan URL、驾车/步行切换
//   region_select      「全部区域（默认）」进入即以北京市中心发 nearby_station_list；
//                      切换到房山区（良乡）后以其区中心坐标定位
//   reserve_then_card  预约成功后充电页出现新订单卡片
//   pwd_toggle         密码「显示/隐藏」切换改变 echoMode（登录页/忘记密码/设改密）
//   ime_hints          各输入框 inputMethodHints：地址/昵称不限（中文 IME），
//                      手机号/验证码/端口为数字、经纬度/金额为数值

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
#include <QToolButton>
#include <QUrlQuery>

#include "mainwindow.h"
#include "map/tencentmapkey.h"
#include "model/appconfig.h"
#include "net/socketclient.h"
#include "pages/chargingpage.h"
#include "pages/findstationpage.h"
#include "pages/loginpage.h"
#include "pages/mypage.h"
#include "pages/navigationdialog.h"
#include "pages/passworddialog.h"
#include "pages/profileeditdialog.h"
#include "pages/rechargedialog.h"
#include "pages/resetpassworddialog.h"
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

// 密码「显示/隐藏」切换按钮（ui::withPasswordToggle 生成）：只查直接子控件，
// 避免把同行其他输入框的切换按钮误算到未包裹的输入框头上
static QToolButton *passwordToggleOf(QLineEdit *edit)
{
    if (!edit || !edit->parentWidget())
        return nullptr;
    const auto children = edit->parentWidget()->children();
    for (QObject *c : children) {
        auto *t = qobject_cast<QToolButton *>(c);
        if (t && t->objectName() == QStringLiteral("passwordToggle"))
            return t;
    }
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
                // 不选区域：默认定位「全部区域（默认）」= 北京市中心 (116.397, 39.909)，
                // 进入找站页自动查询，站点卡片渲染即代表默认定位已生效
                log(QStringLiteral("logged in, waiting default-region stations"));
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
                if (!fromText.contains(QStringLiteral("39.909000"))
                    || !fromText.contains(QStringLiteral("116.397000"))
                    || !fromText.contains(QStringLiteral("全部区域"))
                    || !toText.contains(QStringLiteral("星海广场站"))
                    || !toText.contains(QStringLiteral("38.883000"))
                    || !toText.contains(QStringLiteral("121.596000"))) {
                    finish(2, QStringLiteral("起点/终点展示不正确：%1 | %2").arg(fromText, toText));
                    return;
                }
                // 尺寸断言：对话框近全屏利用主窗口区域（旧 360×560 曾致路线页显示不全）
                if (dlg->width() < w.width() * 9 / 10
                    || dlg->height() < w.height() * 17 / 20) {
                    finish(2, QStringLiteral("导航对话框未充分利用主窗口：dlg %1×%2，主窗 %3×%4")
                                  .arg(dlg->width()).arg(dlg->height())
                                  .arg(w.width()).arg(w.height()));
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
                    QStringLiteral("drive"), 116.397, 39.909, 121.596, 38.883,
                    QStringLiteral("星海广场站"), QStringLiteral("全部区域（默认）"));
                const QUrl literalDrive(
                    QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan?type=drive"
                                   "&from=全部区域（默认）"
                                   "&fromcoord=39.909000,116.397000"
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
                // 视图断言：QWebEngineView 填满对话框内容区（横向≈对话框内宽，纵向占主体）
                if (view->width() > 0
                    && (view->width() < dlg->width() - 40
                        || view->height() < dlg->height() * 3 / 5)) {
                    finish(2, QStringLiteral("地图视图未填满内容区：view %1×%2，dlg %3×%4")
                                  .arg(view->width()).arg(view->height())
                                  .arg(dlg->width()).arg(dlg->height()));
                    return;
                }
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
                    if (s.contains(QStringLiteral("116.397")) && s.contains(QStringLiteral("39.909"))
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
                    QStringLiteral("walk"), 116.397, 39.909, 121.596, 38.883,
                    QStringLiteral("星海广场站"), QStringLiteral("全部区域（默认）"));
                const QUrl literalWalk(
                    QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan?type=walk"
                                   "&from=全部区域（默认）"
                                   "&fromcoord=39.909000,116.397000"
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
                                             "默认定位（北京市中心）直接导航，驾车/步行切换均触发真实路线规划"));
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
                // 断言下拉结构：第一项「全部区域（默认）」+ 6 个北京预设区域
                if (regionCombo->count() != 7
                    || regionCombo->itemText(0) != QStringLiteral("全部区域（默认）")
                    || regionCombo->itemText(1) != QStringLiteral("房山区（良乡）")
                    || regionCombo->itemText(6) != QStringLiteral("西城区")) {
                    finish(2, QStringLiteral("区域下拉项不正确：共 %1 项，首项「%2」")
                                  .arg(regionCombo->count()).arg(regionCombo->itemText(0)));
                    return;
                }
                // 「全部区域（默认）」：进入找站页即自动以北京市中心 (116.397, 39.909) 查询
                const auto reqs = requestsOf(QStringLiteral("nearby_station_list"));
                bool defaultFired = false;
                for (const QJsonObject &r : reqs) {
                    const QJsonObject p = r.value(QStringLiteral("payload")).toObject();
                    if (qAbs(p.value(QStringLiteral("lng")).toDouble() - 116.397) < 1e-6
                        && qAbs(p.value(QStringLiteral("lat")).toDouble() - 39.909) < 1e-6)
                        defaultFired = true;
                }
                if (!defaultFired)
                    return; // 等默认查询发出
                if (lngEdit->text() != QStringLiteral("116.397000")
                    || latEdit->text() != QStringLiteral("39.909000")) {
                    finish(2, QStringLiteral("默认定位未回填北京市中心坐标：%1, %2")
                                  .arg(lngEdit->text(), latEdit->text()));
                    return;
                }
                if (w.findChildren<QFrame *>(QStringLiteral("stationCard")).isEmpty())
                    return; // 等默认站点卡片渲染
                log(QStringLiteral("默认定位（北京市中心）已自动查询并渲染站点"));
                regionCombo->activated(1); // 房山区（良乡） (116.14, 39.74)
                log(QStringLiteral("selected region 1 房山区（良乡）"));
                actStep = 2;
            } else if (actStep == 2) {
                const auto reqs = requestsOf(QStringLiteral("nearby_station_list"));
                for (const QJsonObject &r : reqs) {
                    const QJsonObject p = r.value(QStringLiteral("payload")).toObject();
                    if (qAbs(p.value(QStringLiteral("lng")).toDouble() - 116.14) < 1e-6
                        && qAbs(p.value(QStringLiteral("lat")).toDouble() - 39.74) < 1e-6) {
                        if (lngEdit->text() != QStringLiteral("116.140000")
                            || latEdit->text() != QStringLiteral("39.740000")) {
                            finish(2, QStringLiteral("经纬度输入框未回填预设坐标：%1, %2")
                                          .arg(lngEdit->text(), latEdit->text()));
                            return;
                        }
                        finish(0, QStringLiteral("「全部区域（默认）」进入即以北京市中心 (116.397, 39.909) 查出站点；"
                                                 "切换房山区（良乡）后以其区中心 (116.14, 39.74) 定位"));
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
        } else if (scenario == QLatin1String("pwd_toggle")) {
            // 全程离线：只检查控件结构与 echoMode 切换，不发任何请求
            if (actStep == 0) {
                // 登录页密码框
                QToolButton *t = passwordToggleOf(pwdEdit);
                if (!t || pwdEdit->echoMode() != QLineEdit::Password
                    || t->text() != QStringLiteral("显示")) {
                    finish(2, QStringLiteral("登录页密码框缺少「显示」切换或初始回显不是 Password"));
                    return;
                }
                t->click();
                if (pwdEdit->echoMode() != QLineEdit::Normal
                    || t->text() != QStringLiteral("隐藏")) {
                    finish(2, QStringLiteral("登录页点击「显示」后 echoMode 未变为 Normal"));
                    return;
                }
                t->click();
                if (pwdEdit->echoMode() != QLineEdit::Password
                    || t->text() != QStringLiteral("显示")) {
                    finish(2, QStringLiteral("登录页再次点击后 echoMode 未恢复 Password"));
                    return;
                }
                log(QStringLiteral("登录页密码「显示/隐藏」切换 OK"));
                actStep = 1;
            } else if (actStep == 1) {
                // 忘记密码对话框：新密码框有切换，确认框没有
                SocketClient dummy;
                ResetPasswordDialog dlg(&dummy);
                QLineEdit *newEdit = findEdit(&dlg, QStringLiteral("新密码（6-20 位，不含空白字符）"));
                QLineEdit *confirmEdit = findEdit(&dlg, QStringLiteral("再次输入新密码"));
                QToolButton *t = passwordToggleOf(newEdit);
                if (!newEdit || !confirmEdit || !t) {
                    finish(2, QStringLiteral("忘记密码对话框新密码框缺少「显示」切换"));
                    return;
                }
                if (passwordToggleOf(confirmEdit)) {
                    finish(2, QStringLiteral("忘记密码对话框确认框不应有切换按钮"));
                    return;
                }
                if (newEdit->echoMode() != QLineEdit::Password) {
                    finish(2, QStringLiteral("忘记密码新密码框初始回显不是 Password"));
                    return;
                }
                t->click();
                if (newEdit->echoMode() != QLineEdit::Normal) {
                    finish(2, QStringLiteral("忘记密码点击「显示」后 echoMode 未变为 Normal"));
                    return;
                }
                log(QStringLiteral("忘记密码对话框新密码切换 OK"));
                actStep = 2;
            } else if (actStep == 2) {
                // 设密/改密对话框：新密码框有切换；改密时原密码框、确认框均没有
                SocketClient dummy;
                PasswordDialog changeDlg(&dummy, true, false);
                QLineEdit *oldEdit = findEdit(&changeDlg, QStringLiteral("请输入原密码"));
                QLineEdit *newEdit = findEdit(&changeDlg, QStringLiteral("新密码（6-20 位，不含空白字符）"));
                QLineEdit *confirmEdit = findEdit(&changeDlg, QStringLiteral("再次输入新密码"));
                if (!oldEdit || !newEdit || !confirmEdit || !passwordToggleOf(newEdit)
                    || passwordToggleOf(oldEdit) || passwordToggleOf(confirmEdit)) {
                    finish(2, QStringLiteral("修改密码对话框仅新密码框应有「显示」切换"));
                    return;
                }
                PasswordDialog setDlg(&dummy, false, true);
                QLineEdit *firstNewEdit = findEdit(&setDlg, QStringLiteral("新密码（6-20 位，不含空白字符）"));
                if (!firstNewEdit || !passwordToggleOf(firstNewEdit)) {
                    finish(2, QStringLiteral("首次设密对话框新密码框缺少「显示」切换"));
                    return;
                }
                finish(0, QStringLiteral("密码「显示/隐藏」切换覆盖登录页/忘记密码/设密/改密，echoMode 随点击切换"));
                return;
            }
        } else if (scenario == QLatin1String("ime_hints")) {
            // 全程离线：代码层确认各输入框 inputMethodHints（IME 合成行为需真机确认）
            if (actStep == 0) {
                QLineEdit *addrEdit = findEdit(findPage, QStringLiteral("输入区域或地址，如：海淀区中关村"));
                QLineEdit *codeEdit = findEdit(&w, QStringLiteral("6 位短信验证码"));
                QLineEdit *portEdit = findEdit(&w, QStringLiteral("端口"));
                if (!addrEdit || !codeEdit || !portEdit) {
                    finish(2, QStringLiteral("HARNESS-ERROR: 关键输入框未找到"));
                    return;
                }
                // 地址输入框：输入法不受限（支持中文 IME）
                if (addrEdit->inputMethodHints() != Qt::ImhNone) {
                    finish(2, QStringLiteral("地址输入框输入法受限：hints=%1")
                                  .arg(static_cast<int>(addrEdit->inputMethodHints())));
                    return;
                }
                // 手机号/验证码/端口：仅数字；经纬度：数值
                if (phoneEdit->inputMethodHints() != Qt::ImhDigitsOnly
                    || codeEdit->inputMethodHints() != Qt::ImhDigitsOnly
                    || portEdit->inputMethodHints() != Qt::ImhDigitsOnly) {
                    finish(2, QStringLiteral("手机号/验证码/端口框未限制为数字输入"));
                    return;
                }
                if (lngEdit->inputMethodHints() != Qt::ImhFormattedNumbersOnly
                    || latEdit->inputMethodHints() != Qt::ImhFormattedNumbersOnly) {
                    finish(2, QStringLiteral("经纬度框未限制为数值输入"));
                    return;
                }
                // 资料编辑昵称框：输入法不受限（支持中文 IME）
                SocketClient dummy;
                ProfileEditDialog profileDlg(&dummy, UserInfo{});
                const auto profileEdits = profileDlg.findChildren<QLineEdit *>();
                if (profileEdits.size() != 1
                    || profileEdits.first()->inputMethodHints() != Qt::ImhNone) {
                    finish(2, QStringLiteral("昵称输入框输入法受限"));
                    return;
                }
                // 充值金额框：数值
                RechargeDialog rechargeDlg(&dummy);
                QLineEdit *amountEdit = rechargeDlg.findChild<QLineEdit *>();
                if (!amountEdit || amountEdit->inputMethodHints() != Qt::ImhFormattedNumbersOnly) {
                    finish(2, QStringLiteral("充值金额框未限制为数值输入"));
                    return;
                }
                finish(0, QStringLiteral("地址/昵称输入法不受限（ImhNone，支持中文 IME）；"
                                         "手机号/验证码/端口仅数字，经纬度/金额为数值"));
                return;
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
