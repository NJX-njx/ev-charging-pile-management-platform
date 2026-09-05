#pragma once

#include <QWidget>

class QTimer;

namespace Ui {
class LoginWindow;
}

class SocketClient;

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(SocketClient *client, QWidget *parent = nullptr);
    ~LoginWindow();

private slots:
    void onLoginClicked();

private:
    void doLogin();
    void onConnectTimeout();

    Ui::LoginWindow *ui;
    SocketClient *m_client;
    QTimer *m_connectTimer;
    QString m_pendingUsername;
    QString m_pendingPassword;
};
