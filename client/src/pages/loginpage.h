#pragma once

#include <QWidget>

#include "model/models.h"

class QLineEdit;
class QPushButton;
class SocketClient;
struct AppConfig;

class LoginPage : public QWidget
{
    Q_OBJECT
public:
    explicit LoginPage(SocketClient *client, QWidget *parent = nullptr);

    void setServerConfig(const AppConfig &config);
    AppConfig currentConfig() const;

signals:
    void loginSuccess(const UserInfo &user, bool isNew);
    void configChanged(const AppConfig &config);

private:
    void onLoginClicked();
    void doLogin();
    void setBusy(bool busy);

    SocketClient *m_client;
    QLineEdit *m_phoneEdit;
    QLineEdit *m_hostEdit;
    QLineEdit *m_portEdit;
    QPushButton *m_loginButton;
    bool m_waitingConnect = false;
};
