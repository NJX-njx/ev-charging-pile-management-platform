#include "systempage.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
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

#include "filtertable.h"
#include "net/socketclient.h"

SystemPage::SystemPage(SocketClient *client, const QString &currentUsername, QWidget *parent)
    : QWidget(parent), m_client(client), m_currentUsername(currentUsername)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    root->addWidget(createAdminCard(), 1);
    root->addWidget(createSecurityCard());
    root->addStretch();

    connect(m_addBtn, &QPushButton::clicked, this, &SystemPage::onAddAdmin);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SystemPage::onDeleteAdmin);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &SystemPage::updateActionButtons);
    connect(m_changePwdBtn, &QPushButton::clicked, this, &SystemPage::onChangePassword);
}

QWidget *SystemPage::createAdminCard()
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout *layout = new QVBoxLayout(card);

    QLabel *title = new QLabel(QStringLiteral("管理员账号"));
    title->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(QStringLiteral("管理员账号无公开注册，仅可由已登录管理员新增或删除")));

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("tableAdmins"));
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("用户名")});
    // 先挂筛选排序表头，再配置列宽模式（setHorizontalHeader 会替换表头实例）
    m_ft = new FilterTable(m_table, this);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    layout->addWidget(m_table, 1);

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
    layout->addLayout(actions);

    connect(refreshBtn, &QPushButton::clicked, this, &SystemPage::loadAdmins);
    return card;
}

QWidget *SystemPage::createSecurityCard()
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout *layout = new QVBoxLayout(card);

    QLabel *title = new QLabel(QStringLiteral("安全"));
    title->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(title);

    QFormLayout *form = new QFormLayout;
    m_oldPwdEdit = new QLineEdit;
    m_oldPwdEdit->setObjectName(QStringLiteral("editOldPwd"));
    m_oldPwdEdit->setEchoMode(QLineEdit::Password);
    m_oldPwdEdit->setPlaceholderText(QStringLiteral("请输入当前使用的密码"));
    m_newPwdEdit = new QLineEdit;
    m_newPwdEdit->setObjectName(QStringLiteral("editNewPwd"));
    m_newPwdEdit->setEchoMode(QLineEdit::Password);
    m_newPwdEdit->setPlaceholderText(QStringLiteral("6 至 20 位，不含空白字符"));
    m_confirmPwdEdit = new QLineEdit;
    m_confirmPwdEdit->setObjectName(QStringLiteral("editNewPwd2"));
    m_confirmPwdEdit->setEchoMode(QLineEdit::Password);
    m_confirmPwdEdit->setPlaceholderText(QStringLiteral("再次输入新密码"));
    form->addRow(QStringLiteral("原密码"), m_oldPwdEdit);
    form->addRow(QStringLiteral("新密码"), m_newPwdEdit);
    form->addRow(QStringLiteral("确认新密码"), m_confirmPwdEdit);
    layout->addLayout(form);

    QHBoxLayout *actions = new QHBoxLayout;
    m_changePwdBtn = new QPushButton(QStringLiteral("修改密码"));
    m_changePwdBtn->setObjectName(QStringLiteral("btnChangePwd"));
    m_changePwdBtn->setProperty("primary", true);
    actions->addWidget(m_changePwdBtn);
    actions->addStretch();
    layout->addLayout(actions);
    return card;
}

void SystemPage::refresh()
{
    loadAdmins();
}

int SystemPage::selectedRow() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return -1;
    return items.first()->row();
}

void SystemPage::updateActionButtons()
{
    m_deleteBtn->setEnabled(selectedRow() >= 0);
}

void SystemPage::loadAdmins()
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
                              // 重载后重新应用排序/筛选（管理员列表无选中恢复，与原行为一致）
                              m_ft->apply();
                              updateActionButtons();
                          });
}

void SystemPage::onAddAdmin()
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

void SystemPage::onDeleteAdmin()
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

void SystemPage::onChangePassword()
{
    if (m_pwdUpdatePending)
        return;

    const QString oldPassword = m_oldPwdEdit->text();
    const QString newPassword = m_newPwdEdit->text();
    if (oldPassword.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("修改密码"), QStringLiteral("请输入原密码"));
        return;
    }
    static const QRegularExpression ws(QStringLiteral("\\s"));
    if (newPassword.size() < 6 || newPassword.size() > 20 || ws.match(newPassword).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("修改密码"), QStringLiteral("新密码须为 6 至 20 位且不含空白字符"));
        return;
    }
    if (newPassword != m_confirmPwdEdit->text()) {
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
    m_changePwdBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("admin_password_update"), payload,
                          [this, newPassword](int code, const QString &msg, const QJsonObject &) {
                              m_pwdUpdatePending = false;
                              m_changePwdBtn->setEnabled(true);
                              if (code == 0) {
                                  m_oldPwdEdit->clear();
                                  m_newPwdEdit->clear();
                                  m_confirmPwdEdit->clear();
                                  emit passwordChanged(newPassword);
                                  QMessageBox::information(this, QStringLiteral("修改密码"),
                                                           QStringLiteral("密码修改成功，下次登录请使用新密码"));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("修改密码失败"), msg);
                              }
                          });
}
