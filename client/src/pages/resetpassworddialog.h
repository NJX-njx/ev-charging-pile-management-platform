#pragma once

#include <QDialog>

#include <functional>

class QLineEdit;
class QPushButton;
class SocketClient;
class SmsCodeButton;

// 忘记密码：凭短信验证码重置登录密码（user_password_reset，无需登录）
class ResetPasswordDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResetPasswordDialog(SocketClient *client, QWidget *parent = nullptr);

    void setPhone(const QString &phone);
    // 未连接时由调用方负责发起连接（同登录页服务器配置）
    void setEnsureConnected(std::function<bool()> fn);

private:
    void onReset();

    SocketClient *m_client;
    QLineEdit *m_phoneEdit;
    QLineEdit *m_codeEdit;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_confirmEdit;
    SmsCodeButton *m_codeButton;
    QPushButton *m_resetButton;
};
