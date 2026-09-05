#include "stationpilepage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <limits>
#include <memory>

#include "filtertable.h"
#include "net/socketclient.h"
#include "uienums.h"

StationPilePage::StationPilePage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    // 顶部：站点操作行（搜索/分页之外的站点级操作）
    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("站点")));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(QStringLiteral("按站名模糊搜索"));
    m_searchEdit->setMaximumWidth(240);
    controls->addWidget(m_searchEdit);
    QPushButton *searchBtn = new QPushButton(QStringLiteral("查询"));
    searchBtn->setProperty("primary", true);
    controls->addWidget(searchBtn);
    m_showDeletedStationsCheck = new QCheckBox(QStringLiteral("显示已删除"));
    m_showDeletedStationsCheck->setObjectName(QStringLiteral("checkShowDeletedStations"));
    controls->addWidget(m_showDeletedStationsCheck);
    controls->addStretch();
    m_addStationBtn = new QPushButton(QStringLiteral("新增站点"));
    m_addStationBtn->setProperty("primary", true);
    controls->addWidget(m_addStationBtn);
    m_importBtn = new QPushButton(QStringLiteral("导入站点"));
    m_importBtn->setObjectName(QStringLiteral("btnImportStations"));
    controls->addWidget(m_importBtn);
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    controls->addWidget(refreshBtn);
    root->addLayout(controls);

    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("stationPileSplitter"));
    splitter->setChildrenCollapsible(false);

    // 左侧：站点列表 + 分页
    QWidget *leftPanel = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_stationTable = new QTableWidget;
    m_stationTable->setObjectName(QStringLiteral("stationTable"));
    // 合并页宽度有限：经度/纬度只在修改对话框展示（不可改），不占用列表列
    m_stationTable->setColumnCount(8);
    m_stationTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("站名"), QStringLiteral("地址"),
        QStringLiteral("单价"), QStringLiteral("总桩数"), QStringLiteral("在线率"),
        QStringLiteral("状态"), QStringLiteral("操作"),
    });
    // 先挂筛选排序表头，再配置列宽模式（setHorizontalHeader 会替换表头实例）
    m_stationFt = new FilterTable(m_stationTable, this);
    m_stationFt->setExcludedColumns({7});
    m_stationFt->setScopeNote(QStringLiteral("站点为服务端分页，排序与筛选仅作用于当前页数据"));
    // 操作列控件排序时由工厂重建（Qt 单元格控件不可跨行搬运）
    m_stationFt->setCellWidgetFactory(7, [this](int row) { return createStationOps(row); });
    m_stationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // 窄窗口下允许列压缩省略显示（而不是挤出水平滚动条）
    m_stationTable->horizontalHeader()->setMinimumSectionSize(30);
    // ID 与操作列固定宽度，把空间留给站名/地址
    m_stationTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_stationTable->setColumnWidth(0, 50);
    m_stationTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    m_stationTable->setColumnWidth(7, 130);
    m_stationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stationTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stationTable->setAlternatingRowColors(true);
    m_stationTable->verticalHeader()->setVisible(false);
    m_stationTable->verticalHeader()->setDefaultSectionSize(36);
    leftLayout->addWidget(m_stationTable, 1);

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
    leftLayout->addLayout(pager);

    splitter->addWidget(leftPanel);

    // 右侧：当前站点的电桩列表
    QWidget *rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *pileInfo = new QHBoxLayout;
    m_currentStationLabel = new QLabel(QStringLiteral("当前站点：—"));
    m_currentStationLabel->setObjectName(QStringLiteral("labelCurrentStation"));
    pileInfo->addWidget(m_currentStationLabel);
    m_showDeletedPilesCheck = new QCheckBox(QStringLiteral("显示已删除"));
    m_showDeletedPilesCheck->setObjectName(QStringLiteral("checkShowDeletedPiles"));
    pileInfo->addWidget(m_showDeletedPilesCheck);
    pileInfo->addStretch();
    QPushButton *pileRefreshBtn = new QPushButton(QStringLiteral("刷新"));
    pileInfo->addWidget(pileRefreshBtn);
    rightLayout->addLayout(pileInfo);

    // 电桩操作按钮两行三列：窄窗口（最小 1280）下右侧分栏收窄时仍可完整显示
    QGridLayout *pileActions = new QGridLayout;
    m_addPileBtn = new QPushButton(QStringLiteral("新增电桩"));
    m_addPileBtn->setProperty("primary", true);
    m_addPileBtn->setEnabled(false);
    pileActions->addWidget(m_addPileBtn, 0, 0);
    m_editPileBtn = new QPushButton(QStringLiteral("修改电桩"));
    m_editPileBtn->setEnabled(false);
    pileActions->addWidget(m_editPileBtn, 0, 1);
    m_deletePileBtn = new QPushButton(QStringLiteral("删除电桩"));
    m_deletePileBtn->setEnabled(false);
    pileActions->addWidget(m_deletePileBtn, 0, 2);
    m_restartBtn = new QPushButton(QStringLiteral("远程重启"));
    m_restartBtn->setEnabled(false);
    pileActions->addWidget(m_restartBtn, 1, 0);
    m_disableBtn = new QPushButton(QStringLiteral("禁用"));
    m_disableBtn->setObjectName(QStringLiteral("btnDisablePile"));
    m_disableBtn->setEnabled(false);
    pileActions->addWidget(m_disableBtn, 1, 1);
    m_activeOrderBtn = new QPushButton(QStringLiteral("占用详情"));
    m_activeOrderBtn->setObjectName(QStringLiteral("btnActiveOrder"));
    m_activeOrderBtn->setEnabled(false);
    pileActions->addWidget(m_activeOrderBtn, 1, 2);
    pileActions->setColumnStretch(3, 1);
    rightLayout->addLayout(pileActions);

    m_pileTable = new QTableWidget;
    m_pileTable->setObjectName(QStringLiteral("pileTable"));
    m_pileTable->setColumnCount(6);
    // 合并页右侧宽度有限，列名从简（含义同原充电桩管理页）
    m_pileTable->setHorizontalHeaderLabels({
        QStringLiteral("编号"), QStringLiteral("类型"), QStringLiteral("功率"),
        QStringLiteral("状态"), QStringLiteral("次数"), QStringLiteral("时长(h)"),
    });
    m_pileFt = new FilterTable(m_pileTable, this);
    m_pileFt->setScopeNote(QStringLiteral("作用于当前选中站点的全部电桩（未分页）"));
    m_pileTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pileTable->horizontalHeader()->setMinimumSectionSize(30);
    m_pileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pileTable->setAlternatingRowColors(true);
    m_pileTable->verticalHeader()->setVisible(false);
    m_pileTable->verticalHeader()->setDefaultSectionSize(36);
    rightLayout->addWidget(m_pileTable, 1);

    splitter->addWidget(rightPanel);
    // 余量/缺口全部分配给右侧：窗口调小时左侧站点表保持可读，右侧吸收收窄
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({660, 540});
    root->addWidget(splitter, 1);

    // ---- 站点侧信号 ----
    connect(refreshBtn, &QPushButton::clicked, this, &StationPilePage::refresh);
    connect(m_showDeletedStationsCheck, &QCheckBox::toggled, this, [this]() {
        m_page = 1;
        loadStations();
    });
    connect(m_addStationBtn, &QPushButton::clicked, this, &StationPilePage::onAddStation);
    connect(m_importBtn, &QPushButton::clicked, this, &StationPilePage::onImportStations);
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
    // 选中站点变化 → 右侧电桩联动刷新（loadStations 内部统一触发，见 m_loadingStations）
    connect(m_stationTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!m_loadingStations)
            loadPiles();
    });

    // ---- 电桩侧信号 ----
    connect(pileRefreshBtn, &QPushButton::clicked, this, [this]() { loadPiles(true); });
    connect(m_showDeletedPilesCheck, &QCheckBox::toggled, this, [this]() { loadPiles(true); });
    connect(m_addPileBtn, &QPushButton::clicked, this, &StationPilePage::onAddPile);
    connect(m_editPileBtn, &QPushButton::clicked, this, &StationPilePage::onEditPile);
    connect(m_deletePileBtn, &QPushButton::clicked, this, &StationPilePage::onDeletePile);
    connect(m_restartBtn, &QPushButton::clicked, this, &StationPilePage::onRestartClicked);
    connect(m_disableBtn, &QPushButton::clicked, this, &StationPilePage::onDisableClicked);
    connect(m_activeOrderBtn, &QPushButton::clicked, this, &StationPilePage::onShowActiveOrder);
    connect(m_pileTable, &QTableWidget::itemSelectionChanged, this, &StationPilePage::updatePileActionButtons);
    // 在用行双击查看占用订单（已删除行仅历史查看，不响应）
    connect(m_pileTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *statusItem = m_pileTable->item(row, 3);
        QTableWidgetItem *codeItem = m_pileTable->item(row, 0);
        const bool deleted = codeItem && codeItem->data(Qt::UserRole + 1).toBool();
        if (!deleted && statusItem && statusItem->data(Qt::UserRole).toString() == QStringLiteral("in_use"))
            onShowActiveOrder();
    });
    // 兜底：点击当前行不产生选中变化信号时，也要保证该行被选中且按钮状态同步
    connect(m_pileTable, &QTableWidget::clicked, this, [this](const QModelIndex &index) {
        if (index.isValid())
            m_pileTable->selectRow(index.row());
        updatePileActionButtons();
    });
}

void StationPilePage::refresh()
{
    loadStations();
}

int StationPilePage::totalPages() const
{
    return qMax(1, (m_total + m_pageSize - 1) / m_pageSize);
}

void StationPilePage::updatePagination()
{
    m_pageLabel->setText(QStringLiteral("第 %1 / %2 页（共 %3 条）")
                             .arg(m_page)
                             .arg(totalPages())
                             .arg(m_total));
    m_prevBtn->setEnabled(m_page > 1);
    m_nextBtn->setEnabled(m_page < totalPages());
}

int StationPilePage::selectedStationId() const
{
    const auto items = m_stationTable->selectedItems();
    if (items.isEmpty())
        return -1;
    QTableWidgetItem *it = m_stationTable->item(items.first()->row(), 0);
    return it ? it->data(Qt::UserRole).toInt() : -1;
}

bool StationPilePage::selectedStationDeleted() const
{
    const auto items = m_stationTable->selectedItems();
    if (items.isEmpty())
        return false;
    QTableWidgetItem *it = m_stationTable->item(items.first()->row(), 0);
    return it && it->data(Qt::UserRole + 1).toBool();
}

void StationPilePage::loadStations()
{
    QJsonObject payload;
    payload[QStringLiteral("page")] = m_page;
    payload[QStringLiteral("pageSize")] = m_pageSize;
    const QString keyword = m_searchEdit->text().trimmed();
    if (!keyword.isEmpty())
        payload[QStringLiteral("nameKeyword")] = keyword;
    if (m_showDeletedStationsCheck->isChecked())
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
                              m_loadingStations = true;
                              m_stationTable->setRowCount(stations.size());
                              for (int row = 0; row < stations.size(); ++row) {
                                  const QJsonObject s = stations.at(row).toObject();
                                  const bool deleted = s[QStringLiteral("deleted")].toBool();

                                  QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(s[QStringLiteral("stationId")].toInt()));
                                  idItem->setData(Qt::UserRole, s[QStringLiteral("stationId")].toInt());
                                  idItem->setData(Qt::UserRole + 1, deleted);
                                  // 站点原始 JSON 随行存储，供操作列工厂重建「修改」「删除」按钮
                                  idItem->setData(Qt::UserRole + 2, QVariant::fromValue(s));
                                  m_stationTable->setItem(row, 0, idItem);
                                  // 合并页宽度有限，站名/地址可能省略显示，tooltip 给出完整文本
                                  QTableWidgetItem *nameItem = new QTableWidgetItem(s[QStringLiteral("name")].toString());
                                  nameItem->setToolTip(s[QStringLiteral("name")].toString());
                                  m_stationTable->setItem(row, 1, nameItem);
                                  QTableWidgetItem *addressItem = new QTableWidgetItem(s[QStringLiteral("address")].toString());
                                  addressItem->setToolTip(s[QStringLiteral("address")].toString());
                                  m_stationTable->setItem(row, 2, addressItem);
                                  m_stationTable->setItem(row, 3, new QTableWidgetItem(QString::number(s[QStringLiteral("pricePerKwh")].toDouble(), 'f', 2)));
                                  m_stationTable->setItem(row, 4, new QTableWidgetItem(QString::number(s[QStringLiteral("pileTotal")].toInt())));
                                  m_stationTable->setItem(row, 5, new QTableWidgetItem(QStringLiteral("%1%").arg(s[QStringLiteral("onlineRate")].toDouble() * 100, 0, 'f', 0)));

                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::recordStatusText(deleted));
                                  statusItem->setForeground(UiEnums::recordStatusColor(deleted));
                                  m_stationTable->setItem(row, 6, statusItem);

                                  m_stationTable->setCellWidget(row, 7, createStationOps(row));
                              }
                              // 重载后恢复选中：优先按 stationId 找回原行（跳过筛选隐藏行），
                              // 否则选中第一可见行，保证右侧电桩始终有真实选中站点可用
                              m_stationFt->apply();
                              const int targetRow = m_stationFt->rowToSelect(previousStationId);
                              if (targetRow >= 0)
                                  m_stationTable->selectRow(targetRow);
                              m_loadingStations = false;
                              updatePagination();
                              // 站点刷新统一带动电桩刷新（含选中行未变的场景）
                              loadPiles(true);
                          });
}

void StationPilePage::loadPiles(bool force)
{
    const int stationId = selectedStationId();
    m_stationDeleted = selectedStationDeleted();
    updatePileActionButtons();

    if (stationId < 0) {
        m_loadedStationId = -1;
        m_currentStationLabel->setText(QStringLiteral("当前站点：—（请在左侧选择站点）"));
        m_pileTable->setRowCount(0);
        return;
    }
    // 已删除站点仅用于历史查看（协议 3.4：station_detail/pile_list 对其按不存在处理）
    if (m_stationDeleted) {
        m_loadedStationId = -1;
        m_currentStationLabel->setText(QStringLiteral("当前站点：已删除，仅用于历史查看"));
        m_pileTable->setRowCount(0);
        return;
    }
    if (!force && stationId == m_loadedStationId)
        return;

    QJsonObject payload{{QStringLiteral("stationId"), stationId}};
    if (m_showDeletedPilesCheck->isChecked())
        payload[QStringLiteral("includeDeleted")] = true;
    m_client->sendRequest(QStringLiteral("pile_list"), payload,
                          [this, stationId](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              // 响应到达时选中站点已切换则丢弃，等待新选站点的响应
                              if (selectedStationId() != stationId)
                                  return;
                              m_loadedStationId = stationId;
                              const QString stationName = [this]() {
                                  const auto items = m_stationTable->selectedItems();
                                  return items.isEmpty() ? QString()
                                                         : m_stationTable->item(items.first()->row(), 1)->text();
                              }();
                              m_currentStationLabel->setText(QStringLiteral("当前站点：%1").arg(stationName));

                              const int previousPileId = [this]() {
                                  const int row = selectedPileRow();
                                  if (row < 0)
                                      return -1;
                                  QTableWidgetItem *it = m_pileTable->item(row, 0);
                                  return it ? it->data(Qt::UserRole).toInt() : -1;
                              }();
                              const QJsonArray piles = data[QStringLiteral("piles")].toArray();
                              m_pileTable->setRowCount(piles.size());
                              for (int row = 0; row < piles.size(); ++row) {
                                  const QJsonObject p = piles.at(row).toObject();
                                  const bool deleted = p[QStringLiteral("deleted")].toBool();

                                  QTableWidgetItem *codeItem = new QTableWidgetItem(p[QStringLiteral("code")].toString());
                                  codeItem->setData(Qt::UserRole, p[QStringLiteral("pileId")].toInt());
                                  codeItem->setData(Qt::UserRole + 1, deleted);
                                  m_pileTable->setItem(row, 0, codeItem);

                                  const QString type = p[QStringLiteral("type")].toString();
                                  QTableWidgetItem *typeItem = new QTableWidgetItem(UiEnums::pileTypeText(type));
                                  typeItem->setData(Qt::UserRole, type);
                                  m_pileTable->setItem(row, 1, typeItem);

                                  const double powerKw = p[QStringLiteral("powerKw")].toDouble();
                                  QTableWidgetItem *powerItem = new QTableWidgetItem(QString::number(powerKw));
                                  powerItem->setData(Qt::UserRole, powerKw);
                                  m_pileTable->setItem(row, 2, powerItem);

                                  const QString status = p[QStringLiteral("status")].toString();
                                  // 已删除电桩状态列固定显示「已删除」（色板文本-次色），原始状态保留在 UserRole
                                  QTableWidgetItem *statusItem = new QTableWidgetItem(
                                      deleted ? UiEnums::recordStatusText(true) : UiEnums::pileStatusText(status));
                                  statusItem->setForeground(deleted ? UiEnums::recordStatusColor(true)
                                                                    : UiEnums::pileStatusColor(status));
                                  statusItem->setData(Qt::UserRole, status);
                                  m_pileTable->setItem(row, 3, statusItem);

                                  m_pileTable->setItem(row, 4, new QTableWidgetItem(QString::number(p[QStringLiteral("chargeCount")].toInt())));
                                  m_pileTable->setItem(row, 5, new QTableWidgetItem(QString::number(p[QStringLiteral("chargeMinutes")].toInt() / 60.0, 'f', 1)));
                              }
                              // 重载后恢复选中：优先按 pileId 找回原行（跳过筛选隐藏行），否则选中第一可见行
                              m_pileFt->apply();
                              const int targetRow = m_pileFt->rowToSelect(previousPileId);
                              if (targetRow >= 0)
                                  m_pileTable->selectRow(targetRow);
                              updatePileActionButtons();
                          });
}

int StationPilePage::selectedPileRow() const
{
    const auto items = m_pileTable->selectedItems();
    if (items.isEmpty())
        return -1;
    return items.first()->row();
}

void StationPilePage::updatePileActionButtons()
{
    const auto items = m_pileTable->selectedItems();
    const bool hasSelection = !items.isEmpty();
    QString status;
    bool deleted = false;
    if (hasSelection) {
        const int row = items.first()->row();
        QTableWidgetItem *statusItem = m_pileTable->item(row, 3);
        status = statusItem ? statusItem->data(Qt::UserRole).toString() : QString();
        QTableWidgetItem *codeItem = m_pileTable->item(row, 0);
        deleted = codeItem && codeItem->data(Qt::UserRole + 1).toBool();
    }
    // 已删除记录仅用于历史查看，不作为修改/删除/重启/禁用/占用详情的操作对象
    // v2.3：远程重启支持 idle 与 fault；禁用仅支持 idle；占用详情仅 in_use
    m_restartBtn->setEnabled(hasSelection && !deleted
                             && (status == QStringLiteral("idle") || status == QStringLiteral("fault")));
    m_disableBtn->setEnabled(hasSelection && !deleted && status == QStringLiteral("idle"));
    m_activeOrderBtn->setEnabled(hasSelection && !deleted && status == QStringLiteral("in_use"));
    m_editPileBtn->setEnabled(hasSelection && !deleted);
    m_deletePileBtn->setEnabled(hasSelection && !deleted);
    // 新增电桩需要有未删除的选中站点作为默认所属站点
    m_addPileBtn->setEnabled(selectedStationId() >= 0 && !m_stationDeleted);
    const QString tip = deleted ? QStringLiteral("已删除记录不可操作") : QString();
    m_editPileBtn->setToolTip(tip);
    m_deletePileBtn->setToolTip(tip);
    m_restartBtn->setToolTip(tip);
    m_disableBtn->setToolTip(tip);
    m_activeOrderBtn->setToolTip(tip);
}

QWidget *StationPilePage::createStationOps(int row)
{
    QTableWidgetItem *idItem = m_stationTable->item(row, 0);
    if (!idItem)
        return nullptr;
    const QJsonObject s = idItem->data(Qt::UserRole + 2).toJsonObject();
    const bool deleted = idItem->data(Qt::UserRole + 1).toBool();

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
    connect(editBtn, &QPushButton::clicked, this,
            [this, s, editBtn]() { onEditStation(s, editBtn); });
    connect(delBtn, &QPushButton::clicked, this,
            [this, s, delBtn]() { onDeleteStation(s, delBtn); });
    return ops;
}

void StationPilePage::onEditStation(const QJsonObject &station, QPushButton *button)
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

void StationPilePage::onDeleteStation(const QJsonObject &station, QPushButton *button)
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

void StationPilePage::onAddStation()
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

    m_addStationBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("station_add"), payload,
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              m_addStationBtn->setEnabled(true);
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

void StationPilePage::onImportStations()
{
    if (m_importing) {
        QMessageBox::information(this, QStringLiteral("导入站点"), QStringLiteral("正在导入中，请稍候"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入站点"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty())
        return;
    importStationsFromFile(path);
}

void StationPilePage::importStationsFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("导入站点"),
                             QStringLiteral("无法打开文件：%1").arg(path));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        QMessageBox::warning(this, QStringLiteral("导入站点"),
                             QStringLiteral("文件格式非法：应为 JSON 数组，元素含 name/address/lng/lat/pricePerKwh/pileCount 字段"));
        return;
    }
    const QJsonArray items = doc.array();
    if (items.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导入站点"), QStringLiteral("文件为空数组，没有可导入的站点"));
        return;
    }

    // 逐条预校验（与服务端 station_add 参数规则一致，协议 7.8），通过后进入发送队列
    QList<QPair<QString, QJsonObject>> queue;
    QStringList failures;
    for (int i = 0; i < items.size(); ++i) {
        const QString label = QStringLiteral("第 %1 条").arg(i + 1);
        if (!items.at(i).isObject()) {
            failures << QStringLiteral("%1：不是 JSON 对象").arg(label);
            continue;
        }
        const QJsonObject obj = items.at(i).toObject();
        const QString name = obj[QStringLiteral("name")].toString().trimmed();
        const QString address = obj[QStringLiteral("address")].toString().trimmed();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double lng = obj[QStringLiteral("lng")].toDouble(nan);
        const double lat = obj[QStringLiteral("lat")].toDouble(nan);
        const double price = obj[QStringLiteral("pricePerKwh")].toDouble(nan);
        const double pileCount = obj[QStringLiteral("pileCount")].toDouble(nan);

        QString error;
        if (name.isEmpty() || address.isEmpty())
            error = QStringLiteral("name/address 不能为空");
        else if (std::isnan(lng) || lng < -180.0 || lng > 180.0 || std::isnan(lat) || lat < -90.0 || lat > 90.0)
            error = QStringLiteral("lng/lat 非法或超出范围");
        else if (std::isnan(price) || price <= 0)
            error = QStringLiteral("pricePerKwh 必须大于 0");
        else if (std::isnan(pileCount) || pileCount != std::floor(pileCount) || pileCount < 1 || pileCount > 100)
            error = QStringLiteral("pileCount 必须为 1 至 100 的整数");

        if (!error.isEmpty()) {
            failures << QStringLiteral("%1（%2）：%3").arg(label).arg(name.isEmpty() ? QStringLiteral("未命名") : name).arg(error);
            continue;
        }

        QJsonObject payload;
        payload[QStringLiteral("name")] = name;
        payload[QStringLiteral("address")] = address;
        payload[QStringLiteral("lng")] = lng;
        payload[QStringLiteral("lat")] = lat;
        payload[QStringLiteral("pricePerKwh")] = price;
        payload[QStringLiteral("pileCount")] = static_cast<int>(pileCount);
        queue.append(QPair<QString, QJsonObject>(name, payload));
    }

    const int total = queue.size();
    if (total == 0) {
        QMessageBox::warning(this, QStringLiteral("导入站点"),
                             QStringLiteral("没有可导入的有效站点：\n%1").arg(failures.join(QLatin1Char('\n'))));
        return;
    }

    // 顺序逐条 station_add，避免并发压垮服务端；进度条可取消
    m_importing = true;
    m_importBtn->setEnabled(false);

    QProgressDialog *progress = new QProgressDialog(this);
    progress->setWindowTitle(QStringLiteral("导入站点"));
    progress->setRange(0, total);
    progress->setValue(0);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->show();

    auto queuePtr = std::make_shared<QList<QPair<QString, QJsonObject>>>(queue);
    auto failuresPtr = std::make_shared<QStringList>(failures);
    auto successPtr = std::make_shared<int>(0);
    auto sendNext = std::make_shared<std::function<void()>>();
    const QPointer<QProgressDialog> progressGuard(progress);

    *sendNext = [=]() {
        const bool canceled = !progressGuard || progressGuard->wasCanceled();
        if (queuePtr->isEmpty() || canceled) {
            if (progressGuard)
                progressGuard->close();
            const int succeeded = *successPtr;
            const int notImported = queuePtr->size();
            const QStringList failed = *failuresPtr;
            m_importing = false;
            m_importBtn->setEnabled(true);
            refresh();
            QString summary = canceled && notImported > 0
                                  ? QStringLiteral("导入已取消：成功 %1 条，失败 %2 条，剩余 %3 条未导入")
                                        .arg(succeeded)
                                        .arg(failed.size())
                                        .arg(notImported)
                                  : QStringLiteral("导入完成：成功 %1 条，失败 %2 条").arg(succeeded).arg(failed.size());
            if (!failed.isEmpty())
                summary += QStringLiteral("\n\n失败明细：\n") + failed.join(QLatin1Char('\n'));
            QMessageBox::information(this, QStringLiteral("导入站点"), summary);
            return;
        }

        const int index = total - queuePtr->size() + 1;
        const auto entry = queuePtr->takeFirst();
        progressGuard->setLabelText(QStringLiteral("正在导入第 %1/%2 条：%3").arg(index).arg(total).arg(entry.first));

        m_client->sendRequest(QStringLiteral("station_add"), entry.second,
                              [=](int code, const QString &msg, const QJsonObject &) {
                                  if (progressGuard)
                                      progressGuard->setValue(index);
                                  if (code == 0)
                                      ++(*successPtr);
                                  else
                                      failuresPtr->append(QStringLiteral("第 %1 条（%2）：%3").arg(index).arg(entry.first).arg(msg));
                                  (*sendNext)();
                              });
    };
    (*sendNext)();
}

// ---------------- 电桩操作（作用于右侧当前站点电桩列表的选中行） ----------------

void StationPilePage::loadAllStations(const std::function<void(bool, const QJsonArray &)> &done)
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

void StationPilePage::onAddPile()
{
    const int currentStationId = selectedStationId();
    m_addPileBtn->setEnabled(false);
    loadAllStations([this, currentStationId](bool ok, const QJsonArray &stations) {
        updatePileActionButtons();
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("新增电桩"), QStringLiteral("获取站点列表失败，请稍后重试"));
            return;
        }
        if (stations.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("新增电桩"), QStringLiteral("暂无站点，请先新增站点"));
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
        // 默认归属当前选中的站点
        const int currentIndex = stationBox->findData(currentStationId);
        if (currentIndex >= 0)
            stationBox->setCurrentIndex(currentIndex);
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

        m_addPileBtn->setEnabled(false);
        m_client->sendRequest(QStringLiteral("pile_add"), payload,
                              [this](int code, const QString &msg, const QJsonObject &) {
                                  updatePileActionButtons();
                                  if (code == 0) {
                                      QMessageBox::information(this, QStringLiteral("新增电桩"), QStringLiteral("新增成功"));
                                      refresh();
                                  } else {
                                      QMessageBox::warning(this, QStringLiteral("新增电桩失败"), msg);
                                  }
                              });
    });
}

void StationPilePage::onEditPile()
{
    const int row = selectedPileRow();
    if (row < 0)
        return;
    const int pileId = m_pileTable->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_pileTable->item(row, 0)->text();
    const QString type = m_pileTable->item(row, 1)->data(Qt::UserRole).toString();
    const double powerKw = m_pileTable->item(row, 2)->data(Qt::UserRole).toDouble();

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

    m_editPileBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("pile_update"), payload,
                          [this](int code, const QString &msg, const QJsonObject &) {
                              updatePileActionButtons();
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

void StationPilePage::onDeletePile()
{
    const int row = selectedPileRow();
    if (row < 0)
        return;
    const int pileId = m_pileTable->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_pileTable->item(row, 0)->text();
    const QString status = m_pileTable->item(row, 3)->data(Qt::UserRole).toString();

    if (status != QStringLiteral("idle")) {
        QMessageBox::warning(this, QStringLiteral("删除电桩"), QStringLiteral("仅空闲状态的电桩可删除"));
        return;
    }

    const auto ret = QMessageBox::question(this, QStringLiteral("删除电桩"),
                                           QStringLiteral("确定要删除电桩 %1 吗？（历史订单保留）").arg(code));
    if (ret != QMessageBox::Yes)
        return;

    m_deletePileBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("pile_delete"), QJsonObject{{QStringLiteral("pileId"), pileId}},
                          [this](int code, const QString &msg, const QJsonObject &) {
                              updatePileActionButtons();
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("删除电桩"), QStringLiteral("删除成功"));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("删除电桩失败"), msg);
                              }
                          });
}

void StationPilePage::onRestartClicked()
{
    const int row = selectedPileRow();
    if (row < 0)
        return;
    const int pileId = m_pileTable->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_pileTable->item(row, 0)->text();

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

void StationPilePage::onDisableClicked()
{
    const int row = selectedPileRow();
    if (row < 0)
        return;
    const int pileId = m_pileTable->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_pileTable->item(row, 0)->text();
    const QString status = m_pileTable->item(row, 3)->data(Qt::UserRole).toString();
    if (status != QStringLiteral("idle")) {
        QMessageBox::warning(this, QStringLiteral("禁用电桩"), QStringLiteral("仅空闲状态的电桩可禁用"));
        return;
    }

    const auto ret = QMessageBox::question(this, QStringLiteral("禁用电桩"),
                                           QStringLiteral("确定要禁用（停用下线）电桩 %1 吗？禁用后状态变为故障，恢复需远程重启。").arg(code));
    if (ret != QMessageBox::Yes)
        return;

    m_disableBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("pile_disable"), QJsonObject{{QStringLiteral("pileId"), pileId}},
                          [this](int code, const QString &msg, const QJsonObject &) {
                              if (code == 0) {
                                  QMessageBox::information(this, QStringLiteral("禁用电桩"), QStringLiteral("禁用成功，电桩已停用下线"));
                                  refresh();
                              } else {
                                  QMessageBox::warning(this, QStringLiteral("禁用电桩失败"), msg);
                                  refresh();
                              }
                          });
}

void StationPilePage::onShowActiveOrder()
{
    const int row = selectedPileRow();
    if (row < 0)
        return;
    const int pileId = m_pileTable->item(row, 0)->data(Qt::UserRole).toInt();
    const QString code = m_pileTable->item(row, 0)->text();

    m_activeOrderBtn->setEnabled(false);
    m_client->sendRequest(QStringLiteral("pile_active_order"), QJsonObject{{QStringLiteral("pileId"), pileId}},
                          [this, code](int respCode, const QString &msg, const QJsonObject &data) {
                              updatePileActionButtons();
                              if (respCode != 0) {
                                  QMessageBox::warning(this, QStringLiteral("占用详情"), msg);
                                  return;
                              }
                              const QJsonObject order = data[QStringLiteral("order")].toObject();
                              if (order.isEmpty()) {
                                  QMessageBox::information(this, QStringLiteral("占用详情"),
                                                           QStringLiteral("电桩 %1 当前没有占用订单").arg(code));
                                  return;
                              }

                              QDialog dialog(this);
                              dialog.setWindowTitle(QStringLiteral("电桩 %1 - 占用订单").arg(code));
                              QFormLayout *form = new QFormLayout(&dialog);

                              const int orderId = order[QStringLiteral("orderId")].toInt();
                              const QString status = order[QStringLiteral("status")].toString();
                              const QDateTime reservedAt = QDateTime::fromString(
                                  order[QStringLiteral("reservedAt")].toString(), Qt::ISODate);
                              const QDateTime startTime = QDateTime::fromString(
                                  order[QStringLiteral("startTime")].toString(), Qt::ISODate);

                              QString durationText = QStringLiteral("—（未开始充电）");
                              if (startTime.isValid()) {
                                  const qint64 minutes = startTime.secsTo(QDateTime::currentDateTime()) / 60;
                                  durationText = minutes >= 60
                                                     ? QStringLiteral("%1 小时 %2 分钟").arg(minutes / 60).arg(minutes % 60)
                                                     : QStringLiteral("%1 分钟").arg(minutes);
                              }

                              QLabel *statusLabel = new QLabel(UiEnums::orderStatusText(status));
                              QPalette statusPalette = statusLabel->palette();
                              statusPalette.setColor(QPalette::WindowText, UiEnums::orderStatusColor(status));
                              statusLabel->setPalette(statusPalette);
                              form->addRow(QStringLiteral("订单号"), new QLabel(QString::number(orderId)));
                              form->addRow(QStringLiteral("用户手机号"), new QLabel(order[QStringLiteral("userPhone")].toString()));
                              form->addRow(QStringLiteral("状态"), statusLabel);
                              form->addRow(QStringLiteral("预约时间"), new QLabel(reservedAt.isValid() ? reservedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("—")));
                              form->addRow(QStringLiteral("开始时间"), new QLabel(startTime.isValid() ? startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("—")));
                              form->addRow(QStringLiteral("已充时长"), new QLabel(durationText));

                              QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
                              connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                              form->addRow(buttons);
                              dialog.exec();
                          });
}
