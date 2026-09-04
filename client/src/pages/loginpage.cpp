#include "loginpage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "model/appconfig.h"
#include "net/socketclient.h"

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

    m_phoneEdit = new QLineEdit(card);
    m_phoneEdit->setPlaceholderText(QStringLiteral("请输入 11 位手机号"));
    m_phoneEdit->setMaxLength(11);
    cardLayout->addWidget(m_phoneEdit);

    m_loginButton = new QPushButton(QStringLiteral("登录 / 注册"), card);
    m_loginButton->setProperty("class", QStringLiteral("primary"));
    cardLayout->addWidget(m_loginButton);

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
    m_portEdit->setFixedWidth(72);
    serverRow->addWidget(m_hostEdit, 1);
    serverRow->addWidget(m_portEdit, 0);
    cardLayout->addLayout(serverRow);

    root->addWidget(card);
    root->addStretch(1);

    connect(m_loginButton, &QPushButton::clicked, this, &LoginPage::onLoginClicked);
    connect(m_client, &SocketClient::connected, this, [this]() {
        if (m_waitingConnect) {
            m_waitingConnect = false;
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

void LoginPage::onLoginClicked()
{
    static const QRegularExpression phoneRe(QStringLiteral("^1\\d{10}$"));
    const QString phone = m_phoneEdit->text().trimmed();
    if (!phoneRe.match(phone).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("登录"), QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    const AppConfig config = currentConfig();
    const quint32 port = m_portEdit->text().toUInt();
    if (config.host.isEmpty() || port == 0 || port > 65535) {
        QMessageBox::warning(this, QStringLiteral("登录"), QStringLiteral("请输入正确的服务器地址与端口"));
        return;
    }

    setBusy(true);
    if (m_client->isConnected()
        && m_client->serverDescription() == QStringLiteral("%1:%2").arg(config.host).arg(port)) {
        doLogin();
        return;
    }
    emit configChanged(config);
    m_waitingConnect = true;
    m_client->open(config.host, static_cast<quint16>(port));
}

void LoginPage::doLogin()
{
    const QString phone = m_phoneEdit->text().trimmed();
    m_client->login(phone, [this](int code, const QString &msg, const QJsonObject &data) {
        setBusy(false);
        if (code == 0) {
            const UserInfo user = UserInfo::fromJson(data.value(QStringLiteral("user")).toObject());
            const bool isNew = data.value(QStringLiteral("isNew")).toBool();
            if (isNew) {
                QMessageBox::information(this, QStringLiteral("登录"),
                                         QStringLiteral("手机号未注册，已自动创建账号"));
            }
            emit loginSuccess(user, isNew);
            return;
        }
        if (code == 1002) {
            QMessageBox::warning(this, QStringLiteral("登录"),
                                 QStringLiteral("账号已被冻结，请联系管理员"));
            return;
        }
        QMessageBox::warning(this, QStringLiteral("登录"),
                             msg.isEmpty() ? QStringLiteral("登录失败，请稍后重试") : msg);
    });
}

void LoginPage::setBusy(bool busy)
{
    m_loginButton->setEnabled(!busy);
    m_phoneEdit->setEnabled(!busy);
    m_hostEdit->setEnabled(!busy);
    m_portEdit->setEnabled(!busy);
}
