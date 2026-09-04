#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;
class SocketClient;

class PileStatusPage : public QWidget
{
    Q_OBJECT

public:
    explicit PileStatusPage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private:
    QWidget *createStatusCard(const QString &title, QLabel **valueLabel);

    SocketClient *m_client;
    QLabel *m_idleValue;
    QLabel *m_inUseValue;
    QLabel *m_faultValue;
    QTableWidget *m_table;
};
