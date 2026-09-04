#pragma once

#include <QWidget>

#include "model/models.h"

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class SocketClient;

class ChargingPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChargingPage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

signals:
    void gotoFindStations();
    void requestRecharge();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyOrder(const Order &order);
    void applyEmpty();
    void updateElapsed();
    void doAction(const QString &type, qint64 orderId, const QString &actionName, bool confirm);
    void fetchBalance();

    SocketClient *m_client;
    QStackedWidget *m_stack;
    QTimer *m_elapsedTimer;
    Order m_order;
    bool m_hasOrder = false;
    bool m_busy = false;

    QLabel *m_reservedInfo;
    QPushButton *m_startButton;
    QPushButton *m_cancelButton;

    QLabel *m_chargingInfo;
    QLabel *m_elapsedLabel;
    QPushButton *m_stopButton;

    QLabel *m_paymentInfo;
    QLabel *m_paymentBalance;
    QPushButton *m_settleButton;
    QPushButton *m_paymentRefreshButton;
};
