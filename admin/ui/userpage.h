#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTableWidget;
class SocketClient;

class UserPage : public QWidget
{
    Q_OBJECT

public:
    explicit UserPage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onToggleStatus();
    void onAddUser();
    void onEditUser();
    void onResetPassword();
    void onDeleteUser();

private:
    void loadUsers(const QString &phoneKeyword);
    int selectedRow() const;
    void updateActionButtons();

    SocketClient *m_client;
    QLineEdit *m_searchEdit;
    QTableWidget *m_table;
    QPushButton *m_addBtn;
    QPushButton *m_editBtn;
    QPushButton *m_resetPwdBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_toggleBtn;
};
