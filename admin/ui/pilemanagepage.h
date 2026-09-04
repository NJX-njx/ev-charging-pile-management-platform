#pragma once

#include <QWidget>

class QPushButton;
class QTableWidget;
class SocketClient;

class PileManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit PileManagePage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onRestartClicked();

private:
    SocketClient *m_client;
    QTableWidget *m_table;
    QPushButton *m_restartBtn;
};
