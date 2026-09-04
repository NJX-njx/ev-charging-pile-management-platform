#include "chargingpage.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "ui/uienums.h"

namespace {
QLabel *makeRowValue(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}
}

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

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    auto *emptyPage = new QWidget(m_stack);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(12);
    emptyLayout->addStretch(1);
    auto *emptyHint = new QLabel(QStringLiteral("当前没有进行中的订单"), emptyPage);
    emptyHint->setObjectName(QStringLiteral("hint"));
    emptyHint->setAlignment(Qt::AlignHCenter);
    auto *gotoFind = new QPushButton(QStringLiteral("去找站"), emptyPage);
    gotoFind->setProperty("class", QStringLiteral("primary"));
    emptyLayout->addWidget(emptyHint);
    emptyLayout->addWidget(gotoFind);
    emptyLayout->addStretch(1);
    m_stack->addWidget(emptyPage);

    auto *reservedPage = new QWidget(m_stack);
    auto *reservedLayout = new QVBoxLayout(reservedPage);
    reservedLayout->setContentsMargins(0, 0, 0, 0);
    reservedLayout->setSpacing(12);
    auto *reservedCard = new QFrame(reservedPage);
    reservedCard->setObjectName(QStringLiteral("card"));
    auto *reservedCardLayout = new QVBoxLayout(reservedCard);
    reservedCardLayout->setContentsMargins(16, 16, 16, 16);
    reservedCardLayout->setSpacing(8);
    auto *reservedTitle = new QLabel(QStringLiteral("已预约"), reservedCard);
    reservedTitle->setObjectName(QStringLiteral("cardTitle"));
    m_reservedInfo = makeRowValue(reservedCard, QString());
    reservedCardLayout->addWidget(reservedTitle);
    reservedCardLayout->addWidget(m_reservedInfo);
    reservedLayout->addWidget(reservedCard);
    m_startButton = new QPushButton(QStringLiteral("开始充电"), reservedPage);
    m_startButton->setProperty("class", QStringLiteral("primary"));
    m_cancelButton = new QPushButton(QStringLiteral("取消预约"), reservedPage);
    reservedLayout->addWidget(m_startButton);
    reservedLayout->addWidget(m_cancelButton);
    reservedLayout->addStretch(1);
    m_stack->addWidget(reservedPage);

    auto *chargingPageW = new QWidget(m_stack);
    auto *chargingLayout = new QVBoxLayout(chargingPageW);
    chargingLayout->setContentsMargins(0, 0, 0, 0);
    chargingLayout->setSpacing(12);
    auto *chargingCard = new QFrame(chargingPageW);
    chargingCard->setObjectName(QStringLiteral("card"));
    auto *chargingCardLayout = new QVBoxLayout(chargingCard);
    chargingCardLayout->setContentsMargins(16, 16, 16, 16);
    chargingCardLayout->setSpacing(8);
    auto *chargingTitle = new QLabel(QStringLiteral("充电中"), chargingCard);
    chargingTitle->setObjectName(QStringLiteral("cardTitle"));
    m_elapsedLabel = new QLabel(QStringLiteral("00:00:00"), chargingCard);
    m_elapsedLabel->setObjectName(QStringLiteral("metric"));
    m_elapsedLabel->setAlignment(Qt::AlignHCenter);
    m_chargingInfo = makeRowValue(chargingCard, QString());
    auto *chargingHint = new QLabel(QStringLiteral("停止后将按实际充电量计费"), chargingCard);
    chargingHint->setObjectName(QStringLiteral("hint"));
    chargingCardLayout->addWidget(chargingTitle);
    chargingCardLayout->addWidget(m_elapsedLabel);
    chargingCardLayout->addWidget(m_chargingInfo);
    chargingCardLayout->addWidget(chargingHint);
    chargingLayout->addWidget(chargingCard);
    m_stopButton = new QPushButton(QStringLiteral("停止充电"), chargingPageW);
    m_stopButton->setProperty("class", QStringLiteral("primary"));
    chargingLayout->addWidget(m_stopButton);
    chargingLayout->addStretch(1);
    m_stack->addWidget(chargingPageW);

    auto *paymentPage = new QWidget(m_stack);
    auto *paymentLayout = new QVBoxLayout(paymentPage);
    paymentLayout->setContentsMargins(0, 0, 0, 0);
    paymentLayout->setSpacing(12);
    auto *paymentCard = new QFrame(paymentPage);
    paymentCard->setObjectName(QStringLiteral("card"));
    auto *paymentCardLayout = new QVBoxLayout(paymentCard);
    paymentCardLayout->setContentsMargins(16, 16, 16, 16);
    paymentCardLayout->setSpacing(8);
    auto *paymentTitle = new QLabel(QStringLiteral("待结算"), paymentCard);
    paymentTitle->setObjectName(QStringLiteral("cardTitle"));
    m_paymentInfo = makeRowValue(paymentCard, QString());
    m_paymentBalance = makeRowValue(paymentCard, QString());
    paymentCardLayout->addWidget(paymentTitle);
    paymentCardLayout->addWidget(m_paymentInfo);
    paymentCardLayout->addWidget(m_paymentBalance);
    paymentLayout->addWidget(paymentCard);
    m_settleButton = new QPushButton(QStringLiteral("结算"), paymentPage);
    m_settleButton->setProperty("class", QStringLiteral("primary"));
    m_paymentRefreshButton = new QPushButton(QStringLiteral("刷新状态"), paymentPage);
    paymentLayout->addWidget(m_settleButton);
    paymentLayout->addWidget(m_paymentRefreshButton);
    paymentLayout->addStretch(1);
    m_stack->addWidget(paymentPage);

    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(1000);
    connect(m_elapsedTimer, &QTimer::timeout, this, &ChargingPage::updateElapsed);

    connect(refreshButton, &QPushButton::clicked, this, &ChargingPage::refresh);
    connect(gotoFind, &QPushButton::clicked, this, &ChargingPage::gotoFindStations);
    connect(m_startButton, &QPushButton::clicked, this, [this]() {
        doAction(QStringLiteral("charge_start"), m_order.orderId, QStringLiteral("开始充电"), false);
    });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        doAction(QStringLiteral("charge_cancel"), m_order.orderId, QStringLiteral("取消预约"), true);
    });
    connect(m_stopButton, &QPushButton::clicked, this, [this]() {
        doAction(QStringLiteral("charge_stop"), m_order.orderId, QStringLiteral("停止充电"), true);
    });
    connect(m_settleButton, &QPushButton::clicked, this, [this]() {
        doAction(QStringLiteral("charge_settle"), m_order.orderId, QStringLiteral("结算"), false);
    });
    connect(m_paymentRefreshButton, &QPushButton::clicked, this, &ChargingPage::refresh);

    applyEmpty();
}

void ChargingPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_client->isLoggedIn())
        refresh();
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
                              const QJsonValue orderVal = data.value(QStringLiteral("order"));
                              if (orderVal.isObject())
                                  applyOrder(Order::fromJson(orderVal.toObject()));
                              else
                                  applyEmpty();
                          });
}

void ChargingPage::applyEmpty()
{
    m_hasOrder = false;
    m_elapsedTimer->stop();
    m_stack->setCurrentIndex(0);
}

void ChargingPage::applyOrder(const Order &order)
{
    m_order = order;
    m_hasOrder = true;
    const QString baseInfo = QStringLiteral("站点：%1\n电桩：%2 · 电价 ¥%3/kWh\n预约时间：%4")
                                 .arg(order.stationName, order.pileCode)
                                 .arg(order.unitPrice, 0, 'f', 2)
                                 .arg(formatDateTime(order.reservedAt));
    if (order.status == QLatin1String("reserved")) {
        m_elapsedTimer->stop();
        m_reservedInfo->setText(baseInfo);
        m_stack->setCurrentIndex(1);
    } else if (order.status == QLatin1String("charging")) {
        m_chargingInfo->setText(baseInfo + QStringLiteral("\n开始时间：%1")
                                             .arg(formatDateTime(order.startTime)));
        m_stack->setCurrentIndex(2);
        updateElapsed();
        m_elapsedTimer->start();
    } else if (order.status == QLatin1String("pending_payment")) {
        m_elapsedTimer->stop();
        m_paymentInfo->setText(QStringLiteral("站点：%1\n电桩：%2\n电量：%3 kWh · 单价 ¥%4/kWh\n金额：¥%5")
                                   .arg(order.stationName, order.pileCode)
                                   .arg(order.energyKwh, 0, 'f', 3)
                                   .arg(order.unitPrice, 0, 'f', 2)
                                   .arg(order.amount, 0, 'f', 2));
        fetchBalance();
        m_stack->setCurrentIndex(3);
    } else {
        applyEmpty();
    }
}

void ChargingPage::updateElapsed()
{
    const QDateTime start = m_order.startDateTime();
    if (!start.isValid())
        return;
    const qint64 secs = start.secsTo(QDateTime::currentDateTime());
    if (secs < 0)
        return;
    const qint64 h = secs / 3600;
    const qint64 m = (secs % 3600) / 60;
    const qint64 s = secs % 60;
    m_elapsedLabel->setText(QStringLiteral("%1:%2:%3")
                                .arg(h, 2, 10, QLatin1Char('0'))
                                .arg(m, 2, 10, QLatin1Char('0'))
                                .arg(s, 2, 10, QLatin1Char('0')));
}

void ChargingPage::fetchBalance()
{
    m_client->sendRequest(QStringLiteral("user_profile_get"), QJsonObject{},
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const UserInfo user = UserInfo::fromJson(
                                  data.value(QStringLiteral("user")).toObject());
                              m_paymentBalance->setText(QStringLiteral("当前余额：¥%1")
                                                            .arg(user.balance, 0, 'f', 2));
                          });
}

void ChargingPage::doAction(const QString &type, qint64 orderId, const QString &actionName, bool confirm)
{
    if (m_busy || !m_hasOrder)
        return;
    if (confirm) {
        const auto choice = QMessageBox::question(
            this, actionName, QStringLiteral("确定要%1订单 %2 吗？").arg(actionName).arg(orderId));
        if (choice != QMessageBox::Yes)
            return;
    }
    m_busy = true;
    m_startButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    m_stopButton->setEnabled(false);
    m_settleButton->setEnabled(false);

    m_client->sendRequest(type, QJsonObject{{QStringLiteral("orderId"), static_cast<double>(orderId)}},
                          [this, actionName](int code, const QString &msg, const QJsonObject &data) {
                              m_busy = false;
                              m_startButton->setEnabled(true);
                              m_cancelButton->setEnabled(true);
                              m_stopButton->setEnabled(true);
                              m_settleButton->setEnabled(true);
                              if (code == 0) {
                                  if (data.contains(QStringLiteral("order"))) {
                                      applyOrder(Order::fromJson(data.value(QStringLiteral("order")).toObject()));
                                  } else {
                                      refresh();
                                  }
                                  if (actionName == QLatin1String("结算")) {
                                      const double balance = data.value(QStringLiteral("balance")).toDouble();
                                      QMessageBox::information(this, actionName,
                                                               QStringLiteral("结算完成，余额 ¥%1").arg(balance, 0, 'f', 2));
                                      applyEmpty();
                                  } else if (actionName == QLatin1String("取消预约")) {
                                      QMessageBox::information(this, actionName, QStringLiteral("预约已取消"));
                                      applyEmpty();
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
