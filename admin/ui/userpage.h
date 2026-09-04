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

private:
    void loadUsers(const QString &phoneKeyword);

    SocketClient *m_client;
    QLineEdit *m_searchEdit;
    QTableWidget *m_table;
    QPushButton *m_toggleBtn;
};
