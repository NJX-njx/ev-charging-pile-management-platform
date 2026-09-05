#include "loginwindow.h"
#include "ui_loginwindow.h"

#include <QIntValidator>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include "mainwindow.h"
#include "net/socketclient.h"

LoginWindow::LoginWindow(SocketClient *client, QWidget *parent)
    : QWidget(parent), ui(new Ui::LoginWindow), m_client(client),
      m_connectTimer(new QTimer(this))
{
    ui->setupUi(this);
    ui->pushButtonLogin->setProperty("primary", true);

    // 服务器地址/端口持久化（main.cpp 已设置组织名与应用名）
    const QSettings settings;
    ui->lineEditHost->setText(
        settings.value(QStringLiteral("server/host"), QStringLiteral("127.0.0.1")).toString());
    ui->lineEditPort->setText(
        settings.value(QStringLiteral("server/port"), QStringLiteral("8888")).toString());
    // 端口为纯数字输入框，保持数字限制
    ui->lineEditPort->setValidator(new QIntValidator(1, 65535, ui->lineEditPort));

    // 等待 connectToServer 结果：连上后自动发登录请求，超时则提示并放弃
    m_connectTimer->setSingleShot(true);
    m_connectTimer->setInterval(5000);
    connect(m_connectTimer, &QTimer::timeout, this, &LoginWindow::onConnectTimeout);
    connect(m_client, &SocketClient::connected, this, [this]() {
        if (m_connectTimer->isActive()) {
            m_connectTimer->stop();
            doLogin();
        }
    });

    connect(ui->pushButtonLogin, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
}

LoginWindow::~LoginWindow()
{
    delete ui;
}

void LoginWindow::onLoginClicked()
{
    const QString host = ui->lineEditHost->text().trimmed();
    const QString portStr = ui->lineEditPort->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入服务器地址"));
        return;
    }
    bool portOk = false;
    const int port = portStr.toInt(&portOk);
    if (!portOk || port < 1 || port > 65535) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("端口号须为 1 至 65535 的整数"));
        return;
    }
    const QString username = ui->lineEditUser->text().trimmed();
    const QString password = ui->lineEditPass->text();
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入账号和密码"));
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("server/host"), host);
    settings.setValue(QStringLiteral("server/port"), portStr);

    ui->pushButtonLogin->setEnabled(false);
    m_pendingUsername = username;
    m_pendingPassword = password;

    if (m_client->isConnected() && m_client->host() == host
        && m_client->port() == static_cast<quint16>(port)) {
        doLogin();
    } else {
        m_client->connectToServer(host, static_cast<quint16>(port));
        m_connectTimer->start();
    }
}

void LoginWindow::onConnectTimeout()
{
    m_client->abortConnection();
    ui->pushButtonLogin->setEnabled(true);
    QMessageBox::warning(this, QStringLiteral("登录失败"),
                         QStringLiteral("无法连接服务器 %1:%2，请检查地址、端口与服务端是否已启动")
                             .arg(ui->lineEditHost->text().trimmed())
                             .arg(ui->lineEditPort->text().trimmed()));
}

void LoginWindow::doLogin()
{
    QJsonObject payload;
    payload[QStringLiteral("username")] = m_pendingUsername;
    payload[QStringLiteral("password")] = m_pendingPassword;

    m_client->sendRequest(QStringLiteral("admin_login"), payload,
                          [this](int code, const QString &msg, const QJsonObject &) {
                              ui->pushButtonLogin->setEnabled(true);
                              if (code == 0) {
                                  MainWindow *w = new MainWindow(m_client, m_pendingUsername, m_pendingPassword);
                                  w->show();
                                  close();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("登录失败"),
                                                       msg.isEmpty() ? QStringLiteral("账号或密码错误") : msg);
                              }
                          });
}
