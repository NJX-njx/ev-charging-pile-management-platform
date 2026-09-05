#pragma once

#include <QDialog>

class QPushButton;
class QTableWidget;
class SocketClient;

// 管理员账号管理（协议 7.23~7.25）：管理员无公开注册，只能由已登录管理员维护
class AdminManageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AdminManageDialog(SocketClient *client, const QString &currentUsername,
                               QWidget *parent = nullptr);

private slots:
    void onAddAdmin();
    void onDeleteAdmin();

private:
    void loadAdmins();
    void updateActionButtons();
    int selectedRow() const;

    SocketClient *m_client;
    QString m_currentUsername;
    QTableWidget *m_table;
    QPushButton *m_addBtn;
    QPushButton *m_deleteBtn;
};
