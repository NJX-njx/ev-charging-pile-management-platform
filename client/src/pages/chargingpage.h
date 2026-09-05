#pragma once

#include <QHash>
#include <QList>
#include <QWidget>

#include "model/models.h"

class QLabel;
class QTimer;
class QVBoxLayout;
class SocketClient;

// 充电页（协议 v2.2）：active_order_get 返回未完成订单数组，每单一卡并行展示；
// 卡片按状态给出操作（reserved→开始/取消，charging→停止，pending_payment→结算）；
// charging 卡片每秒刷新「已充时长｜预计花费」（powerKw × 已充小时 × unitPrice，
// 仅为估算展示，以实际结算为准）。进入页面、定时（15s）与每次操作后刷新整表。
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
    void hideEvent(QHideEvent *event) override;

private:
    void applyOrders(const QList<Order> &orders);
    void buildCards();
    void updateTick();
    void updateBalanceLabel();
    void doAction(const QString &type, qint64 orderId, const QString &actionName, bool confirm);

    SocketClient *m_client;
    QWidget *m_listContainer;
    QVBoxLayout *m_listLayout;
    QLabel *m_balanceLabel;
    QTimer *m_tickTimer;
    QTimer *m_refreshTimer;
    QList<Order> m_orders;
    QHash<qint64, QLabel *> m_tickLabels; // charging 订单 orderId → 「时长｜花费」行
    bool m_busy = false;
};
