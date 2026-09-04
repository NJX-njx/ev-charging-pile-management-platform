#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QLabel>

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
    for (const QString &name : modules) {
        QLabel *page = new QLabel(QStringLiteral("「%1」页面待开发").arg(name));
        page->setAlignment(Qt::AlignCenter);
        ui->stackedWidget->addWidget(page);
    }

    connect(ui->listWidgetNav, &QListWidget::currentRowChanged,
            ui->stackedWidget, &QStackedWidget::setCurrentIndex);
    ui->listWidgetNav->setCurrentRow(0);
}

MainWindow::~MainWindow()
{
    delete ui;
}
