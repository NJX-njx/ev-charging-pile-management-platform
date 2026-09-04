#include "pilemanagepage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "uienums.h"

PileManagePage::PileManagePage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("选中故障电桩后可执行远程重启（模拟）")));
    controls->addStretch();
    m_restartBtn = new QPushButton(QStringLiteral("远程重启"));
    m_restartBtn->setEnabled(false);
    controls->addWidget(m_restartBtn);
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    controls->addWidget(refreshBtn);
    root->addLayout(controls);

    m_table = new QTableWidget;
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("编号"), QStringLiteral("所属电站"), QStringLiteral("类型"),
        QStringLiteral("功率(kW)"), QStringLiteral("当前状态"),
        QStringLiteral("累计充电次数"), QStringLiteral("累计充电时长(h)"),
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    root->addWidget(m_table, 1);

    connect(refreshBtn, &QPushButton::clicked, this, &PileManagePage::refresh);
    connect(m_restartBtn, &QPushButton::clicked, this, &PileManagePage::onRestartClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto items = m_table->selectedItems();
        bool canRestart = false;
        if (!items.isEmpty()) {
            const int row = items.first()->row();
            QTableWidgetItem *statusItem = m_table->item(row, 4);
            canRestart = statusItem && statusItem->data(Qt::UserRole).toString() == QStringLiteral("fault");
        }
        m_restartBtn->setEnabled(canRestart);
    });
}

void PileManagePage::refresh()
{
    m_client->sendRequest(QStringLiteral("pile_list"), QJsonObject{{QStringLiteral("stationId"), 0}},
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const QJsonArray piles = data[QStringLiteral("piles")].toArray();
                              m_table->setRowCount(piles.size());
                              for (int row = 0; row < piles.size(); ++row) {
                                  const QJsonObject p = piles.at(row).toObject();

                                  QTableWidgetItem *codeItem = new QTableWidgetItem(p[QStringLiteral("code")].toString());
                                  codeItem->setData(Qt::UserRole, p[QStringLiteral("pileId")].toInt());
                                  m_table->setItem(row, 0, codeItem);
                                  m_table->setItem(row, 1, new QTableWidgetItem(p[QStringLiteral("stationName")].toString()));
                                  m_table->setItem(row, 2, new QTableWidgetItem(UiEnums::pileTypeText(p[QStringLiteral("type")].toString())));
                                  m_table->setItem(row, 3, new QTableWidgetItem(QString::number(p[QStringLiteral("powerKw")].toInt())));

                                  const QString status = p[QStringLiteral("status")].toString();
                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::pileStatusText(status));
                                  statusItem->setForeground(UiEnums::pileStatusColor(status));
                                  statusItem->setData(Qt::UserRole, status);
                                  m_table->setItem(row, 4, statusItem);

                                  m_table->setItem(row, 5, new QTableWidgetItem(QString::number(p[QStringLiteral("chargeCount")].toInt())));
                                  m_table->setItem(row, 6, new QTableWidgetItem(QString::number(p[QStringLiteral("chargeMinutes")].toInt() / 60.0, 'f', 1)));
                              }
                              m_restartBtn->setEnabled(false);
                          });
}

void PileManagePage::onRestartClicked()
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return;
    const int row = items.first()->row();
    const int pileId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_table->item(row, 0)->text();

    const auto ret = QMessageBox::question(this, QStringLiteral("远程重启"),
                                           QStringLiteral("确定要重启电桩 %1 吗？").arg(code));
    if (ret != QMessageBox::Yes)
        return;

    m_restartBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("pile_restart"), QJsonObject{{QStringLiteral("pileId"), pileId}},
                          [this](int code, const QString &msg, const QJsonObject &) {
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("远程重启"), QStringLiteral("重启成功，电桩已恢复空闲"));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("远程重启失败"), msg);
                                  refresh();
                              }
                          });
}
