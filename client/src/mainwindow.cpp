#include "mainwindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "pages/chargingpage.h"
#include "pages/findstationpage.h"
#include "pages/loginpage.h"
#include "pages/mypage.h"
#include "pages/passworddialog.h"

MainWindow::MainWindow(const AppConfig &config, QWidget *parent)
    : QMainWindow(parent)
    , m_config(config)
    , m_client(new SocketClient(this))
{
    setWindowTitle(QStringLiteral("电动汽车充电桩 - 用户端"));
    setFixedSize(460, 960);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    setCentralWidget(central);

    m_stack = new QStackedWidget(central);
    centralLayout->addWidget(m_stack);

    m_loginPage = new LoginPage(m_client, m_stack);
    m_loginPage->setServerConfig(m_config);
    m_stack->addWidget(m_loginPage);

    m_mainPage = new QWidget(m_stack);
    auto *mainLayout = new QVBoxLayout(m_mainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_banner = new QLabel(m_mainPage);
    m_banner->setObjectName(QStringLiteral("banner"));
    m_banner->setAlignment(Qt::AlignHCenter);
    m_banner->setVisible(false);
    mainLayout->addWidget(m_banner);

    m_tabStack = new QStackedWidget(m_mainPage);
    m_findPage = new FindStationPage(m_client, m_tabStack);
    m_chargingPage = new ChargingPage(m_client, m_tabStack);
    m_myPage = new MyPage(m_client, m_tabStack);
    m_tabStack->addWidget(m_findPage);
    m_tabStack->addWidget(m_chargingPage);
    m_tabStack->addWidget(m_myPage);
    mainLayout->addWidget(m_tabStack, 1);

    auto *tabBar = new QWidget(m_mainPage);
    tabBar->setObjectName(QStringLiteral("tabBar"));
    auto *tabLayout = new QHBoxLayout(tabBar);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);
    const QStringList tabNames{QStringLiteral("找站"), QStringLiteral("充电"), QStringLiteral("我的")};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto *btn = new QPushButton(tabNames.at(i), tabBar);
        btn->setObjectName(QStringLiteral("tabButton"));
        btn->setCheckable(true);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            switchTab(i);
        });
        m_tabButtons.append(btn);
        tabLayout->addWidget(btn, 1);
    }
    mainLayout->addWidget(tabBar);
    m_stack->addWidget(m_mainPage);
    m_stack->setCurrentWidget(m_loginPage);
    switchTab(0);

    connect(m_loginPage, &LoginPage::loginSuccess, this, [this](const UserInfo &user, bool) {
        m_myPage->setUser(user);
        m_findPage->setKnownBalance(user.balance);
        m_stack->setCurrentWidget(m_mainPage);
        switchTab(0);
        m_chargingPage->refresh();
        maybePromptSetPassword(user);
    });
    connect(m_loginPage, &LoginPage::configChanged, this, [this](const AppConfig &config) {
        m_config = config;
        m_config.save();
    });

    connect(m_client, &SocketClient::connectionLost, this, [this](const QString &reason) {
        if (m_stack->currentWidget() == m_mainPage)
            showBanner(QStringLiteral("连接已断开（%1），正在重连…").arg(reason));
    });
    connect(m_client, &SocketClient::connectFailed, this, [this](const QString &reason) {
        if (m_stack->currentWidget() == m_mainPage)
            showBanner(QStringLiteral("无法连接服务器（%1），正在重连…").arg(reason));
    });
    connect(m_client, &SocketClient::connected, this, [this]() {
        if (m_stack->currentWidget() == m_mainPage && !m_client->isLoggedIn())
            showBanner(QStringLiteral("已重新连接，正在恢复登录…"));
    });
    connect(m_client, &SocketClient::reloginFinished, this,
            [this](bool ok, const QString &msg, const QJsonObject &data) {
        hideBanner();
        if (ok) {
            m_chargingPage->refresh();
            m_myPage->refreshProfile();
            const UserInfo user =
                UserInfo::fromJson(data.value(QStringLiteral("user")).toObject());
            maybePromptSetPassword(user);
            return;
        }
        QMessageBox::warning(this, QStringLiteral("会话"),
                             msg.isEmpty() ? QStringLiteral("重新登录失败，请重新登录")
                                           : QStringLiteral("重新登录失败：%1").arg(msg));
        m_client->logout();
        m_stack->setCurrentWidget(m_loginPage);
    });
    connect(m_client, &SocketClient::protocolError, this, [this](const QString &detail) {
        showBanner(QStringLiteral("协议错误：%1").arg(detail));
        QTimer::singleShot(5000, this, &MainWindow::hideBanner);
    });

    connect(m_findPage, &FindStationPage::orderStateDirty, this, [this]() {
        switchTab(1);
        m_chargingPage->refresh();
    });
    connect(m_chargingPage, &ChargingPage::gotoFindStations, this, [this]() {
        switchTab(0);
    });
    connect(m_chargingPage, &ChargingPage::requestRecharge, this, [this]() {
        switchTab(2);
        m_myPage->openRechargeDialog();
    });
    connect(m_findPage, &FindStationPage::requestRecharge, this, [this]() {
        switchTab(2);
        m_myPage->openRechargeDialog();
    });
    connect(m_myPage, &MyPage::balanceChanged, m_findPage, &FindStationPage::setKnownBalance);
    connect(m_myPage, &MyPage::logoutRequested, this, [this]() {
        m_client->logout();
        m_stack->setCurrentWidget(m_loginPage);
    });
}

void MainWindow::switchTab(int index)
{
    m_tabStack->setCurrentIndex(index);
    for (int i = 0; i < m_tabButtons.size(); ++i)
        m_tabButtons.at(i)->setChecked(i == index);
}

void MainWindow::maybePromptSetPassword(const UserInfo &user)
{
    if (user.hasPassword)
        return;
    // 排队到事件循环，等登录成功的提示框与页面切换完成后再弹出
    QTimer::singleShot(0, this, [this]() {
        if (!m_client->isLoggedIn())
            return;
        PasswordDialog dlg(m_client, false, true, this);
        connect(&dlg, &PasswordDialog::passwordUpdated, this,
                [this](const QString &newPassword) {
                    m_client->setSessionPassword(newPassword);
                    m_myPage->refreshProfile();
                });
        dlg.exec();
    });
}

void MainWindow::showBanner(const QString &text)
{
    m_banner->setText(text);
    m_banner->setVisible(true);
}

void MainWindow::hideBanner()
{
    m_banner->setVisible(false);
}
