#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QLabel>

#include "salespage.h"

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
    ui->stackedWidget->addWidget(salesPage);
    for (int i = 1; i < modules.size(); ++i) {
        QLabel *page = new QLabel(QStringLiteral("「%1」页面待开发").arg(modules.at(i)));
        page->setAlignment(Qt::AlignCenter);
        ui->stackedWidget->addWidget(page);
    }

    connect(ui->listWidgetNav, &QListWidget::currentRowChanged, this,
            [this, salesPage](int row) {
                ui->stackedWidget->setCurrentIndex(row);
                if (row == 0)
                    salesPage->refresh();
            });
    ui->listWidgetNav->setCurrentRow(0);
    salesPage->refresh();
}

MainWindow::~MainWindow()
{
    delete ui;
}
