#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTableWidget;
class FilterTable;
class SocketClient;

// 系统管理页：整合管理员账号管理（协议 7.23~7.25）与修改密码（协议 7.11）
class SystemPage : public QWidget
{
    Q_OBJECT

public:
    explicit SystemPage(SocketClient *client, const QString &currentUsername,
                        QWidget *parent = nullptr);

    void refresh();

signals:
    // 修改密码成功后通知主窗口更新断线重连所用的密码（协议 2.3）
    void passwordChanged(const QString &newPassword);

private slots:
    void onAddAdmin();
    void onDeleteAdmin();
    void onChangePassword();

private:
    QWidget *createAdminCard();
    QWidget *createSecurityCard();
    void loadAdmins();
    void updateActionButtons();
    int selectedRow() const;

    SocketClient *m_client;
    QString m_currentUsername;
    QTableWidget *m_table;
    FilterTable *m_ft;
    QPushButton *m_addBtn;
    QPushButton *m_deleteBtn;
    QLineEdit *m_oldPwdEdit;
    QLineEdit *m_newPwdEdit;
    QLineEdit *m_confirmPwdEdit;
    QPushButton *m_changePwdBtn;
    bool m_pwdUpdatePending = false;
};
