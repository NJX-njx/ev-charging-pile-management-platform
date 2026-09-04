#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QJsonObject>
#include <QLabel>
#include <QStatusBar>

#include "net/socketclient.h"
#include "pilemanagepage.h"
#include "pilestatuspage.h"
#include "salespage.h"
#include "stationpage.h"
#include "userpage.h"

MainWindow::MainWindow(SocketClient *client, const QString &username, const QString &password,
                       QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_client(client),
      m_username(username), m_password(password)
{
    ui->setupUi(this);
    setMinimumSize(1120, 700);

    const QStringList modules = {
        QStringLiteral("销售业绩"),
        QStringLiteral("电桩状态"),
        QStringLiteral("充电桩管理"),
        QStringLiteral("站点管理"),
        QStringLiteral("用户管理"),
    };

    ui->listWidgetNav->addItems(modules);

    SalesPage *salesPage = new SalesPage(m_client);
    PileStatusPage *pileStatusPage = new PileStatusPage(m_client);
    PileManagePage *pileManagePage = new PileManagePage(m_client);
    StationPage *stationPage = new StationPage(m_client);
    UserPage *userPage = new UserPage(m_client);

    ui->stackedWidget->addWidget(salesPage);
    ui->stackedWidget->addWidget(pileStatusPage);
    ui->stackedWidget->addWidget(pileManagePage);
    ui->stackedWidget->addWidget(stationPage);
    ui->stackedWidget->addWidget(userPage);

    connect(ui->listWidgetNav, &QListWidget::currentRowChanged, this,
            [=](int row) {
                ui->stackedWidget->setCurrentIndex(row);
                refreshCurrentPage();
            });

    m_connLabel = new QLabel;
    statusBar()->addPermanentWidget(m_connLabel);

    connect(m_client, &SocketClient::requestError, this, [this](const QString &msg) {
        if (!msg.isEmpty())
            statusBar()->showMessage(msg, 5000);
    });
    connect(m_client, &SocketClient::disconnected, this, [this]() {
        m_connLabel->setText(QStringLiteral("服务器：已断开，正在重连…"));
    });
    connect(m_client, &SocketClient::connected, this, [this]() {
        // 断线重连后服务端会话已失效（协议 2.3），需要重新登录
        QJsonObject payload;
        payload[QStringLiteral("username")] = m_username;
        payload[QStringLiteral("password")] = m_password;
        m_client->sendRequest(QStringLiteral("admin_login"), payload,
                              [this](int code, const QString &, const QJsonObject &) {
                                  if (code == 0) {
                                      m_connLabel->setText(QStringLiteral("服务器：已连接"));
                                      refreshCurrentPage();
                                  } else {
                                      m_connLabel->setText(QStringLiteral("服务器：重新登录失败"));
                                  }
                              });
    });
    m_connLabel->setText(m_client->isConnected()
                             ? QStringLiteral("服务器：已连接")
                             : QStringLiteral("服务器：连接中…"));

    ui->listWidgetNav->setCurrentRow(0);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::refreshCurrentPage()
{
    switch (ui->stackedWidget->currentIndex()) {
    case 0: static_cast<SalesPage *>(ui->stackedWidget->widget(0))->refresh(); break;
    case 1: static_cast<PileStatusPage *>(ui->stackedWidget->widget(1))->refresh(); break;
    case 2: static_cast<PileManagePage *>(ui->stackedWidget->widget(2))->refresh(); break;
    case 3: static_cast<StationPage *>(ui->stackedWidget->widget(3))->refresh(); break;
    case 4: static_cast<UserPage *>(ui->stackedWidget->widget(4))->refresh(); break;
    }
}
