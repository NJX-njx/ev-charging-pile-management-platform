#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class SocketClient;

class SalesPage : public QWidget
{
    Q_OBJECT

public:
    explicit SalesPage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private:
    QWidget *createMetricCard(const QString &title, QLabel **valueLabel);
    void updateTrend(int range);

    SocketClient *m_client;
    QLabel *m_todayValue;
    QLabel *m_monthValue;
    QLabel *m_totalValue;
    QComboBox *m_rangeBox;
    QWidget *m_chartArea = nullptr;
};
