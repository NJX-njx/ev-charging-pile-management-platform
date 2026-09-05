#include "mypage.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "pages/passworddialog.h"
#include "ui/uienums.h"

namespace {
constexpr qint64 kMaxAvatarBytes = 512 * 1024;
constexpr int kOrderPageSize = 10;

QPixmap roundedAvatar(const QPixmap &src, int size)
{
    const QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, 8, 8);
    painter.setClipPath(path);
    const int x = (size - scaled.width()) / 2;
    const int y = (size - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);
    return out;
}

bool looksLikeJpegOrPng(const QByteArray &bytes, QString *mime)
{
    if (bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xFF
        && static_cast<unsigned char>(bytes[1]) == 0xD8
        && static_cast<unsigned char>(bytes[2]) == 0xFF) {
        *mime = QStringLiteral("image/jpeg");
        return true;
    }
    if (bytes.size() >= 8
        && static_cast<unsigned char>(bytes[0]) == 0x89
        && bytes.mid(1, 3) == "PNG"
        && static_cast<unsigned char>(bytes[4]) == 0x0D
        && static_cast<unsigned char>(bytes[5]) == 0x0A) {
        *mime = QStringLiteral("image/png");
        return true;
    }
    return false;
}
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
    m_nicknameEdit = new QLineEdit(profileCard);
    m_nicknameEdit->setMaxLength(20);
    m_nicknameEdit->setPlaceholderText(QStringLiteral("昵称（1-20 个字符）"));
    nameColumn->addWidget(m_nicknameEdit);
    m_phoneLabel = new QLabel(profileCard);
    m_phoneLabel->setObjectName(QStringLiteral("hint"));
    nameColumn->addWidget(m_phoneLabel);
    profileTopRow->addLayout(nameColumn, 1);
    profileLayout->addLayout(profileTopRow);

    auto *profileButtons = new QHBoxLayout();
    m_saveNicknameButton = new QPushButton(QStringLiteral("保存昵称"), profileCard);
    m_saveNicknameButton->setProperty("class", QStringLiteral("smallPrimary"));
    m_avatarButton = new QPushButton(QStringLiteral("更换头像"), profileCard);
    m_avatarButton->setProperty("class", QStringLiteral("small"));
    m_passwordButton = new QPushButton(QStringLiteral("修改密码"), profileCard);
    m_passwordButton->setProperty("class", QStringLiteral("small"));
    profileButtons->addWidget(m_saveNicknameButton);
    profileButtons->addWidget(m_avatarButton);
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
    m_amountEdit = new QLineEdit(walletCard);
    m_amountEdit->setPlaceholderText(QStringLiteral("充值金额（≤10000，最多两位小数）"));
    m_rechargeButton = new QPushButton(QStringLiteral("充值"), walletCard);
    m_rechargeButton->setProperty("class", QStringLiteral("smallPrimary"));
    rechargeRow->addWidget(m_amountEdit, 1);
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
    connect(m_saveNicknameButton, &QPushButton::clicked, this, &MyPage::onSaveNickname);
    connect(m_avatarButton, &QPushButton::clicked, this, &MyPage::onChangeAvatar);
    connect(m_rechargeButton, &QPushButton::clicked, this, &MyPage::onRecharge);
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
    if (m_hasUser && m_client->isLoggedIn())
        refreshProfile();
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
    m_phoneLabel->setText(QStringLiteral("手机号：%1 · 状态：%2")
                              .arg(m_user.phone, ui::userStatusText(m_user.status)));
    m_nicknameEdit->setText(m_user.nickname);
    m_balanceLabel->setText(QStringLiteral("¥%1").arg(m_user.balance, 0, 'f', 2));
    if (m_user.hasAvatar) {
        QPixmap pix;
        if (pix.loadFromData(m_user.avatarBytes))
            m_avatarLabel->setPixmap(roundedAvatar(pix, 64));
    } else {
        m_avatarLabel->setPixmap(QPixmap());
        m_avatarLabel->setText(QStringLiteral("头像"));
    }
}

void MyPage::onSaveNickname()
{
    const QString nickname = m_nicknameEdit->text().trimmed();
    if (nickname.isEmpty() || nickname.size() > 20) {
        QMessageBox::warning(this, QStringLiteral("保存昵称"),
                             QStringLiteral("昵称长度须为 1-20 个字符"));
        return;
    }
    if (nickname == m_user.nickname)
        return;
    m_saveNicknameButton->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_profile_update"),
                          QJsonObject{{QStringLiteral("nickname"), nickname}},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              m_saveNicknameButton->setEnabled(true);
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("保存昵称"),
                                                       msg.isEmpty() ? QStringLiteral("保存失败") : msg);
                                  return;
                              }
                              m_user = UserInfo::fromJson(data.value(QStringLiteral("user")).toObject());
                              renderProfile();
                              QMessageBox::information(this, QStringLiteral("保存昵称"), QStringLiteral("昵称已更新"));
                          });
}

void MyPage::onChangeAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择头像"), QString(),
        QStringLiteral("图片文件 (*.jpg *.jpeg *.png)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("更换头像"), QStringLiteral("无法读取所选文件"));
        return;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() > kMaxAvatarBytes) {
        QMessageBox::warning(this, QStringLiteral("更换头像"),
                             QStringLiteral("头像文件不得超过 512 KiB"));
        return;
    }
    QString mime;
    if (!looksLikeJpegOrPng(bytes, &mime)) {
        QMessageBox::warning(this, QStringLiteral("更换头像"),
                             QStringLiteral("头像仅支持 JPEG 或 PNG 格式"));
        return;
    }
    m_avatarButton->setEnabled(false);
    const QJsonObject avatar{{QStringLiteral("mime"), mime},
                             {QStringLiteral("base64"), QString::fromLatin1(bytes.toBase64())}};
    m_client->sendRequest(QStringLiteral("user_profile_update"),
                          QJsonObject{{QStringLiteral("avatar"), avatar}},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              m_avatarButton->setEnabled(true);
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("更换头像"),
                                                       msg.isEmpty() ? QStringLiteral("上传失败") : msg);
                                  return;
                              }
                              m_user = UserInfo::fromJson(data.value(QStringLiteral("user")).toObject());
                              renderProfile();
                              QMessageBox::information(this, QStringLiteral("更换头像"), QStringLiteral("头像已更新"));
                          });
}

void MyPage::onRecharge()
{
    static const QRegularExpression amountRe(QStringLiteral("^[0-9]+(\\.[0-9]{1,2})?$"));
    const QString text = m_amountEdit->text().trimmed();
    if (!amountRe.match(text).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("充值"),
                             QStringLiteral("金额格式不正确，最多两位小数"));
        return;
    }
    const double amount = text.toDouble();
    if (amount <= 0 || amount > 10000) {
        QMessageBox::warning(this, QStringLiteral("充值"),
                             QStringLiteral("充值金额须大于 0 且不超过 10000"));
        return;
    }
    m_rechargeButton->setEnabled(false);
    m_client->sendRequest(QStringLiteral("wallet_recharge"),
                          QJsonObject{{QStringLiteral("amount"), amount}},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              m_rechargeButton->setEnabled(true);
                              if (code == SocketClient::kErrConnectionLost) {
                                  QMessageBox::warning(this, QStringLiteral("充值"),
                                                       QStringLiteral("网络中断，充值结果未知，请刷新余额确认后再决定是否重新充值"));
                                  refreshProfile();
                                  return;
                              }
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("充值"),
                                                       msg.isEmpty() ? QStringLiteral("充值失败") : msg);
                                  return;
                              }
                              m_user.balance = data.value(QStringLiteral("balance")).toDouble();
                              m_balanceLabel->setText(QStringLiteral("¥%1").arg(m_user.balance, 0, 'f', 2));
                              m_amountEdit->clear();
                              QMessageBox::information(this, QStringLiteral("充值"),
                                                       QStringLiteral("充值成功，当前余额 ¥%1").arg(m_user.balance, 0, 'f', 2));
                          });
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
