#pragma once

#include <QList>
#include <QWidget>

#include "model/models.h"

class QLineEdit;
class QPushButton;
class QStackedWidget;
class SocketClient;
class SmsCodeButton;
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
    void switchMode(int index);
    void onLoginClicked();
    void doLogin();
    void showLoginError(int code, const QString &msg);
    void showForgotPassword();
    bool ensureConnected();
    void setBusy(bool busy);

    SocketClient *m_client;
    QList<QPushButton *> m_modeButtons;
    QLineEdit *m_phoneEdit;
    QStackedWidget *m_formStack;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_codeEdit;
    SmsCodeButton *m_codeButton;
    QLineEdit *m_hostEdit;
    QLineEdit *m_portEdit;
    QPushButton *m_loginButton;
    bool m_waitingConnect = false;
};
