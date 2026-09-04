#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QLabel>

#include "pilemanagepage.h"
#include "pilestatuspage.h"
#include "salespage.h"
#include "stationpage.h"
#include "userpage.h"

MainWindow::MainWindow(SocketClient *client, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_client(client)
{
    ui->setupUi(this);

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
                switch (row) {
                case 0: salesPage->refresh(); break;
                case 1: pileStatusPage->refresh(); break;
                case 2: pileManagePage->refresh(); break;
                case 3: stationPage->refresh(); break;
                case 4: userPage->refresh(); break;
                }
            });
    ui->listWidgetNav->setCurrentRow(0);
    salesPage->refresh();
}

MainWindow::~MainWindow()
{
    delete ui;
}
