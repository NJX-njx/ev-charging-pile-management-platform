#include "userpage.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "uienums.h"

UserPage::UserPage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("手机号")));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(QStringLiteral("留空查询全部，支持模糊匹配"));
    m_searchEdit->setMaximumWidth(240);
    controls->addWidget(m_searchEdit);
    QPushButton *searchBtn = new QPushButton(QStringLiteral("查询"));
    controls->addWidget(searchBtn);
    controls->addStretch();
    m_toggleBtn = new QPushButton(QStringLiteral("冻结/解冻"));
    m_toggleBtn->setEnabled(false);
    controls->addWidget(m_toggleBtn);
    root->addLayout(controls);

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
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    connect(searchBtn, &QPushButton::clicked, this, [this]() {
        loadUsers(m_searchEdit->text().trimmed());
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        loadUsers(m_searchEdit->text().trimmed());
    });
    connect(m_toggleBtn, &QPushButton::clicked, this, &UserPage::onToggleStatus);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_toggleBtn->setEnabled(!m_table->selectedItems().isEmpty());
    });
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
                              m_toggleBtn->setEnabled(false);
                          });
}

void UserPage::onToggleStatus()
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return;
    const int row = items.first()->row();
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
