#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class SocketClient;

// 设置/修改登录密码（user_password_update）
// hasPassword=false 时为首次设置：不显示原密码框，请求省略 oldPassword；
// allowSkip=true 时提供「暂不设置」按钮（用于首次登录引导）
class PasswordDialog : public QDialog
{
    Q_OBJECT
public:
    PasswordDialog(SocketClient *client, bool hasPassword, bool allowSkip, QWidget *parent = nullptr);

signals:
    void passwordUpdated(const QString &newPassword);

private:
    void onSubmit();

    SocketClient *m_client;
    bool m_hasPassword;
    QLineEdit *m_oldEdit = nullptr;
    QLineEdit *m_newEdit;
    QLineEdit *m_confirmEdit;
    QPushButton *m_submitButton;
};
