#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>

#include "net/socketclient.h"
#include "orderpage.h"
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
        QStringLiteral("订单管理"),
    };

    ui->listWidgetNav->addItems(modules);

    SalesPage *salesPage = new SalesPage(m_client);
    PileStatusPage *pileStatusPage = new PileStatusPage(m_client);
    PileManagePage *pileManagePage = new PileManagePage(m_client);
    StationPage *stationPage = new StationPage(m_client);
    UserPage *userPage = new UserPage(m_client);
    OrderPage *orderPage = new OrderPage(m_client);

    ui->stackedWidget->addWidget(salesPage);
    ui->stackedWidget->addWidget(pileStatusPage);
    ui->stackedWidget->addWidget(pileManagePage);
    ui->stackedWidget->addWidget(stationPage);
    ui->stackedWidget->addWidget(userPage);
    ui->stackedWidget->addWidget(orderPage);

    connect(ui->listWidgetNav, &QListWidget::currentRowChanged, this,
            [=](int row) {
                ui->stackedWidget->setCurrentIndex(row);
                refreshCurrentPage();
            });

    QMenu *accountMenu = menuBar()->addMenu(QStringLiteral("账号"));
    QAction *changePwdAction = accountMenu->addAction(QStringLiteral("修改密码"));
    connect(changePwdAction, &QAction::triggered, this, &MainWindow::onChangePassword);

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
    case 5: static_cast<OrderPage *>(ui->stackedWidget->widget(5))->refresh(); break;
    }
}

void MainWindow::onChangePassword()
{
    if (m_pwdUpdatePending)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改密码"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *oldEdit = new QLineEdit;
    oldEdit->setEchoMode(QLineEdit::Password);
    QLineEdit *newEdit = new QLineEdit;
    newEdit->setEchoMode(QLineEdit::Password);
    QLineEdit *confirmEdit = new QLineEdit;
    confirmEdit->setEchoMode(QLineEdit::Password);

    form->addRow(QStringLiteral("原密码"), oldEdit);
    form->addRow(QStringLiteral("新密码"), newEdit);
    form->addRow(QStringLiteral("确认新密码"), confirmEdit);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString oldPassword = oldEdit->text();
    const QString newPassword = newEdit->text();
    if (oldPassword.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("修改密码"), QStringLiteral("请输入原密码"));
        return;
    }
    static const QRegularExpression ws(QStringLiteral("\\s"));
    if (newPassword.size() < 6 || newPassword.size() > 20 || ws.match(newPassword).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("修改密码"), QStringLiteral("新密码须为 6 至 20 位且不含空白字符"));
        return;
    }
    if (newPassword != confirmEdit->text()) {
        QMessageBox::warning(this, QStringLiteral("修改密码"), QStringLiteral("两次输入的新密码不一致"));
        return;
    }
    if (newPassword == oldPassword) {
        QMessageBox::warning(this, QStringLiteral("修改密码"), QStringLiteral("新密码不能与原密码相同"));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("oldPassword")] = oldPassword;
    payload[QStringLiteral("newPassword")] = newPassword;

    m_pwdUpdatePending = true;
    m_client->sendRequest(QStringLiteral("admin_password_update"), payload,
                          [this, newPassword](int code, const QString &msg, const QJsonObject &) {
                              m_pwdUpdatePending = false;
                              if (code == 0) {
                                  // 断线重连需用新密码重新登录（协议 2.3）
                                  m_password = newPassword;
                                  QMessageBox::information(this, QStringLiteral("修改密码"),
                                                           QStringLiteral("密码修改成功，下次登录请使用新密码"));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("修改密码失败"), msg);
                              }
                          });
}
