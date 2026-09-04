#pragma once

#include <QWidget>

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
    Ui::LoginWindow *ui;
    SocketClient *m_client;
};
