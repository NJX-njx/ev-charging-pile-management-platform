#include "pilemanagepage.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
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

#include <memory>

#include "net/socketclient.h"
#include "uienums.h"

PileManagePage::PileManagePage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("选中电桩后可执行修改、删除；故障电桩可远程重启（模拟）")));
    controls->addStretch();
    m_addBtn = new QPushButton(QStringLiteral("新增电桩"));
    m_addBtn->setProperty("primary", true);
    controls->addWidget(m_addBtn);
    m_editBtn = new QPushButton(QStringLiteral("修改电桩"));
    m_editBtn->setEnabled(false);
    controls->addWidget(m_editBtn);
    m_deleteBtn = new QPushButton(QStringLiteral("删除电桩"));
    m_deleteBtn->setEnabled(false);
    controls->addWidget(m_deleteBtn);
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
    connect(m_addBtn, &QPushButton::clicked, this, &PileManagePage::onAddPile);
    connect(m_editBtn, &QPushButton::clicked, this, &PileManagePage::onEditPile);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PileManagePage::onDeletePile);
    connect(m_restartBtn, &QPushButton::clicked, this, &PileManagePage::onRestartClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &PileManagePage::updateActionButtons);
    // 兜底：点击当前行不产生选中变化信号时，也要保证该行被选中且按钮状态同步
    connect(m_table, &QTableWidget::clicked, this, [this](const QModelIndex &index) {
        if (index.isValid())
            m_table->selectRow(index.row());
        updateActionButtons();
    });
}

void PileManagePage::updateActionButtons()
{
    const auto items = m_table->selectedItems();
    const bool hasSelection = !items.isEmpty();
    bool canRestart = false;
    if (hasSelection) {
        const int row = items.first()->row();
        QTableWidgetItem *statusItem = m_table->item(row, 4);
        canRestart = statusItem && statusItem->data(Qt::UserRole).toString() == QStringLiteral("fault");
    }
    m_restartBtn->setEnabled(canRestart);
    m_editBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

int PileManagePage::selectedRow() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return -1;
    return items.first()->row();
}

void PileManagePage::refresh()
{
    m_client->sendRequest(QStringLiteral("pile_list"), QJsonObject{{QStringLiteral("stationId"), 0}},
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const int previousPileId = selectedRow() >= 0
                                                             ? m_table->item(selectedRow(), 0)->data(Qt::UserRole).toInt()
                                                             : -1;
                              const QJsonArray piles = data[QStringLiteral("piles")].toArray();
                              m_table->setRowCount(piles.size());
                              for (int row = 0; row < piles.size(); ++row) {
                                  const QJsonObject p = piles.at(row).toObject();

                                  QTableWidgetItem *codeItem = new QTableWidgetItem(p[QStringLiteral("code")].toString());
                                  codeItem->setData(Qt::UserRole, p[QStringLiteral("pileId")].toInt());
                                  m_table->setItem(row, 0, codeItem);
                                  m_table->setItem(row, 1, new QTableWidgetItem(p[QStringLiteral("stationName")].toString()));

                                  const QString type = p[QStringLiteral("type")].toString();
                                  QTableWidgetItem *typeItem = new QTableWidgetItem(UiEnums::pileTypeText(type));
                                  typeItem->setData(Qt::UserRole, type);
                                  m_table->setItem(row, 2, typeItem);

                                  const double powerKw = p[QStringLiteral("powerKw")].toDouble();
                                  QTableWidgetItem *powerItem = new QTableWidgetItem(QString::number(powerKw));
                                  powerItem->setData(Qt::UserRole, powerKw);
                                  m_table->setItem(row, 3, powerItem);

                                  const QString status = p[QStringLiteral("status")].toString();
                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::pileStatusText(status));
                                  statusItem->setForeground(UiEnums::pileStatusColor(status));
                                  statusItem->setData(Qt::UserRole, status);
                                  m_table->setItem(row, 4, statusItem);

                                  m_table->setItem(row, 5, new QTableWidgetItem(QString::number(p[QStringLiteral("chargeCount")].toInt())));
                                  m_table->setItem(row, 6, new QTableWidgetItem(QString::number(p[QStringLiteral("chargeMinutes")].toInt() / 60.0, 'f', 1)));
                              }
                              // 重载后恢复选中：优先按 pileId 找回原行，否则选中第一行，
                              // 保证始终存在真实选中行而非仅有当前行高亮
                              int targetRow = -1;
                              for (int row = 0; row < m_table->rowCount(); ++row) {
                                  if (m_table->item(row, 0)->data(Qt::UserRole).toInt() == previousPileId) {
                                      targetRow = row;
                                      break;
                                  }
                              }
                              if (targetRow < 0 && m_table->rowCount() > 0)
                                  targetRow = 0;
                              if (targetRow >= 0)
                                  m_table->selectRow(targetRow);
                              updateActionButtons();
                          });
}

void PileManagePage::loadAllStations(const std::function<void(bool, const QJsonArray &)> &done)
{
    // station_list 分页拉取全部站点，供新增电桩选择所属站点
    auto stations = std::make_shared<QJsonArray>();
    auto fetch = std::make_shared<std::function<void(int)>>();
    auto donePtr = std::make_shared<std::function<void(bool, const QJsonArray &)>>(done);
    *fetch = [this, stations, fetch, donePtr](int page) {
        QJsonObject payload;
        payload[QStringLiteral("page")] = page;
        payload[QStringLiteral("pageSize")] = 100;
        m_client->sendRequest(QStringLiteral("station_list"), payload,
                              [stations, fetch, donePtr, page](int code, const QString &, const QJsonObject &data) {
                                  if (code != 0) {
                                      (*donePtr)(false, QJsonArray());
                                      return;
                                  }
                                  const QJsonArray batch = data[QStringLiteral("stations")].toArray();
                                  for (const auto &v : batch)
                                      stations->append(v);
                                  const int total = data[QStringLiteral("total")].toInt();
                                  if (!batch.isEmpty() && stations->size() < total)
                                      (*fetch)(page + 1);
                                  else
                                      (*donePtr)(true, *stations);
                              });
    };
    (*fetch)(1);
}

void PileManagePage::onAddPile()
{
    m_addBtn->setEnabled(false);
    loadAllStations([this](bool ok, const QJsonArray &stations) {
        m_addBtn->setEnabled(true);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("新增电桩"), QStringLiteral("获取站点列表失败，请稍后重试"));
            return;
        }
        if (stations.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("新增电桩"), QStringLiteral("暂无站点，请先在站点管理中新增站点"));
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("新增电桩"));
        QFormLayout *form = new QFormLayout(&dialog);

        QComboBox *stationBox = new QComboBox;
        for (const auto &v : stations) {
            const QJsonObject s = v.toObject();
            stationBox->addItem(QStringLiteral("%1（ID:%2）")
                                    .arg(s[QStringLiteral("name")].toString())
                                    .arg(s[QStringLiteral("stationId")].toInt()),
                                s[QStringLiteral("stationId")].toInt());
        }
        QLineEdit *codeEdit = new QLineEdit;
        codeEdit->setMaxLength(20);
        codeEdit->setPlaceholderText(QStringLiteral("全局唯一，如 P-9001"));
        QComboBox *typeBox = new QComboBox;
        typeBox->addItem(QStringLiteral("快充"), QStringLiteral("fast"));
        typeBox->addItem(QStringLiteral("慢充"), QStringLiteral("slow"));
        QDoubleSpinBox *powerBox = new QDoubleSpinBox;
        powerBox->setRange(0.1, 1000.0);
        powerBox->setDecimals(1);
        powerBox->setValue(60.0);

        form->addRow(QStringLiteral("所属站点"), stationBox);
        form->addRow(QStringLiteral("电桩编号"), codeEdit);
        form->addRow(QStringLiteral("类型"), typeBox);
        form->addRow(QStringLiteral("功率(kW)"), powerBox);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);

        if (dialog.exec() != QDialog::Accepted)
            return;

        if (codeEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("新增电桩"), QStringLiteral("电桩编号不能为空"));
            return;
        }

        QJsonObject payload;
        payload[QStringLiteral("stationId")] = stationBox->currentData().toInt();
        payload[QStringLiteral("code")] = codeEdit->text().trimmed();
        payload[QStringLiteral("type")] = typeBox->currentData().toString();
        payload[QStringLiteral("powerKw")] = powerBox->value();

        m_addBtn->setEnabled(false);
        m_client->sendRequest(QStringLiteral("pile_add"), payload,
                              [this](int code, const QString &msg, const QJsonObject &) {
                                  m_addBtn->setEnabled(true);
                                  if (code == 0) {
                                      QMessageBox::information(this, QStringLiteral("新增电桩"), QStringLiteral("新增成功"));
                                      refresh();
                                  } else {
                                      QMessageBox::warning(this, QStringLiteral("新增电桩失败"), msg);
                                  }
                              });
    });
}

void PileManagePage::onEditPile()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    const int pileId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_table->item(row, 0)->text();
    const QString type = m_table->item(row, 2)->data(Qt::UserRole).toString();
    const double powerKw = m_table->item(row, 3)->data(Qt::UserRole).toDouble();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改电桩"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *codeEdit = new QLineEdit(code);
    codeEdit->setEnabled(false);
    QComboBox *typeBox = new QComboBox;
    typeBox->addItem(QStringLiteral("快充"), QStringLiteral("fast"));
    typeBox->addItem(QStringLiteral("慢充"), QStringLiteral("slow"));
    const int typeIndex = typeBox->findData(type);
    if (typeIndex >= 0)
        typeBox->setCurrentIndex(typeIndex);
    QDoubleSpinBox *powerBox = new QDoubleSpinBox;
    powerBox->setRange(0.1, 1000.0);
    powerBox->setDecimals(1);
    powerBox->setValue(powerKw);

    form->addRow(QStringLiteral("电桩编号(不可修改)"), codeEdit);
    form->addRow(QStringLiteral("类型"), typeBox);
    form->addRow(QStringLiteral("功率(kW)"), powerBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QJsonObject payload;
    payload[QStringLiteral("pileId")] = pileId;
    payload[QStringLiteral("type")] = typeBox->currentData().toString();
    payload[QStringLiteral("powerKw")] = powerBox->value();

    m_editBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("pile_update"), payload,
                          [this](int code, const QString &msg, const QJsonObject &) {
                              updateActionButtons();
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("修改电桩"), QStringLiteral("保存成功"));
                                  refresh();
                              } else if (code == 3002) {
                                  QMessageBox::warning(this, QStringLiteral("修改电桩失败"),
                                                       QStringLiteral("电桩正在使用中，无法修改"));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("修改电桩失败"), msg);
                              }
                          });
}

void PileManagePage::onDeletePile()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    const int pileId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_table->item(row, 0)->text();
    const QString status = m_table->item(row, 4)->data(Qt::UserRole).toString();

    if (status != QStringLiteral("idle")) {
        QMessageBox::warning(this, QStringLiteral("删除电桩"), QStringLiteral("仅空闲状态的电桩可删除"));
        return;
    }

    const auto ret = QMessageBox::question(this, QStringLiteral("删除电桩"),
                                           QStringLiteral("确定要删除电桩 %1 吗？（历史订单保留）").arg(code));
    if (ret != QMessageBox::Yes)
        return;

    m_deleteBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("pile_delete"), QJsonObject{{QStringLiteral("pileId"), pileId}},
                          [this](int code, const QString &msg, const QJsonObject &) {
                              updateActionButtons();
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("删除电桩"), QStringLiteral("删除成功"));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("删除电桩失败"), msg);
                              }
                          });
}

void PileManagePage::onRestartClicked()
{
    const int row = selectedRow();
    if (row < 0)
        return;
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
