#include "loginwindow.h"
#include "ui_loginwindow.h"

#include <QJsonObject>
#include <QMessageBox>

#include "mainwindow.h"
#include "net/socketclient.h"

LoginWindow::LoginWindow(SocketClient *client, QWidget *parent)
    : QWidget(parent), ui(new Ui::LoginWindow), m_client(client)
{
    ui->setupUi(this);
    connect(ui->pushButtonLogin, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
}

LoginWindow::~LoginWindow()
{
    delete ui;
}

void LoginWindow::onLoginClicked()
{
    const QString username = ui->lineEditUser->text().trimmed();
    const QString password = ui->lineEditPass->text();
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入账号和密码"));
        return;
    }

    ui->pushButtonLogin->setEnabled(false);

    QJsonObject payload;
    payload[QStringLiteral("username")] = username;
    payload[QStringLiteral("password")] = password;

    m_client->sendRequest(QStringLiteral("admin_login"), payload,
                          [this](int code, const QString &msg, const QJsonObject &) {
                              ui->pushButtonLogin->setEnabled(true);
                              if (code == 0) {
                                  MainWindow *w = new MainWindow(m_client);
                                  w->show();
                                  close();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("登录失败"),
                                                       msg.isEmpty() ? QStringLiteral("账号或密码错误") : msg);
                              }
                          });
}
