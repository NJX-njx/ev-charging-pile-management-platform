#include "chargingpage.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "ui/uienums.h"

namespace {

constexpr int kRefreshIntervalMs = 15000;

QString formatElapsed(qint64 secs)
{
    const qint64 h = secs / 3600;
    const qint64 m = (secs % 3600) / 60;
    const qint64 s = secs % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2")
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QString tickText(const Order &order)
{
    const QDateTime start = order.startDateTime();
    if (!start.isValid())
        return QStringLiteral("已充时长 --:--");
    const qint64 secs = start.secsTo(QDateTime::currentDateTime());
    if (secs < 0)
        return QStringLiteral("已充时长 --:--");
    QString text = QStringLiteral("已充时长 %1").arg(formatElapsed(secs));
    if (order.powerKw > 0.0) {
        const double hours = static_cast<double>(secs) / 3600.0;
        const double cost = order.powerKw * hours * order.unitPrice;
        text += QStringLiteral("｜预计花费 %1 元").arg(cost, 0, 'f', 2);
    } else {
        text += QStringLiteral("｜预计花费 --");
    }
    return text;
}

QLabel *makeHint(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hint"));
    label->setWordWrap(true);
    return label;
}

} // namespace

ChargingPage::ChargingPage(SocketClient *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    auto *headerRow = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("充电"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    refreshButton->setProperty("class", QStringLiteral("small"));
    headerRow->addWidget(title, 1);
    headerRow->addWidget(refreshButton, 0);
    root->addLayout(headerRow);

    m_balanceLabel = makeHint(this, QString());
    m_balanceLabel->setVisible(false);
    root->addWidget(m_balanceLabel);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_listContainer = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(12);
    scroll->setWidget(m_listContainer);
    root->addWidget(scroll, 1);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &ChargingPage::updateTick);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(kRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_busy)
            refresh();
    });

    connect(refreshButton, &QPushButton::clicked, this, &ChargingPage::refresh);

    buildCards();
}

void ChargingPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_client->isLoggedIn()) {
        refresh();
        m_refreshTimer->start();
    }
}

void ChargingPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_refreshTimer->stop();
}

void ChargingPage::refresh()
{
    if (!m_client->isConnected())
        return;
    m_client->sendRequest(QStringLiteral("active_order_get"), QJsonObject{},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              if (code != 0) {
                                  if (code > 0)
                                      QMessageBox::warning(this, QStringLiteral("刷新"), msg);
                                  return;
                              }
                              // 协议 v2.2：未完成订单数组（reserved/charging/pending_payment）
                              QList<Order> orders;
                              const QJsonArray arr = data.value(QStringLiteral("orders")).toArray();
                              orders.reserve(arr.size());
                              for (const QJsonValue &v : arr)
                                  orders.append(Order::fromJson(v.toObject()));
                              applyOrders(orders);
                          });
}

void ChargingPage::applyOrders(const QList<Order> &orders)
{
    m_orders = orders;
    buildCards();
    updateBalanceLabel();
    bool anyCharging = false;
    for (const Order &o : m_orders) {
        if (o.status == QLatin1String("charging") && o.startDateTime().isValid()) {
            anyCharging = true;
            break;
        }
    }
    if (anyCharging) {
        updateTick();
        m_tickTimer->start();
    } else {
        m_tickTimer->stop();
    }
}

void ChargingPage::buildCards()
{
    m_tickLabels.clear();
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    if (m_orders.isEmpty()) {
        auto *emptyHint = new QLabel(QStringLiteral("当前没有进行中的订单"), m_listContainer);
        emptyHint->setObjectName(QStringLiteral("hint"));
        emptyHint->setAlignment(Qt::AlignHCenter);
        auto *gotoFind = new QPushButton(QStringLiteral("去找站"), m_listContainer);
        gotoFind->setProperty("class", QStringLiteral("primary"));
        connect(gotoFind, &QPushButton::clicked, this, &ChargingPage::gotoFindStations);
        m_listLayout->addStretch(1);
        m_listLayout->addWidget(emptyHint);
        m_listLayout->addWidget(gotoFind);
        m_listLayout->addStretch(1);
        return;
    }

    for (const Order &order : m_orders) {
        auto *card = new QFrame(m_listContainer);
        card->setObjectName(QStringLiteral("orderCard"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);
        cardLayout->setSpacing(6);

        auto *topRow = new QHBoxLayout();
        auto *name = new QLabel(order.stationName, card);
        name->setObjectName(QStringLiteral("cardTitle"));
        topRow->addWidget(name, 1);
        auto *status = new QLabel(ui::orderStatusText(order.status), card);
        ui::setState(status, order.status);
        topRow->addWidget(status, 0);
        cardLayout->addLayout(topRow);

        QString info = QStringLiteral("电桩：%1 · 电价 ¥%2/kWh")
                           .arg(order.pileCode)
                           .arg(order.unitPrice, 0, 'f', 2);
        if (order.powerKw > 0.0)
            info += QStringLiteral(" · 功率 %1kW").arg(order.powerKw, 0, 'f', 0);
        cardLayout->addWidget(new QLabel(info, card));
        cardLayout->addWidget(makeHint(card, QStringLiteral("预约时间：%1")
                                                 .arg(formatDateTime(order.reservedAt))));

        auto *buttonRow = new QHBoxLayout();
        if (order.status == QLatin1String("reserved")) {
            auto *startButton = new QPushButton(QStringLiteral("开始充电"), card);
            startButton->setProperty("class", QStringLiteral("smallPrimary"));
            auto *cancelButton = new QPushButton(QStringLiteral("取消预约"), card);
            cancelButton->setProperty("class", QStringLiteral("small"));
            buttonRow->addWidget(startButton, 1);
            buttonRow->addWidget(cancelButton, 1);
            connect(startButton, &QPushButton::clicked, this, [this, order]() {
                doAction(QStringLiteral("charge_start"), order.orderId, QStringLiteral("开始充电"), false);
            });
            connect(cancelButton, &QPushButton::clicked, this, [this, order]() {
                doAction(QStringLiteral("charge_cancel"), order.orderId, QStringLiteral("取消预约"), true);
            });
        } else if (order.status == QLatin1String("charging")) {
            cardLayout->addWidget(makeHint(card, QStringLiteral("开始时间：%1")
                                                     .arg(formatDateTime(order.startTime))));
            auto *tickLabel = new QLabel(tickText(order), card);
            tickLabel->setObjectName(QStringLiteral("emphasis"));
            m_tickLabels.insert(order.orderId, tickLabel);
            cardLayout->addWidget(tickLabel);
            cardLayout->addWidget(makeHint(card, QStringLiteral("预计花费为估算值，以实际结算为准")));
            auto *stopButton = new QPushButton(QStringLiteral("停止充电"), card);
            stopButton->setProperty("class", QStringLiteral("smallPrimary"));
            buttonRow->addWidget(stopButton, 1);
            connect(stopButton, &QPushButton::clicked, this, [this, order]() {
                doAction(QStringLiteral("charge_stop"), order.orderId, QStringLiteral("停止充电"), true);
            });
        } else if (order.status == QLatin1String("pending_payment")) {
            QString payment = QStringLiteral("电量 %1 kWh · 金额 ¥%2")
                                  .arg(order.energyKwh, 0, 'f', 3)
                                  .arg(order.amount, 0, 'f', 2);
            cardLayout->addWidget(new QLabel(payment, card));
            auto *settleButton = new QPushButton(QStringLiteral("结算"), card);
            settleButton->setProperty("class", QStringLiteral("smallPrimary"));
            buttonRow->addWidget(settleButton, 1);
            connect(settleButton, &QPushButton::clicked, this, [this, order]() {
                doAction(QStringLiteral("charge_settle"), order.orderId, QStringLiteral("结算"), false);
            });
        }
        cardLayout->addLayout(buttonRow);
        m_listLayout->addWidget(card);
    }
    m_listLayout->addStretch(1);
}

void ChargingPage::updateTick()
{
    for (const Order &o : m_orders) {
        if (o.status != QLatin1String("charging"))
            continue;
        QLabel *label = m_tickLabels.value(o.orderId, nullptr);
        if (label)
            label->setText(tickText(o));
    }
}

void ChargingPage::updateBalanceLabel()
{
    bool hasPendingPayment = false;
    for (const Order &o : m_orders) {
        if (o.status == QLatin1String("pending_payment")) {
            hasPendingPayment = true;
            break;
        }
    }
    if (!hasPendingPayment) {
        m_balanceLabel->setVisible(false);
        return;
    }
    m_client->sendRequest(QStringLiteral("user_profile_get"), QJsonObject{},
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const UserInfo user = UserInfo::fromJson(
                                  data.value(QStringLiteral("user")).toObject());
                              m_balanceLabel->setText(QStringLiteral("当前余额：¥%1（余额不足请先到「我的」充值）")
                                                          .arg(user.balance, 0, 'f', 2));
                              m_balanceLabel->setVisible(true);
                          });
}

void ChargingPage::doAction(const QString &type, qint64 orderId, const QString &actionName, bool confirm)
{
    if (m_busy)
        return;
    if (confirm) {
        const auto choice = QMessageBox::question(
            this, actionName, QStringLiteral("确定要%1订单 %2 吗？").arg(actionName).arg(orderId));
        if (choice != QMessageBox::Yes)
            return;
    }
    m_busy = true;
    m_listContainer->setEnabled(false);

    m_client->sendRequest(type, QJsonObject{{QStringLiteral("orderId"), static_cast<double>(orderId)}},
                          [this, type, actionName](int code, const QString &msg, const QJsonObject &data) {
                              m_busy = false;
                              m_listContainer->setEnabled(true);
                              if (code == 0) {
                                  // 操作后整表刷新（v2.2 多订单并行，状态以服务端为准）
                                  refresh();
                                  if (type == QLatin1String("charge_settle")) {
                                      const double balance = data.value(QStringLiteral("balance")).toDouble();
                                      QMessageBox::information(this, actionName,
                                                               QStringLiteral("结算完成，余额 ¥%1").arg(balance, 0, 'f', 2));
                                  } else if (type == QLatin1String("charge_cancel")) {
                                      QMessageBox::information(this, actionName, QStringLiteral("预约已取消"));
                                  }
                                  return;
                              }
                              if (code == SocketClient::kErrConnectionLost) {
                                  QMessageBox::warning(this, actionName,
                                                       QStringLiteral("网络中断，%1结果未知，正在刷新订单状态").arg(actionName));
                                  refresh();
                                  return;
                              }
                              if (code == 3004) {
                                  QMessageBox::warning(this, actionName,
                                                       QStringLiteral("余额不足，请先到「我的」页面充值"));
                                  emit requestRecharge();
                                  refresh();
                                  return;
                              }
                              QMessageBox::warning(this, actionName,
                                                   msg.isEmpty() ? QStringLiteral("操作失败") : msg);
                              refresh();
                          });
}
