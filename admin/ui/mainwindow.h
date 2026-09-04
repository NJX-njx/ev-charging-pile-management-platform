#pragma once

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class SocketClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(SocketClient *client, QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    SocketClient *m_client;
};
