#include "mypage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "pages/passworddialog.h"
#include "pages/profileeditdialog.h"
#include "pages/rechargedialog.h"
#include "ui/avatarutils.h"
#include "ui/uienums.h"

namespace {
constexpr int kOrderPageSize = 10;
} // namespace

MyPage::MyPage(SocketClient *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("我的"), body);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    auto *profileCard = new QFrame(body);
    profileCard->setObjectName(QStringLiteral("card"));
    auto *profileLayout = new QVBoxLayout(profileCard);
    profileLayout->setContentsMargins(16, 16, 16, 16);
    profileLayout->setSpacing(8);
    auto *profileTitle = new QLabel(QStringLiteral("个人资料"), profileCard);
    profileTitle->setObjectName(QStringLiteral("cardTitle"));
    profileLayout->addWidget(profileTitle);

    auto *profileTopRow = new QHBoxLayout();
    m_avatarLabel = new QLabel(QStringLiteral("头像"), profileCard);
    m_avatarLabel->setObjectName(QStringLiteral("avatar"));
    m_avatarLabel->setFixedSize(64, 64);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    profileTopRow->addWidget(m_avatarLabel, 0);
    auto *nameColumn = new QVBoxLayout();
    m_nicknameLabel = new QLabel(profileCard);
    m_nicknameLabel->setObjectName(QStringLiteral("cardTitle"));
    nameColumn->addWidget(m_nicknameLabel);
    m_phoneLabel = new QLabel(profileCard);
    m_phoneLabel->setObjectName(QStringLiteral("hint"));
    nameColumn->addWidget(m_phoneLabel);
    profileTopRow->addLayout(nameColumn, 1);
    profileLayout->addLayout(profileTopRow);

    auto *profileButtons = new QHBoxLayout();
    m_editProfileButton = new QPushButton(QStringLiteral("编辑资料"), profileCard);
    m_editProfileButton->setProperty("class", QStringLiteral("smallPrimary"));
    m_passwordButton = new QPushButton(QStringLiteral("修改密码"), profileCard);
    m_passwordButton->setProperty("class", QStringLiteral("small"));
    profileButtons->addWidget(m_editProfileButton);
    profileButtons->addWidget(m_passwordButton);
    profileButtons->addStretch(1);
    profileLayout->addLayout(profileButtons);
    layout->addWidget(profileCard);

    auto *walletCard = new QFrame(body);
    walletCard->setObjectName(QStringLiteral("card"));
    auto *walletLayout = new QVBoxLayout(walletCard);
    walletLayout->setContentsMargins(16, 16, 16, 16);
    walletLayout->setSpacing(8);
    auto *walletTitle = new QLabel(QStringLiteral("钱包"), walletCard);
    walletTitle->setObjectName(QStringLiteral("cardTitle"));
    m_balanceLabel = new QLabel(QStringLiteral("¥0.00"), walletCard);
    m_balanceLabel->setObjectName(QStringLiteral("metric"));
    walletLayout->addWidget(walletTitle);
    walletLayout->addWidget(m_balanceLabel);
    auto *rechargeRow = new QHBoxLayout();
    auto *rechargeHint = new QLabel(QStringLiteral("支持预设档位与自定义金额"), walletCard);
    rechargeHint->setObjectName(QStringLiteral("hint"));
    m_rechargeButton = new QPushButton(QStringLiteral("充值"), walletCard);
    m_rechargeButton->setProperty("class", QStringLiteral("smallPrimary"));
    rechargeRow->addWidget(rechargeHint, 1);
    rechargeRow->addWidget(m_rechargeButton, 0);
    walletLayout->addLayout(rechargeRow);
    layout->addWidget(walletCard);

    auto *ordersCard = new QFrame(body);
    ordersCard->setObjectName(QStringLiteral("card"));
    auto *ordersCardLayout = new QVBoxLayout(ordersCard);
    ordersCardLayout->setContentsMargins(16, 16, 16, 16);
    ordersCardLayout->setSpacing(8);
    auto *ordersTitle = new QLabel(QStringLiteral("订单记录"), ordersCard);
    ordersTitle->setObjectName(QStringLiteral("cardTitle"));
    ordersCardLayout->addWidget(ordersTitle);

    m_ordersContainer = new QWidget(ordersCard);
    m_ordersLayout = new QVBoxLayout(m_ordersContainer);
    m_ordersLayout->setContentsMargins(0, 0, 0, 0);
    m_ordersLayout->setSpacing(8);
    m_ordersHint = new QLabel(QStringLiteral("暂无订单记录"), m_ordersContainer);
    m_ordersHint->setObjectName(QStringLiteral("hint"));
    m_ordersHint->setAlignment(Qt::AlignHCenter);
    m_ordersLayout->addWidget(m_ordersHint);
    ordersCardLayout->addWidget(m_ordersContainer);

    m_loadMoreButton = new QPushButton(QStringLiteral("加载更多"), ordersCard);
    m_loadMoreButton->setProperty("class", QStringLiteral("small"));
    m_loadMoreButton->setVisible(false);
    ordersCardLayout->addWidget(m_loadMoreButton);
    layout->addWidget(ordersCard);

    m_logoutButton = new QPushButton(QStringLiteral("退出登录"), body);
    layout->addWidget(m_logoutButton);
    layout->addStretch(1);

    scroll->setWidget(body);
    root->addWidget(scroll);

    connect(m_passwordButton, &QPushButton::clicked, this, [this]() {
        if (!m_hasUser)
            return;
        // hasPassword=false 时不显示原密码框，走首次设置语义
        PasswordDialog dlg(m_client, m_user.hasPassword, false, this);
        connect(&dlg, &PasswordDialog::passwordUpdated, this,
                [this](const QString &newPassword) {
                    m_client->setSessionPassword(newPassword);
                    refreshProfile();
                });
        dlg.exec();
    });
    connect(m_editProfileButton, &QPushButton::clicked, this, &MyPage::openProfileEdit);
    connect(m_rechargeButton, &QPushButton::clicked, this, &MyPage::openRechargeDialog);
    connect(m_loadMoreButton, &QPushButton::clicked, this, &MyPage::onLoadMoreOrders);
    connect(m_logoutButton, &QPushButton::clicked, this, [this]() {
        const auto choice = QMessageBox::question(this, QStringLiteral("退出登录"),
                                                  QStringLiteral("确定要退出当前账号吗？"));
        if (choice == QMessageBox::Yes)
            emit logoutRequested();
    });
}

void MyPage::setUser(const UserInfo &user)
{
    m_user = user;
    m_hasUser = true;
    renderProfile();
    resetOrders();
    onLoadMoreOrders();
}

void MyPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_hasUser && m_client->isLoggedIn()) {
        refreshProfile();
        reloadOrders();
    }
}

void MyPage::reloadOrders()
{
    if (m_loadingOrders || !m_client->isConnected())
        return;
    resetOrders();
    onLoadMoreOrders();
}

void MyPage::refreshProfile()
{
    if (!m_client->isConnected())
        return;
    m_client->sendRequest(QStringLiteral("user_profile_get"), QJsonObject{},
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              m_user = UserInfo::fromJson(data.value(QStringLiteral("user")).toObject());
                              m_hasUser = true;
                              renderProfile();
                          });
}

void MyPage::renderProfile()
{
    m_nicknameLabel->setText(m_user.nickname);
    m_phoneLabel->setText(QStringLiteral("手机号：%1 · 状态：%2")
                              .arg(m_user.phone, ui::userStatusText(m_user.status)));
    m_balanceLabel->setText(QStringLiteral("¥%1").arg(m_user.balance, 0, 'f', 2));
    emit balanceChanged(m_user.balance);
    if (m_user.hasAvatar) {
        QPixmap pix;
        if (pix.loadFromData(m_user.avatarBytes))
            m_avatarLabel->setPixmap(avatar::roundedAvatar(pix, 64));
    } else {
        m_avatarLabel->setPixmap(QPixmap());
        m_avatarLabel->setText(QStringLiteral("头像"));
    }
}

void MyPage::openProfileEdit()
{
    if (!m_hasUser)
        return;
    ProfileEditDialog dlg(m_client, m_user, this);
    connect(&dlg, &ProfileEditDialog::profileUpdated, this, [this](const UserInfo &user) {
        m_user = user;
        renderProfile();
    });
    dlg.exec();
}

void MyPage::openRechargeDialog()
{
    if (!m_client->isLoggedIn())
        return;
    RechargeDialog dlg(m_client, this);
    connect(&dlg, &RechargeDialog::recharged, this, [this](double newBalance) {
        m_user.balance = newBalance;
        m_balanceLabel->setText(QStringLiteral("¥%1").arg(m_user.balance, 0, 'f', 2));
        emit balanceChanged(m_user.balance);
    });
    dlg.exec();
}

void MyPage::resetOrders()
{
    while (QLayoutItem *item = m_ordersLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_ordersHint = new QLabel(QStringLiteral("暂无订单记录"), m_ordersContainer);
    m_ordersHint->setObjectName(QStringLiteral("hint"));
    m_ordersHint->setAlignment(Qt::AlignHCenter);
    m_ordersLayout->addWidget(m_ordersHint);
    m_nextPage = 1;
    m_loadMoreButton->setVisible(false);
}

void MyPage::onLoadMoreOrders()
{
    if (m_loadingOrders || !m_client->isConnected())
        return;
    m_loadingOrders = true;
    m_loadMoreButton->setEnabled(false);
    const int page = m_nextPage;
    m_client->sendRequest(QStringLiteral("user_order_list"),
                          QJsonObject{{QStringLiteral("page"), page},
                                      {QStringLiteral("pageSize"), kOrderPageSize}},
                          [this, page](int code, const QString &msg, const QJsonObject &data) {
                              m_loadingOrders = false;
                              m_loadMoreButton->setEnabled(true);
                              if (code != 0) {
                                  if (code > 0)
                                      QMessageBox::warning(this, QStringLiteral("订单记录"), msg);
                                  return;
                              }
                              QList<Order> orders;
                              const QJsonArray arr = data.value(QStringLiteral("orders")).toArray();
                              for (const QJsonValue &v : arr)
                                  orders.append(Order::fromJson(v.toObject()));
                              if (page == 1 && orders.isEmpty())
                                  return;
                              if (page == 1)
                                  m_ordersHint->deleteLater();
                              appendOrders(orders);
                              m_nextPage = page + 1;
                              const int total = data.value(QStringLiteral("total")).toInt();
                              const bool more = (page * kOrderPageSize) < total
                                  && orders.size() == kOrderPageSize;
                              m_loadMoreButton->setVisible(more);
                              if (!more && !orders.isEmpty()) {
                                  auto *endHint = new QLabel(QStringLiteral("没有更多了"), m_ordersContainer);
                                  endHint->setObjectName(QStringLiteral("hint"));
                                  endHint->setAlignment(Qt::AlignHCenter);
                                  m_ordersLayout->addWidget(endHint);
                              }
                          });
}

void MyPage::appendOrders(const QList<Order> &orders)
{
    for (const Order &o : orders) {
        auto *row = new QFrame(m_ordersContainer);
        row->setObjectName(QStringLiteral("card"));
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(4);

        auto *topRow = new QHBoxLayout();
        auto *name = new QLabel(QStringLiteral("%1 · %2").arg(o.stationName, o.pileCode), row);
        topRow->addWidget(name, 1);
        auto *status = new QLabel(ui::orderStatusText(o.status), row);
        ui::setState(status, o.status);
        topRow->addWidget(status, 0);
        rowLayout->addLayout(topRow);

        auto *timeLabel = new QLabel(formatDateTime(o.reservedAt), row);
        timeLabel->setObjectName(QStringLiteral("hint"));
        rowLayout->addWidget(timeLabel);

        if (o.hasEnergy || o.hasAmount) {
            QString detail;
            if (o.hasEnergy)
                detail += QStringLiteral("电量 %1 kWh").arg(o.energyKwh, 0, 'f', 3);
            if (o.hasEnergy && o.hasAmount)
                detail += QStringLiteral(" · ");
            if (o.hasAmount)
                detail += QStringLiteral("金额 ¥%1").arg(o.amount, 0, 'f', 2);
            rowLayout->addWidget(new QLabel(detail, row));
        }
        m_ordersLayout->addWidget(row);
    }
}
