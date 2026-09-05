#include "adminmanagedialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

#include "net/socketclient.h"

AdminManageDialog::AdminManageDialog(SocketClient *client, const QString &currentUsername,
                                     QWidget *parent)
    : QDialog(parent), m_client(client), m_currentUsername(currentUsername)
{
    setWindowTitle(QStringLiteral("管理员账号管理"));
    resize(420, 360);
    QVBoxLayout *root = new QVBoxLayout(this);

    root->addWidget(new QLabel(QStringLiteral("管理员账号无公开注册，仅可由已登录管理员新增或删除")));

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("tableAdmins"));
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("用户名")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    root->addWidget(m_table, 1);

    QHBoxLayout *actions = new QHBoxLayout;
    m_addBtn = new QPushButton(QStringLiteral("新增管理员"));
    m_addBtn->setObjectName(QStringLiteral("btnAddAdmin"));
    m_addBtn->setProperty("primary", true);
    actions->addWidget(m_addBtn);
    m_deleteBtn = new QPushButton(QStringLiteral("删除管理员"));
    m_deleteBtn->setObjectName(QStringLiteral("btnDeleteAdmin"));
    m_deleteBtn->setEnabled(false);
    actions->addWidget(m_deleteBtn);
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    actions->addWidget(refreshBtn);
    actions->addStretch();
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    actions->addWidget(buttons);
    root->addLayout(actions);

    connect(refreshBtn, &QPushButton::clicked, this, &AdminManageDialog::loadAdmins);
    connect(m_addBtn, &QPushButton::clicked, this, &AdminManageDialog::onAddAdmin);
    connect(m_deleteBtn, &QPushButton::clicked, this, &AdminManageDialog::onDeleteAdmin);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &AdminManageDialog::updateActionButtons);

    loadAdmins();
}

int AdminManageDialog::selectedRow() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return -1;
    return items.first()->row();
}

void AdminManageDialog::updateActionButtons()
{
    m_deleteBtn->setEnabled(selectedRow() >= 0);
}

void AdminManageDialog::loadAdmins()
{
    m_client->sendRequest(QStringLiteral("admin_list"), QJsonObject(),
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("管理员账号管理"), msg);
                                  return;
                              }
                              const QJsonArray admins = data[QStringLiteral("admins")].toArray();
                              m_table->setRowCount(admins.size());
                              for (int row = 0; row < admins.size(); ++row) {
                                  const QJsonObject a = admins.at(row).toObject();
                                  const int adminId = a[QStringLiteral("adminId")].toInt();
                                  const QString username = a[QStringLiteral("username")].toString();

                                  QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(adminId));
                                  idItem->setData(Qt::UserRole, adminId);
                                  m_table->setItem(row, 0, idItem);

                                  const bool isSelf = username == m_currentUsername;
                                  QTableWidgetItem *nameItem = new QTableWidgetItem(
                                      isSelf ? QStringLiteral("%1（本人）").arg(username) : username);
                                  nameItem->setData(Qt::UserRole, username);
                                  m_table->setItem(row, 1, nameItem);
                              }
                              updateActionButtons();
                          });
}

void AdminManageDialog::onAddAdmin()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增管理员"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *nameEdit = new QLineEdit;
    nameEdit->setMaxLength(20);
    QLineEdit *pwdEdit = new QLineEdit;
    pwdEdit->setEchoMode(QLineEdit::Password);
    QLineEdit *confirmEdit = new QLineEdit;
    confirmEdit->setEchoMode(QLineEdit::Password);

    form->addRow(QStringLiteral("用户名"), nameEdit);
    form->addRow(QStringLiteral("密码"), pwdEdit);
    form->addRow(QStringLiteral("确认密码"), confirmEdit);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString username = nameEdit->text().trimmed();
    const QString password = pwdEdit->text();
    if (username.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("新增管理员"), QStringLiteral("用户名不能为空"));
        return;
    }
    static const QRegularExpression ws(QStringLiteral("\\s"));
    if (password.size() < 6 || password.size() > 20 || ws.match(password).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("新增管理员"),
                             QStringLiteral("密码须为 6 至 20 位且不含空白字符"));
        return;
    }
    if (password != confirmEdit->text()) {
        QMessageBox::warning(this, QStringLiteral("新增管理员"), QStringLiteral("两次输入的密码不一致"));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("username")] = username;
    payload[QStringLiteral("password")] = password;

    m_addBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("admin_add"), payload,
                          [this](int code, const QString &msg, const QJsonObject &) {
                              m_addBtn->setEnabled(true);
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("新增管理员"),
                                                           QStringLiteral("新增成功"));
                                  loadAdmins();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("新增管理员失败"), msg);
                              }
                          });
}

void AdminManageDialog::onDeleteAdmin()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    const int adminId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString username = m_table->item(row, 1)->data(Qt::UserRole).toString();

    if (username == m_currentUsername) {
        QMessageBox::warning(this, QStringLiteral("删除管理员"),
                             QStringLiteral("不能删除当前登录的本人账号"));
        return;
    }

    const auto ret = QMessageBox::question(this, QStringLiteral("删除管理员"),
                                           QStringLiteral("确定要删除管理员「%1」吗？").arg(username));
    if (ret != QMessageBox::Yes)
        return;

    m_deleteBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("admin_delete"),
                          QJsonObject{{QStringLiteral("adminId"), adminId}},
                          [this](int code, const QString &msg, const QJsonObject &) {
                              updateActionButtons();
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("删除管理员"),
                                                           QStringLiteral("删除成功"));
                                  loadAdmins();
                              } else if (code == 3002) {
                                  QMessageBox::warning(this, QStringLiteral("删除管理员失败"),
                                                       QStringLiteral("不能删除当前登录的本人账号或最后一个管理员账号"));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("删除管理员失败"), msg);
                              }
                          });
}
