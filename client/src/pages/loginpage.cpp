#include "loginpage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "model/appconfig.h"
#include "net/socketclient.h"
#include "pages/resetpassworddialog.h"
#include "ui/passwordtoggle.h"
#include "ui/smscodebutton.h"

LoginPage::LoginPage(SocketClient *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 48, 24, 24);
    root->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("电动汽车充电桩"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    title->setAlignment(Qt::AlignHCenter);
    auto *subtitle = new QLabel(QStringLiteral("手机号登录，未注册将自动创建账号"), this);
    subtitle->setObjectName(QStringLiteral("hint"));
    subtitle->setAlignment(Qt::AlignHCenter);
    root->addWidget(title);
    root->addWidget(subtitle);
    root->addSpacing(16);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);

    auto *modeRow = new QHBoxLayout();
    modeRow->setSpacing(8);
    const QStringList modeNames{QStringLiteral("密码登录"), QStringLiteral("验证码登录")};
    for (int i = 0; i < modeNames.size(); ++i) {
        auto *btn = new QPushButton(modeNames.at(i), card);
        btn->setObjectName(QStringLiteral("segmentButton"));
        btn->setCheckable(true);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            switchMode(i);
        });
        m_modeButtons.append(btn);
        modeRow->addWidget(btn, 1);
    }
    cardLayout->addLayout(modeRow);

    m_phoneEdit = new QLineEdit(card);
    m_phoneEdit->setPlaceholderText(QStringLiteral("请输入 11 位手机号"));
    m_phoneEdit->setMaxLength(11);
    m_phoneEdit->setInputMethodHints(Qt::ImhDigitsOnly);
    cardLayout->addWidget(m_phoneEdit);

    m_formStack = new QStackedWidget(card);

    auto *passwordForm = new QWidget(m_formStack);
    auto *passwordLayout = new QVBoxLayout(passwordForm);
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    m_passwordEdit = new QLineEdit(passwordForm);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    m_passwordEdit->setMaxLength(20);
    passwordLayout->addWidget(ui::withPasswordToggle(m_passwordEdit, passwordForm));
    m_formStack->addWidget(passwordForm);

    auto *codeForm = new QWidget(m_formStack);
    auto *codeLayout = new QHBoxLayout(codeForm);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(8);
    m_codeEdit = new QLineEdit(codeForm);
    m_codeEdit->setPlaceholderText(QStringLiteral("6 位短信验证码"));
    m_codeEdit->setMaxLength(6);
    m_codeEdit->setInputMethodHints(Qt::ImhDigitsOnly);
    m_codeButton = new SmsCodeButton(m_client, m_phoneEdit, codeForm);
    codeLayout->addWidget(m_codeEdit, 1);
    codeLayout->addWidget(m_codeButton, 0);
    m_formStack->addWidget(codeForm);
    cardLayout->addWidget(m_formStack);

    m_loginButton = new QPushButton(QStringLiteral("登录 / 注册"), card);
    m_loginButton->setProperty("class", QStringLiteral("primary"));
    cardLayout->addWidget(m_loginButton);

    auto *forgotRow = new QHBoxLayout();
    forgotRow->addStretch(1);
    auto *forgotButton = new QPushButton(QStringLiteral("忘记密码？"), card);
    forgotButton->setProperty("class", QStringLiteral("link"));
    forgotButton->setCursor(Qt::PointingHandCursor);
    forgotRow->addWidget(forgotButton, 0);
    cardLayout->addLayout(forgotRow);

    auto *divider = new QFrame(card);
    divider->setObjectName(QStringLiteral("divider"));
    cardLayout->addWidget(divider);

    auto *serverLabel = new QLabel(QStringLiteral("服务器地址"), card);
    serverLabel->setObjectName(QStringLiteral("hint"));
    cardLayout->addWidget(serverLabel);

    auto *serverRow = new QHBoxLayout();
    m_hostEdit = new QLineEdit(card);
    m_hostEdit->setPlaceholderText(QStringLiteral("主机"));
    m_portEdit = new QLineEdit(card);
    m_portEdit->setPlaceholderText(QStringLiteral("端口"));
    m_portEdit->setMaxLength(5);
    m_portEdit->setInputMethodHints(Qt::ImhDigitsOnly);
    m_portEdit->setFixedWidth(84);
    serverRow->addWidget(m_hostEdit, 1);
    serverRow->addWidget(m_portEdit, 0);
    cardLayout->addLayout(serverRow);

    root->addWidget(card);
    root->addStretch(1);

    switchMode(0);

    m_codeButton->setEnsureConnected([this]() {
        return ensureConnected();
    });

    connect(m_loginButton, &QPushButton::clicked, this, &LoginPage::onLoginClicked);
    connect(forgotButton, &QPushButton::clicked, this, &LoginPage::showForgotPassword);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginPage::onLoginClicked);
    connect(m_codeEdit, &QLineEdit::returnPressed, this, &LoginPage::onLoginClicked);
    connect(m_client, &SocketClient::connected, this, [this]() {
        if (m_waitingConnect) {
            m_waitingConnect = false;
            setBusy(true); // 连接成功，切换为「登录中…」并禁用取消
            doLogin();
        }
    });
    connect(m_client, &SocketClient::connectionLost, this, [this](const QString &reason) {
        if (m_loginButton->isEnabled())
            return;
        m_waitingConnect = false;
        setBusy(false);
        QMessageBox::warning(this, QStringLiteral("连接服务器"),
                             QStringLiteral("无法连接服务器：%1").arg(reason));
    });
    connect(m_client, &SocketClient::connectFailed, this, [this](const QString &reason) {
        if (!m_waitingConnect)
            return; // 非本次登录触发的连接失败（如已登录后的后台重连），由主窗口横幅提示
        m_waitingConnect = false;
        setBusy(false);
        QMessageBox::warning(this, QStringLiteral("连接服务器"),
                             QStringLiteral("无法连接服务器：%1").arg(reason));
    });
}

void LoginPage::setServerConfig(const AppConfig &config)
{
    m_hostEdit->setText(config.host);
    m_portEdit->setText(QString::number(config.port));
}

AppConfig LoginPage::currentConfig() const
{
    AppConfig c;
    c.host = m_hostEdit->text().trimmed();
    c.port = static_cast<quint16>(m_portEdit->text().toUInt());
    return c;
}

void LoginPage::switchMode(int index)
{
    m_formStack->setCurrentIndex(index);
    for (int i = 0; i < m_modeButtons.size(); ++i)
        m_modeButtons.at(i)->setChecked(i == index);
}

bool LoginPage::ensureConnected()
{
    const AppConfig config = currentConfig();
    const quint32 port = m_portEdit->text().toUInt();
    if (config.host.isEmpty() || port == 0 || port > 65535) {
        QMessageBox::warning(this, QStringLiteral("连接服务器"),
                             QStringLiteral("请先填写正确的服务器地址与端口"));
        return false;
    }
    if (m_client->isConnected()
        && m_client->serverDescription() == QStringLiteral("%1:%2").arg(config.host).arg(port))
        return true;
    emit configChanged(config);
    m_client->open(config.host, static_cast<quint16>(port));
    return true;
}

void LoginPage::onLoginClicked()
{
    if (m_waitingConnect) {
        // 连接等待期按钮文案为「连接中…（点击取消）」，再次点击即取消本次连接
        m_waitingConnect = false;
        m_client->cancelConnect();
        setBusy(false);
        return;
    }

    static const QRegularExpression phoneRe(QStringLiteral("^1\\d{10}$"));
    static const QRegularExpression codeRe(QStringLiteral("^\\d{6}$"));
    const QString phone = m_phoneEdit->text().trimmed();
    if (!phoneRe.match(phone).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("登录"), QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    if (m_formStack->currentIndex() == 0) {
        if (m_passwordEdit->text().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("登录"), QStringLiteral("请输入密码"));
            return;
        }
    } else if (!codeRe.match(m_codeEdit->text().trimmed()).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("登录"), QStringLiteral("请输入 6 位短信验证码"));
        return;
    }

    if (m_client->isConnected()
        && m_client->serverDescription() == QStringLiteral("%1:%2")
                                               .arg(currentConfig().host)
                                               .arg(m_portEdit->text().toUInt())) {
        setBusy(true);
        doLogin();
        return;
    }
    if (!ensureConnected())
        return;
    m_waitingConnect = true;
    setBusy(true);
}

void LoginPage::doLogin()
{
    const QString phone = m_phoneEdit->text().trimmed();
    QString password;
    QString code;
    if (m_formStack->currentIndex() == 0)
        password = m_passwordEdit->text();
    else
        code = m_codeEdit->text().trimmed();
    m_client->login(phone, password, code,
                    [this](int code, const QString &msg, const QJsonObject &data) {
                        setBusy(false);
                        if (code == 0) {
                            const UserInfo user =
                                UserInfo::fromJson(data.value(QStringLiteral("user")).toObject());
                            const bool isNew = data.value(QStringLiteral("isNew")).toBool();
                            if (isNew) {
                                QMessageBox::information(this, QStringLiteral("登录"),
                                                         QStringLiteral("手机号未注册，已自动创建账号"));
                            }
                            emit loginSuccess(user, isNew);
                            return;
                        }
                        if (code == SocketClient::kErrConnectionLost)
                            return; // 断线已由 connectionLost 提示，避免重复弹窗
                        showLoginError(code, msg);
                    });
}

void LoginPage::showLoginError(int code, const QString &msg)
{
    QString text;
    if (code == 1001)
        text = QStringLiteral("密码或验证码错误");
    else if (code == 1002)
        text = QStringLiteral("账号已冻结，请联系管理员");
    else if (code == 1005)
        text = QStringLiteral("账号已注销，如有疑问请联系管理员");
    else
        text = msg.isEmpty() ? QStringLiteral("登录失败，请稍后重试") : msg;
    QMessageBox::warning(this, QStringLiteral("登录"), text);
}

void LoginPage::showForgotPassword()
{
    ResetPasswordDialog dlg(m_client, this);
    dlg.setPhone(m_phoneEdit->text().trimmed());
    dlg.setEnsureConnected([this]() {
        return ensureConnected();
    });
    dlg.exec();
}

void LoginPage::setBusy(bool busy)
{
    // 连接等待期登录按钮保持可用（点击即取消连接）；请求在途期禁用
    m_loginButton->setEnabled(!busy || m_waitingConnect);
    m_loginButton->setText(!busy ? QStringLiteral("登录 / 注册")
                           : m_waitingConnect ? QStringLiteral("连接中…（点击取消）")
                                              : QStringLiteral("登录中…"));
    m_phoneEdit->setEnabled(!busy);
    m_formStack->setEnabled(!busy);
    for (QPushButton *btn : m_modeButtons)
        btn->setEnabled(!busy);
    m_hostEdit->setEnabled(!busy);
    m_portEdit->setEnabled(!busy);
}
