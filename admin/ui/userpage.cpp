#include "userpage.h"

#include <QDateTime>
#include <QDialog>
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
#include "uienums.h"

namespace {

bool isValidPhone(const QString &phone)
{
    static const QRegularExpression re(QStringLiteral("^1\\d{10}$"));
    return re.match(phone).hasMatch();
}

bool isValidPassword(const QString &password)
{
    if (password.size() < 6 || password.size() > 20)
        return false;
    static const QRegularExpression ws(QStringLiteral("\\s"));
    return !ws.match(password).hasMatch();
}

} // namespace

UserPage::UserPage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("手机号")));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(QStringLiteral("留空查询全部，支持模糊匹配"));
    m_searchEdit->setMaximumWidth(240);
    controls->addWidget(m_searchEdit);
    QPushButton *searchBtn = new QPushButton(QStringLiteral("查询"));
    searchBtn->setProperty("primary", true);
    controls->addWidget(searchBtn);
    controls->addStretch();
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    controls->addWidget(refreshBtn);
    root->addLayout(controls);

    QHBoxLayout *actions = new QHBoxLayout;
    actions->addStretch();
    m_addBtn = new QPushButton(QStringLiteral("新增用户"));
    m_addBtn->setProperty("primary", true);
    actions->addWidget(m_addBtn);
    m_editBtn = new QPushButton(QStringLiteral("修改"));
    m_editBtn->setEnabled(false);
    actions->addWidget(m_editBtn);
    m_resetPwdBtn = new QPushButton(QStringLiteral("重置密码"));
    m_resetPwdBtn->setEnabled(false);
    actions->addWidget(m_resetPwdBtn);
    m_deleteBtn = new QPushButton(QStringLiteral("删除用户"));
    m_deleteBtn->setEnabled(false);
    actions->addWidget(m_deleteBtn);
    m_toggleBtn = new QPushButton(QStringLiteral("冻结/解冻"));
    m_toggleBtn->setEnabled(false);
    actions->addWidget(m_toggleBtn);
    root->addLayout(actions);

    m_table = new QTableWidget;
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("用户ID"), QStringLiteral("手机号"), QStringLiteral("昵称"),
        QStringLiteral("余额(元)"), QStringLiteral("注册时间"), QStringLiteral("状态"),
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    root->addWidget(m_table, 1);

    connect(searchBtn, &QPushButton::clicked, this, [this]() {
        loadUsers(m_searchEdit->text().trimmed());
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        loadUsers(m_searchEdit->text().trimmed());
    });
    connect(refreshBtn, &QPushButton::clicked, this, &UserPage::refresh);
    connect(m_addBtn, &QPushButton::clicked, this, &UserPage::onAddUser);
    connect(m_editBtn, &QPushButton::clicked, this, &UserPage::onEditUser);
    connect(m_resetPwdBtn, &QPushButton::clicked, this, &UserPage::onResetPassword);
    connect(m_deleteBtn, &QPushButton::clicked, this, &UserPage::onDeleteUser);
    connect(m_toggleBtn, &QPushButton::clicked, this, &UserPage::onToggleStatus);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        const bool hasSelection = !m_table->selectedItems().isEmpty();
        m_editBtn->setEnabled(hasSelection);
        m_resetPwdBtn->setEnabled(hasSelection);
        m_deleteBtn->setEnabled(hasSelection);
        m_toggleBtn->setEnabled(hasSelection);
    });
}

int UserPage::selectedRow() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return -1;
    return items.first()->row();
}

void UserPage::refresh()
{
    loadUsers(m_searchEdit->text().trimmed());
}

void UserPage::loadUsers(const QString &phoneKeyword)
{
    m_client->sendRequest(QStringLiteral("user_list"), QJsonObject{{QStringLiteral("phoneKeyword"), phoneKeyword}},
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const QJsonArray users = data[QStringLiteral("users")].toArray();
                              m_table->setRowCount(users.size());
                              for (int row = 0; row < users.size(); ++row) {
                                  const QJsonObject u = users.at(row).toObject();

                                  QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(u[QStringLiteral("userId")].toInt()));
                                  idItem->setData(Qt::UserRole, u[QStringLiteral("userId")].toInt());
                                  m_table->setItem(row, 0, idItem);
                                  m_table->setItem(row, 1, new QTableWidgetItem(u[QStringLiteral("phone")].toString()));
                                  m_table->setItem(row, 2, new QTableWidgetItem(u[QStringLiteral("nickname")].toString()));
                                  m_table->setItem(row, 3, new QTableWidgetItem(QString::number(u[QStringLiteral("balance")].toDouble(), 'f', 2)));

                                  const QDateTime regTime = QDateTime::fromString(u[QStringLiteral("regTime")].toString(), Qt::ISODate);
                                  m_table->setItem(row, 4, new QTableWidgetItem(regTime.isValid()
                                                                                    ? regTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                                                                    : u[QStringLiteral("regTime")].toString()));

                                  const QString status = u[QStringLiteral("status")].toString();
                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::userStatusText(status));
                                  statusItem->setForeground(UiEnums::userStatusColor(status));
                                  statusItem->setData(Qt::UserRole, status);
                                  m_table->setItem(row, 5, statusItem);
                              }
                              m_editBtn->setEnabled(false);
                              m_resetPwdBtn->setEnabled(false);
                              m_deleteBtn->setEnabled(false);
                              m_toggleBtn->setEnabled(false);
                          });
}

void UserPage::onAddUser()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增用户"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *phoneEdit = new QLineEdit;
    phoneEdit->setPlaceholderText(QStringLiteral("11 位手机号，如 13800005678"));
    QLineEdit *nicknameEdit = new QLineEdit;
    nicknameEdit->setMaxLength(20);
    QLineEdit *passwordEdit = new QLineEdit(QStringLiteral("123456"));

    form->addRow(QStringLiteral("手机号"), phoneEdit);
    form->addRow(QStringLiteral("昵称"), nicknameEdit);
    form->addRow(QStringLiteral("初始密码"), passwordEdit);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString phone = phoneEdit->text().trimmed();
    const QString nickname = nicknameEdit->text().trimmed();
    const QString password = passwordEdit->text();
    if (!isValidPhone(phone)) {
        QMessageBox::warning(this, QStringLiteral("新增用户"), QStringLiteral("手机号格式不正确"));
        return;
    }
    if (nickname.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("新增用户"), QStringLiteral("昵称不能为空"));
        return;
    }
    if (!password.isEmpty() && !isValidPassword(password)) {
        QMessageBox::warning(this, QStringLiteral("新增用户"), QStringLiteral("密码须为 6 至 20 位且不含空白字符"));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("phone")] = phone;
    payload[QStringLiteral("nickname")] = nickname;
    if (!password.isEmpty())
        payload[QStringLiteral("password")] = password;

    m_addBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_add"), payload,
                          [this](int code, const QString &msg, const QJsonObject &) {
                              m_addBtn->setEnabled(true);
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("新增用户"), QStringLiteral("新增成功"));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("新增用户失败"), msg);
                              }
                          });
}

void UserPage::onEditUser()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    const int userId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString phone = m_table->item(row, 1)->text();
    const QString nickname = m_table->item(row, 2)->text();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改用户"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *idEdit = new QLineEdit(QString::number(userId));
    idEdit->setEnabled(false);
    QLineEdit *phoneEdit = new QLineEdit(phone);
    QLineEdit *nicknameEdit = new QLineEdit(nickname);
    nicknameEdit->setMaxLength(20);

    form->addRow(QStringLiteral("用户ID"), idEdit);
    form->addRow(QStringLiteral("手机号"), phoneEdit);
    form->addRow(QStringLiteral("昵称"), nicknameEdit);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString newPhone = phoneEdit->text().trimmed();
    const QString newNickname = nicknameEdit->text().trimmed();
    if (!isValidPhone(newPhone)) {
        QMessageBox::warning(this, QStringLiteral("修改用户"), QStringLiteral("手机号格式不正确"));
        return;
    }
    if (newNickname.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("修改用户"), QStringLiteral("昵称不能为空"));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("userId")] = userId;
    payload[QStringLiteral("phone")] = newPhone;
    payload[QStringLiteral("nickname")] = newNickname;

    m_editBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_update"), payload,
                          [this](int code, const QString &msg, const QJsonObject &) {
                              m_editBtn->setEnabled(true);
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("修改用户"), QStringLiteral("保存成功"));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("修改用户失败"), msg);
                              }
                          });
}

void UserPage::onResetPassword()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    const int userId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString phone = m_table->item(row, 1)->text();

    const auto ret = QMessageBox::question(this, QStringLiteral("重置密码"),
                                           QStringLiteral("确定要将用户 %1 的密码重置为初始密码吗？").arg(phone));
    if (ret != QMessageBox::Yes)
        return;

    m_resetPwdBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_reset_password"), QJsonObject{{QStringLiteral("userId"), userId}},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              m_resetPwdBtn->setEnabled(true);
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("重置密码"),
                                                           QStringLiteral("密码已重置为初始密码：%1\n请将初始密码告知用户。")
                                                               .arg(data[QStringLiteral("password")].toString()));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("重置密码失败"), msg);
                              }
                          });
}

void UserPage::onDeleteUser()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    const int userId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString phone = m_table->item(row, 1)->text();

    const auto ret = QMessageBox::question(this, QStringLiteral("删除用户"),
                                           QStringLiteral("确定要删除用户 %1 吗？（逻辑删除，历史订单保留）").arg(phone));
    if (ret != QMessageBox::Yes)
        return;

    m_deleteBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_delete"), QJsonObject{{QStringLiteral("userId"), userId}},
                          [this](int code, const QString &msg, const QJsonObject &) {
                              m_deleteBtn->setEnabled(true);
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("删除用户"), QStringLiteral("删除成功"));
                                  refresh();
                              } else if (code == 3002) {
                                  QMessageBox::warning(this, QStringLiteral("删除用户失败"),
                                                       QStringLiteral("该用户存在未完成订单，无法删除"));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("删除用户失败"), msg);
                              }
                          });
}

void UserPage::onToggleStatus()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    const int userId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString phone = m_table->item(row, 1)->text();
    const QString current = m_table->item(row, 5)->data(Qt::UserRole).toString();
    const QString target = current == QStringLiteral("frozen")
                               ? QStringLiteral("normal")
                               : QStringLiteral("frozen");
    const QString actionText = target == QStringLiteral("frozen")
                                   ? QStringLiteral("冻结")
                                   : QStringLiteral("解冻");

    const auto ret = QMessageBox::question(this, QStringLiteral("用户%1").arg(actionText),
                                           QStringLiteral("确定要%1用户 %2 吗？").arg(actionText, phone));
    if (ret != QMessageBox::Yes)
        return;

    m_toggleBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_set_status"),
                          QJsonObject{{QStringLiteral("userId"), userId},
                                      {QStringLiteral("status"), target}},
                          [this, actionText](int code, const QString &msg, const QJsonObject &) {
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("用户%1").arg(actionText),
                                                           QStringLiteral("%1成功").arg(actionText));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("用户%1失败").arg(actionText), msg);
                              }
                              refresh();
                          });
}
