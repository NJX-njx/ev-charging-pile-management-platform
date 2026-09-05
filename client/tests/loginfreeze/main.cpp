// 登录流程卡死回归 harness：offscreen 下链接真实 MainWindow/LoginPage/SocketClient，
// 脚本化点击「登录 / 注册」「获取验证码」，并用看门狗验证事件循环存活、
// 用按钮可用态与模态框文本验证 UI 是否从「先连接再发送」流程中恢复。
//
// 退出码：0=PASS，2=FAIL/卡死，3=harness 自身错误。
// 场景（--scenario）：
//   refused          服务端不在线（连接被拒）：期望报错弹窗 + 按钮恢复
//   timeout          连接黑洞地址：期望连接超时弹窗 + 按钮恢复
//   login            正常登录成功（--dialog-action set|skip 处理设密引导弹窗）
//   login_err        服务端在线但返回业务错误（--expect-modal 指定期望文案）
//   smscode_refused  未连接时点击「获取验证码」：期望报错 + 按钮恢复
//   smscode_login    获取验证码并用其登录成功
//   cancel_connect   连接等待期（黑洞地址）再次点击按钮取消连接：按钮恢复且无弹窗
//   reconnect        登录后服务端被杀：断线横幅出现；服务端恢复后自动重连重登，横幅消失

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

#include "mainwindow.h"
#include "model/appconfig.h"
#include "pages/loginpage.h"

static qint64 g_t0 = 0;
static qint64 elapsed() { return QDateTime::currentMSecsSinceEpoch() - g_t0; }
static void log(const QString &s)
{
    qInfo().noquote() << QStringLiteral("[%1ms] %2").arg(elapsed(), 6).arg(s);
}

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

static QStringList g_modals;
static bool g_loginOk = false;
static bool g_dialogSeen = false;
static bool g_pwdSetOk = false;
static QString g_smsCode;
static QString g_dialogAction; // "set" | "skip" | ""（不应出现设密弹窗）
static QPointer<QDialog> g_handledDialog;

static bool modalSeen(const QString &substr)
{
    for (const QString &t : g_modals)
        if (t.contains(substr))
            return true;
    return false;
}

// 自动处理模态框：QMessageBox 记录文本后关闭；PasswordDialog 按 g_dialogAction 填密码或跳过。
static void pollModal()
{
    QWidget *m = QApplication::activeModalWidget();
    if (!m)
        return;
    if (auto *box = qobject_cast<QMessageBox *>(m)) {
        const QString text = box->text();
        log(QStringLiteral("MODAL[%1]: %2").arg(box->windowTitle(), text));
        g_modals << text;
        if (text.contains(QStringLiteral("密码设置成功")))
            g_pwdSetOk = true;
        if (text.contains(QStringLiteral("验证码"))) {
            const QRegularExpression re(QStringLiteral("(\\d{6})"));
            const auto match = re.match(text);
            if (match.hasMatch())
                g_smsCode = match.captured(1);
        }
        box->done(0);
        return;
    }
    auto *dlg = qobject_cast<QDialog *>(m);
    if (dlg && dlg != g_handledDialog) {
        g_dialogSeen = true;
        g_handledDialog = dlg;
        log(QStringLiteral("DIALOG: %1 (action=%2)").arg(dlg->windowTitle(), g_dialogAction));
        if (g_dialogAction == QLatin1String("set")) {
            const auto edits = dlg->findChildren<QLineEdit *>();
            for (QLineEdit *e : edits)
                e->setText(QStringLiteral("abc123"));
            if (auto *ok = findButton(dlg, {QStringLiteral("确定")}))
                ok->click();
            else
                dlg->reject();
        } else {
            if (auto *skip = findButton(dlg, {QStringLiteral("暂不设置"), QStringLiteral("取消")}))
                skip->click();
            else
                dlg->reject();
        }
    }
}

int main(int argc, char *argv[])
{
    g_t0 = QDateTime::currentMSecsSinceEpoch();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("evcp-client"));
    QApplication::setOrganizationName(QStringLiteral("NeusoftEVCP"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption scenarioOpt(QStringLiteral("scenario"), QString(), QStringLiteral("name"));
    const QCommandLineOption hostOpt(QStringLiteral("host"), QString(), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOpt(QStringLiteral("port"), QString(), QStringLiteral("port"));
    const QCommandLineOption phoneOpt(QStringLiteral("phone"), QString(), QStringLiteral("phone"), QStringLiteral("13800001111"));
    const QCommandLineOption passwordOpt(QStringLiteral("password"), QString(), QStringLiteral("pwd"), QStringLiteral("abc123"));
    const QCommandLineOption expectModalOpt(QStringLiteral("expect-modal"), QString(), QStringLiteral("substr"));
    const QCommandLineOption dialogActionOpt(QStringLiteral("dialog-action"), QString(), QStringLiteral("set|skip"));
    const QCommandLineOption deadlineOpt(QStringLiteral("deadline-ms"), QString(), QStringLiteral("ms"), QStringLiteral("10000"));
    parser.addOptions({scenarioOpt, hostOpt, portOpt, phoneOpt, passwordOpt, expectModalOpt, dialogActionOpt, deadlineOpt});
    parser.process(app);

    const QString scenario = parser.value(scenarioOpt);
    const QString phone = parser.value(phoneOpt);
    const QString password = parser.value(passwordOpt);
    const QString expectModal = parser.value(expectModalOpt);
    g_dialogAction = parser.value(dialogActionOpt);
    const int deadlineMs = parser.value(deadlineOpt).toInt();

    AppConfig config;
    config.host = parser.value(hostOpt);
    config.port = static_cast<quint16>(parser.value(portOpt).toUInt());

    MainWindow w(config);
    w.show();

    auto *lp = w.findChild<LoginPage *>();
    QLineEdit *phoneEdit = findEdit(&w, QStringLiteral("请输入 11 位手机号"));
    QLineEdit *pwdEdit = findEdit(&w, QStringLiteral("请输入密码"));
    QLineEdit *codeEdit = findEdit(&w, QStringLiteral("6 位短信验证码"));
    QPushButton *loginBtn = findButton(&w, {QStringLiteral("登录 / 注册"),
                                            QStringLiteral("连接中…（点击取消）"),
                                            QStringLiteral("登录中…")});
    QPushButton *smsBtn = findButton(&w, {QStringLiteral("获取验证码")});
    QPushButton *codeModeBtn = findButton(&w, {QStringLiteral("验证码登录")});
    auto *banner = w.findChild<QLabel *>(QStringLiteral("banner"));
    if (!lp || !phoneEdit || !pwdEdit || !codeEdit || !loginBtn || !smsBtn || !codeModeBtn || !banner) {
        log(QStringLiteral("HARNESS-ERROR: 关键控件未找到 lp=%1 phone=%2 pwd=%3 code=%4 login=%5 sms=%6 mode=%7")
                .arg(lp != nullptr).arg(phoneEdit != nullptr).arg(pwdEdit != nullptr)
                .arg(codeEdit != nullptr).arg(loginBtn != nullptr).arg(smsBtn != nullptr)
                .arg(codeModeBtn != nullptr));
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

    // 看门狗：周期性输出证明事件循环存活（若无 TICK 输出且进程被外部 timeout 杀死 = 事件循环硬卡死）
    QTimer watchdog;
    watchdog.setInterval(1000);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&]() {
        log(QStringLiteral("TICK alive loginBtnEnabled=%1 smsBtnEnabled=%2 loginOk=%3 modals=%4")
                .arg(loginBtn->isEnabled()).arg(smsBtn->isEnabled()).arg(g_loginOk).arg(g_modals.size()));
    });
    watchdog.start();

    QTimer modalTimer;
    modalTimer.setInterval(100);
    QObject::connect(&modalTimer, &QTimer::timeout, &app, &pollModal);
    modalTimer.start();

    int actStep = 0;
    qint64 cancelRecoveredAt = 0;
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (done)
            return;

        // 场景动作驱动
        if (scenario == QLatin1String("smscode_login")) {
            if (actStep == 0) {
                codeModeBtn->click();
                phoneEdit->setText(phone);
                smsBtn->click();
                log(QStringLiteral("clicked 获取验证码"));
                actStep = 1;
            } else if (actStep == 1 && !g_smsCode.isEmpty()) {
                codeEdit->setText(g_smsCode);
                loginBtn->click();
                log(QStringLiteral("filled code %1, clicked login").arg(g_smsCode));
                actStep = 2;
            }
        } else if (scenario == QLatin1String("smscode_refused")) {
            if (actStep == 0) {
                codeModeBtn->click();
                phoneEdit->setText(phone);
                smsBtn->click();
                log(QStringLiteral("clicked 获取验证码 (server offline)"));
                actStep = 1;
            }
        } else if (scenario == QLatin1String("cancel_connect")) {
            if (actStep == 0) {
                phoneEdit->setText(phone);
                pwdEdit->setText(password);
                loginBtn->click();
                log(QStringLiteral("clicked 登录 / 注册 (blackhole host)"));
                actStep = 1;
            } else if (actStep == 1 && elapsed() >= 600) {
                // 连接等待期再次点击 = 取消连接
                loginBtn->click();
                log(QStringLiteral("clicked cancel during 连接中"));
                actStep = 2;
            }
        } else if (actStep == 0) { // refused / timeout / login / login_err / reconnect
            phoneEdit->setText(phone);
            pwdEdit->setText(password);
            loginBtn->click();
            log(QStringLiteral("clicked 登录 / 注册"));
            actStep = 1;
        }

        // 意外弹窗检查
        if (g_dialogAction.isEmpty() && g_dialogSeen) {
            finish(2, QStringLiteral("出现未预期的设密引导弹窗"));
            return;
        }

        const bool dialogDone = g_dialogAction == QLatin1String("set") ? g_pwdSetOk
                                : g_dialogAction == QLatin1String("skip") ? g_dialogSeen
                                                                          : !g_dialogSeen;
        // PASS 判定
        if (scenario == QLatin1String("login") || scenario == QLatin1String("smscode_login")) {
            if (g_loginOk && dialogDone) {
                finish(0, QStringLiteral("登录成功且设密引导处理完成 (dialogAction=%1)").arg(g_dialogAction));
                return;
            }
        } else if (scenario == QLatin1String("smscode_refused")) {
            if (modalSeen(expectModal) && smsBtn->isEnabled()) {
                finish(0, QStringLiteral("获取验证码失败有明确报错且按钮已恢复"));
                return;
            }
        } else if (scenario == QLatin1String("cancel_connect")) {
            if (actStep == 2) {
                if (modalSeen(QStringLiteral("无法连接服务器"))) {
                    finish(2, QStringLiteral("取消连接后仍弹出错误提示"));
                    return;
                }
                if (loginBtn->isEnabled()
                    && loginBtn->text() == QStringLiteral("登录 / 注册")) {
                    if (cancelRecoveredAt == 0)
                        cancelRecoveredAt = elapsed();
                    if (elapsed() - cancelRecoveredAt >= 2500) {
                        finish(0, QStringLiteral("连接等待期可点击取消，取消后按钮恢复且 2.5s 内无错误弹窗"));
                        return;
                    }
                }
            }
        } else if (scenario == QLatin1String("reconnect")) {
            if (actStep == 1 && g_loginOk) {
                actStep = 2;
                log(QStringLiteral("logged in, waiting for server kill"));
            } else if (actStep == 2 && banner->isVisible()) {
                actStep = 3;
                log(QStringLiteral("断线横幅已出现: %1").arg(banner->text()));
            } else if (actStep == 3 && !banner->isVisible()) {
                if (!lp->isVisible()) {
                    finish(0, QStringLiteral("服务端恢复后自动重连并重登成功，断线横幅消失"));
                    return;
                }
                // lp 可见 = 重登失败被踢回登录页，留待 deadline 判 FAIL
            }
        } else { // refused / timeout / login_err
            if (g_loginOk && scenario == QLatin1String("login_err")) {
                finish(2, QStringLiteral("期望登录失败但实际成功了"));
                return;
            }
            if (modalSeen(expectModal) && loginBtn->isEnabled()) {
                finish(0, QStringLiteral("出现预期报错「%1」且登录按钮已恢复").arg(expectModal));
                return;
            }
        }

        // 超时/卡死判定
        if (elapsed() > deadlineMs) {
            const QString state = QStringLiteral("loginBtnEnabled=%1 smsBtnEnabled=%2 loginOk=%3 dialogSeen=%4 modals=[%5]")
                                      .arg(loginBtn->isEnabled()).arg(smsBtn->isEnabled())
                                      .arg(g_loginOk).arg(g_dialogSeen).arg(g_modals.join(QStringLiteral(" | ")));
            const bool busy = !loginBtn->isEnabled()
                              || (scenario.startsWith(QLatin1String("smscode")) && !smsBtn->isEnabled());
            if (busy)
                finish(2, QStringLiteral("FREEZE: 点击后按钮持续禁用 %1ms 且无任何错误反馈（界面卡死）。%2")
                              .arg(deadlineMs).arg(state));
            else
                finish(2, QStringLiteral("场景未在 %1ms 内达成预期。%2").arg(deadlineMs).arg(state));
        }
    });
    poll.start();

    return app.exec();
}
