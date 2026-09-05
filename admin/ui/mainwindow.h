#pragma once

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class QLabel;
class SocketClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(SocketClient *client, const QString &username, const QString &password,
                        QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onChangePassword();
    void onManageAdmins();

private:
    void refreshCurrentPage();

    Ui::MainWindow *ui;
    SocketClient *m_client;
    QString m_username;
    QString m_password;
    QLabel *m_connLabel;
    bool m_pwdUpdatePending = false;
};
