#include "stationpage.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "uienums.h"

StationPage::StationPage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("充电站列表")));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(QStringLiteral("按站名模糊搜索"));
    m_searchEdit->setMaximumWidth(240);
    controls->addWidget(m_searchEdit);
    QPushButton *searchBtn = new QPushButton(QStringLiteral("查询"));
    searchBtn->setProperty("primary", true);
    controls->addWidget(searchBtn);
    m_showDeletedCheck = new QCheckBox(QStringLiteral("显示已删除"));
    m_showDeletedCheck->setObjectName(QStringLiteral("checkShowDeleted"));
    controls->addWidget(m_showDeletedCheck);
    controls->addStretch();
    QPushButton *pilesBtn = new QPushButton(QStringLiteral("查看站内电桩"));
    controls->addWidget(pilesBtn);
    m_addBtn = new QPushButton(QStringLiteral("新增站点"));
    m_addBtn->setProperty("primary", true);
    controls->addWidget(m_addBtn);
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    controls->addWidget(refreshBtn);
    root->addLayout(controls);

    m_table = new QTableWidget;
    m_table->setColumnCount(10);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("站名"), QStringLiteral("地址"),
        QStringLiteral("经度"), QStringLiteral("纬度"), QStringLiteral("单价(元/度)"),
        QStringLiteral("总桩数"), QStringLiteral("在线率"), QStringLiteral("状态"),
        QStringLiteral("操作"),
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // 操作列固定宽度，避免 Stretch 均分时「修改」「删除」按钮文字被截断
    m_table->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Fixed);
    m_table->setColumnWidth(9, 170);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    root->addWidget(m_table, 1);

    QHBoxLayout *pager = new QHBoxLayout;
    pager->addStretch();
    m_prevBtn = new QPushButton(QStringLiteral("上一页"));
    m_prevBtn->setEnabled(false);
    pager->addWidget(m_prevBtn);
    m_pageLabel = new QLabel(QStringLiteral("第 1 / 1 页（共 0 条）"));
    pager->addWidget(m_pageLabel);
    m_nextBtn = new QPushButton(QStringLiteral("下一页"));
    m_nextBtn->setEnabled(false);
    pager->addWidget(m_nextBtn);
    pager->addStretch();
    root->addLayout(pager);

    connect(refreshBtn, &QPushButton::clicked, this, &StationPage::refresh);
    connect(m_showDeletedCheck, &QCheckBox::toggled, this, [this]() {
        m_page = 1;
        loadStations();
    });
    connect(m_addBtn, &QPushButton::clicked, this, &StationPage::onAddStation);
    connect(pilesBtn, &QPushButton::clicked, this, &StationPage::onShowPiles);
    connect(searchBtn, &QPushButton::clicked, this, [this]() {
        m_page = 1;
        loadStations();
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        m_page = 1;
        loadStations();
    });
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_page > 1) {
            --m_page;
            loadStations();
        }
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_page < totalPages()) {
            ++m_page;
            loadStations();
        }
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        onShowPiles();
    });
}

void StationPage::refresh()
{
    loadStations();
}

int StationPage::totalPages() const
{
    return qMax(1, (m_total + m_pageSize - 1) / m_pageSize);
}

void StationPage::updatePagination()
{
    m_pageLabel->setText(QStringLiteral("第 %1 / %2 页（共 %3 条）")
                             .arg(m_page)
                             .arg(totalPages())
                             .arg(m_total));
    m_prevBtn->setEnabled(m_page > 1);
    m_nextBtn->setEnabled(m_page < totalPages());
}

void StationPage::loadStations()
{
    QJsonObject payload;
    payload[QStringLiteral("page")] = m_page;
    payload[QStringLiteral("pageSize")] = m_pageSize;
    const QString keyword = m_searchEdit->text().trimmed();
    if (!keyword.isEmpty())
        payload[QStringLiteral("nameKeyword")] = keyword;
    if (m_showDeletedCheck->isChecked())
        payload[QStringLiteral("includeDeleted")] = true;

    m_client->sendRequest(QStringLiteral("station_list"), payload,
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              m_total = data[QStringLiteral("total")].toInt();
                              const QJsonArray stations = data[QStringLiteral("stations")].toArray();
                              // 删除后当前页可能超出范围，回退到最后一页重新拉取
                              if (stations.isEmpty() && m_total > 0 && m_page > totalPages()) {
                                  m_page = totalPages();
                                  loadStations();
                                  return;
                              }
                              const int previousStationId = selectedStationId();
                              m_table->setRowCount(stations.size());
                              for (int row = 0; row < stations.size(); ++row) {
                                  const QJsonObject s = stations.at(row).toObject();
                                  const bool deleted = s[QStringLiteral("deleted")].toBool();

                                  QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(s[QStringLiteral("stationId")].toInt()));
                                  idItem->setData(Qt::UserRole, s[QStringLiteral("stationId")].toInt());
                                  idItem->setData(Qt::UserRole + 1, deleted);
                                  m_table->setItem(row, 0, idItem);
                                  m_table->setItem(row, 1, new QTableWidgetItem(s[QStringLiteral("name")].toString()));
                                  m_table->setItem(row, 2, new QTableWidgetItem(s[QStringLiteral("address")].toString()));
                                  m_table->setItem(row, 3, new QTableWidgetItem(QString::number(s[QStringLiteral("lng")].toDouble(), 'f', 6)));
                                  m_table->setItem(row, 4, new QTableWidgetItem(QString::number(s[QStringLiteral("lat")].toDouble(), 'f', 6)));
                                  m_table->setItem(row, 5, new QTableWidgetItem(QString::number(s[QStringLiteral("pricePerKwh")].toDouble(), 'f', 2)));
                                  m_table->setItem(row, 6, new QTableWidgetItem(QString::number(s[QStringLiteral("pileTotal")].toInt())));
                                  m_table->setItem(row, 7, new QTableWidgetItem(QStringLiteral("%1%").arg(s[QStringLiteral("onlineRate")].toDouble() * 100, 0, 'f', 0)));

                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::recordStatusText(deleted));
                                  statusItem->setForeground(UiEnums::recordStatusColor(deleted));
                                  m_table->setItem(row, 8, statusItem);

                                  QWidget *ops = new QWidget;
                                  ops->setObjectName(QStringLiteral("stationRowOps"));
                                  QHBoxLayout *opsLayout = new QHBoxLayout(ops);
                                  opsLayout->setContentsMargins(4, 2, 4, 2);
                                  opsLayout->setSpacing(6);
                                  QPushButton *editBtn = new QPushButton(QStringLiteral("修改"));
                                  QPushButton *delBtn = new QPushButton(QStringLiteral("删除"));
                                  // 已删除记录仅用于历史查看，不作为修改/删除的操作对象
                                  if (deleted) {
                                      editBtn->setEnabled(false);
                                      delBtn->setEnabled(false);
                                      editBtn->setToolTip(QStringLiteral("已删除记录不可操作"));
                                      delBtn->setToolTip(QStringLiteral("已删除记录不可操作"));
                                  }
                                  opsLayout->addWidget(editBtn);
                                  opsLayout->addWidget(delBtn);
                                  m_table->setCellWidget(row, 9, ops);
                                  connect(editBtn, &QPushButton::clicked, this,
                                          [this, s, editBtn]() { onEditStation(s, editBtn); });
                                  connect(delBtn, &QPushButton::clicked, this,
                                          [this, s, delBtn]() { onDeleteStation(s, delBtn); });
                              }
                              // 重载后恢复选中：优先按 stationId 找回原行，否则选中第一行，
                              // 保证「查看站内电桩」始终有真实选中行可用
                              int targetRow = -1;
                              for (int row = 0; row < m_table->rowCount(); ++row) {
                                  if (m_table->item(row, 0)->data(Qt::UserRole).toInt() == previousStationId) {
                                      targetRow = row;
                                      break;
                                  }
                              }
                              if (targetRow < 0 && m_table->rowCount() > 0)
                                  targetRow = 0;
                              if (targetRow >= 0)
                                  m_table->selectRow(targetRow);
                              updatePagination();
                          });
}

int StationPage::selectedStationId() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return -1;
    return m_table->item(items.first()->row(), 0)->data(Qt::UserRole).toInt();
}

void StationPage::onEditStation(const QJsonObject &station, QPushButton *button)
{
    const int stationId = station[QStringLiteral("stationId")].toInt();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改站点"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *idEdit = new QLineEdit(QString::number(stationId));
    idEdit->setEnabled(false);
    QLineEdit *nameEdit = new QLineEdit(station[QStringLiteral("name")].toString());
    QLineEdit *addressEdit = new QLineEdit(station[QStringLiteral("address")].toString());
    QLineEdit *lngEdit = new QLineEdit(QString::number(station[QStringLiteral("lng")].toDouble(), 'f', 6));
    lngEdit->setEnabled(false);
    QLineEdit *latEdit = new QLineEdit(QString::number(station[QStringLiteral("lat")].toDouble(), 'f', 6));
    latEdit->setEnabled(false);
    QDoubleSpinBox *priceBox = new QDoubleSpinBox;
    priceBox->setRange(0.01, 99.99);
    priceBox->setDecimals(2);
    priceBox->setSingleStep(0.10);
    priceBox->setValue(station[QStringLiteral("pricePerKwh")].toDouble());

    form->addRow(QStringLiteral("站点ID"), idEdit);
    form->addRow(QStringLiteral("站名"), nameEdit);
    form->addRow(QStringLiteral("地址"), addressEdit);
    form->addRow(QStringLiteral("经度(不可修改)"), lngEdit);
    form->addRow(QStringLiteral("纬度(不可修改)"), latEdit);
    form->addRow(QStringLiteral("单价(元/度)"), priceBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    if (nameEdit->text().trimmed().isEmpty() || addressEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("修改站点"), QStringLiteral("站名、地址不能为空"));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("stationId")] = stationId;
    payload[QStringLiteral("name")] = nameEdit->text().trimmed();
    payload[QStringLiteral("address")] = addressEdit->text().trimmed();
    payload[QStringLiteral("pricePerKwh")] = priceBox->value();

    QPointer<QPushButton> guard(button);
    button->setEnabled(false);
    m_client->sendRequest(QStringLiteral("station_update"), payload,
                          [this, guard](int code, const QString &msg, const QJsonObject &) {
                              if (guard)
                                  guard->setEnabled(true);
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("修改站点"), QStringLiteral("保存成功"));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("修改站点失败"), msg);
                              }
                          });
}

void StationPage::onDeleteStation(const QJsonObject &station, QPushButton *button)
{
    const int stationId = station[QStringLiteral("stationId")].toInt();
    const QString name = station[QStringLiteral("name")].toString();

    const auto ret = QMessageBox::question(this, QStringLiteral("删除站点"),
                                           QStringLiteral("确定要删除站点「%1」吗？站内电桩将一并删除（历史订单保留）。").arg(name));
    if (ret != QMessageBox::Yes)
        return;

    QPointer<QPushButton> guard(button);
    button->setEnabled(false);
    m_client->sendRequest(QStringLiteral("station_delete"), QJsonObject{{QStringLiteral("stationId"), stationId}},
                          [this, guard](int code, const QString &msg, const QJsonObject &data) {
                              if (guard)
                                  guard->setEnabled(true);
                              if (code == 0) {
                                  const int removed = data[QStringLiteral("removedPileCount")].toInt();
                                  QMessageBox::information(this, QStringLiteral("删除站点"),
                                                           QStringLiteral("删除成功，已一并删除 %1 个电桩").arg(removed));
                                  refresh();
                              } else if (code == 3002) {
                                  QMessageBox::warning(this, QStringLiteral("删除站点失败"),
                                                       QStringLiteral("站内有占用中的电桩，无法删除"));
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("删除站点失败"), msg);
                              }
                          });
}

void StationPage::onShowPiles()
{
    const auto items = m_table->selectedItems();
    const int stationId = selectedStationId();
    if (stationId < 0) {
        QMessageBox::information(this, QStringLiteral("站内电桩"), QStringLiteral("请先选择一个充电站"));
        return;
    }
    // 已删除站点仅用于历史查看，station_detail 对其按不存在处理（协议 3.4）
    if (!items.isEmpty() && m_table->item(items.first()->row(), 0)->data(Qt::UserRole + 1).toBool()) {
        QMessageBox::information(this, QStringLiteral("站内电桩"), QStringLiteral("已删除站点仅用于历史数据查看"));
        return;
    }

    m_client->sendRequest(QStringLiteral("station_detail"), QJsonObject{{QStringLiteral("stationId"), stationId}},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("站内电桩"), msg);
                                  return;
                              }
                              const QJsonArray piles = data[QStringLiteral("piles")].toArray();
                              const QString stationName = data[QStringLiteral("station")].toObject()
                                                              [QStringLiteral("name")].toString();

                              QDialog dialog(this);
                              dialog.setWindowTitle(QStringLiteral("%1 - 站内电桩").arg(stationName));
                              dialog.resize(640, 360);
                              QVBoxLayout *layout = new QVBoxLayout(&dialog);
                              QTableWidget *table = new QTableWidget;
                              table->setColumnCount(4);
                              table->setHorizontalHeaderLabels({
                                  QStringLiteral("编号"), QStringLiteral("类型"),
                                  QStringLiteral("功率(kW)"), QStringLiteral("状态"),
                              });
                              table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
                              table->setEditTriggers(QAbstractItemView::NoEditTriggers);
                              table->verticalHeader()->setVisible(false);
                              table->setRowCount(piles.size());
                              for (int row = 0; row < piles.size(); ++row) {
                                  const QJsonObject p = piles.at(row).toObject();
                                  table->setItem(row, 0, new QTableWidgetItem(p[QStringLiteral("code")].toString()));
                                  table->setItem(row, 1, new QTableWidgetItem(UiEnums::pileTypeText(p[QStringLiteral("type")].toString())));
                                  table->setItem(row, 2, new QTableWidgetItem(QString::number(p[QStringLiteral("powerKw")].toDouble())));
                                  const QString status = p[QStringLiteral("status")].toString();
                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::pileStatusText(status));
                                  statusItem->setForeground(UiEnums::pileStatusColor(status));
                                  table->setItem(row, 3, statusItem);
                              }
                              layout->addWidget(table);
                              QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
                              connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                              layout->addWidget(buttons);
                              dialog.exec();
                          });
}

void StationPage::onAddStation()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增站点"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *nameEdit = new QLineEdit;
    QLineEdit *addressEdit = new QLineEdit;
    QLineEdit *lngEdit = new QLineEdit;
    lngEdit->setValidator(new QDoubleValidator(-180.0, 180.0, 6, lngEdit));
    QLineEdit *latEdit = new QLineEdit;
    latEdit->setValidator(new QDoubleValidator(-90.0, 90.0, 6, latEdit));
    QDoubleSpinBox *priceBox = new QDoubleSpinBox;
    priceBox->setRange(0.01, 99.99);
    priceBox->setDecimals(2);
    priceBox->setSingleStep(0.10);
    priceBox->setValue(1.20);
    QSpinBox *countBox = new QSpinBox;
    countBox->setRange(1, 100);
    countBox->setValue(5);

    form->addRow(QStringLiteral("站名"), nameEdit);
    form->addRow(QStringLiteral("地址"), addressEdit);
    form->addRow(QStringLiteral("经度"), lngEdit);
    form->addRow(QStringLiteral("纬度"), latEdit);
    form->addRow(QStringLiteral("单价(元/度)"), priceBox);
    form->addRow(QStringLiteral("电桩数量"), countBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    if (nameEdit->text().trimmed().isEmpty() || addressEdit->text().trimmed().isEmpty()
        || lngEdit->text().isEmpty() || latEdit->text().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("新增站点"), QStringLiteral("站名、地址、经度、纬度不能为空"));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("name")] = nameEdit->text().trimmed();
    payload[QStringLiteral("address")] = addressEdit->text().trimmed();
    payload[QStringLiteral("lng")] = lngEdit->text().toDouble();
    payload[QStringLiteral("lat")] = latEdit->text().toDouble();
    payload[QStringLiteral("pricePerKwh")] = priceBox->value();
    payload[QStringLiteral("pileCount")] = countBox->value();

    m_addBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("station_add"), payload,
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              m_addBtn->setEnabled(true);
                              if (code == 0) {
                                  const int stationId = data[QStringLiteral("station")].toObject()
                                                            [QStringLiteral("stationId")].toInt();
                                  const int created = data[QStringLiteral("createdPileCount")].toInt();
                                  QMessageBox::information(this, QStringLiteral("新增站点"),
                                                           QStringLiteral("创建成功，站点ID：%1，已生成 %2 个电桩").arg(stationId).arg(created));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("新增站点失败"), msg);
                              }
                          });
}
