#include "orderpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
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
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

#include "filtertable.h"
#include "net/socketclient.h"
#include "uienums.h"

namespace {

QString fmtTime(const QJsonValue &value)
{
    const QString s = value.toString();
    if (s.isEmpty())
        return QStringLiteral("—");
    const QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    return dt.isValid() ? dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : s;
}

QString fmtNum(const QJsonValue &value, int precision)
{
    if (!value.isDouble())
        return QStringLiteral("—");
    return QString::number(value.toDouble(), 'f', precision);
}

} // namespace

OrderPage::OrderPage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *filters = new QHBoxLayout;
    filters->addWidget(new QLabel(QStringLiteral("手机号")));
    m_phoneEdit = new QLineEdit;
    m_phoneEdit->setPlaceholderText(QStringLiteral("模糊匹配"));
    m_phoneEdit->setMaximumWidth(160);
    m_phoneEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^\\d{0,11}$")), m_phoneEdit));
    filters->addWidget(m_phoneEdit);

    filters->addWidget(new QLabel(QStringLiteral("状态")));
    m_statusBox = new QComboBox;
    m_statusBox->addItem(QStringLiteral("全部"), QString());
    m_statusBox->addItem(QStringLiteral("已预约"), QStringLiteral("reserved"));
    m_statusBox->addItem(QStringLiteral("充电中"), QStringLiteral("charging"));
    m_statusBox->addItem(QStringLiteral("待结算"), QStringLiteral("pending_payment"));
    m_statusBox->addItem(QStringLiteral("已完成"), QStringLiteral("completed"));
    m_statusBox->addItem(QStringLiteral("已取消"), QStringLiteral("cancelled"));
    filters->addWidget(m_statusBox);

    m_dateCheck = new QCheckBox(QStringLiteral("按预约日期筛选"));
    filters->addWidget(m_dateCheck);
    m_dateFrom = new QDateEdit(QDate::currentDate().addDays(-7));
    m_dateFrom->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dateFrom->setCalendarPopup(true);
    m_dateFrom->setEnabled(false);
    filters->addWidget(m_dateFrom);
    filters->addWidget(new QLabel(QStringLiteral("至")));
    m_dateTo = new QDateEdit(QDate::currentDate());
    m_dateTo->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dateTo->setCalendarPopup(true);
    m_dateTo->setEnabled(false);
    filters->addWidget(m_dateTo);
    connect(m_dateCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_dateFrom->setEnabled(checked);
        m_dateTo->setEnabled(checked);
    });

    QPushButton *searchBtn = new QPushButton(QStringLiteral("查询"));
    searchBtn->setProperty("primary", true);
    filters->addWidget(searchBtn);
    filters->addStretch();
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    filters->addWidget(refreshBtn);
    root->addLayout(filters);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("orderTable"));
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("订单号"), QStringLiteral("手机号"), QStringLiteral("站点"),
        QStringLiteral("电桩编号"), QStringLiteral("状态"), QStringLiteral("电量(kWh)"),
        QStringLiteral("金额(元)"), QStringLiteral("结算时间"), QStringLiteral("操作"),
    });
    // 先挂筛选排序表头，再配置列宽模式（setHorizontalHeader 会替换表头实例）
    m_ft = new FilterTable(m_table, this);
    m_ft->setExcludedColumns({8});
    m_ft->setScopeNote(QStringLiteral("订单为服务端分页，排序与筛选仅作用于当前页数据"));
    // 操作列控件排序时由工厂重建（Qt 单元格控件不可跨行搬运）
    m_ft->setCellWidgetFactory(8, [this](int row) { return createOrderOps(row); });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
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

    connect(searchBtn, &QPushButton::clicked, this, [this]() {
        m_page = 1;
        loadOrders();
    });
    connect(m_phoneEdit, &QLineEdit::returnPressed, this, [this]() {
        m_page = 1;
        loadOrders();
    });
    connect(refreshBtn, &QPushButton::clicked, this, &OrderPage::refresh);
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_page > 1) {
            --m_page;
            loadOrders();
        }
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_page < totalPages()) {
            ++m_page;
            loadOrders();
        }
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        showDetail(m_table->item(row, 0)->data(Qt::UserRole).toInt());
    });
}

void OrderPage::refresh()
{
    loadOrders();
}

QWidget *OrderPage::createOrderOps(int row)
{
    QTableWidgetItem *idItem = m_table->item(row, 0);
    if (!idItem)
        return nullptr;
    const int orderId = idItem->data(Qt::UserRole).toInt();
    QPushButton *detailBtn = new QPushButton(QStringLiteral("详情"));
    connect(detailBtn, &QPushButton::clicked, this, [this, orderId]() { showDetail(orderId); });
    return detailBtn;
}

int OrderPage::totalPages() const
{
    return qMax(1, (m_total + m_pageSize - 1) / m_pageSize);
}

void OrderPage::updatePagination()
{
    m_pageLabel->setText(QStringLiteral("第 %1 / %2 页（共 %3 条）")
                             .arg(m_page)
                             .arg(totalPages())
                             .arg(m_total));
    m_prevBtn->setEnabled(m_page > 1);
    m_nextBtn->setEnabled(m_page < totalPages());
}

void OrderPage::loadOrders()
{
    if (m_dateCheck->isChecked() && m_dateFrom->date() > m_dateTo->date()) {
        QMessageBox::warning(this, QStringLiteral("订单查询"), QStringLiteral("开始日期不能晚于结束日期"));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("page")] = m_page;
    payload[QStringLiteral("pageSize")] = m_pageSize;
    const QString phone = m_phoneEdit->text().trimmed();
    if (!phone.isEmpty())
        payload[QStringLiteral("phoneKeyword")] = phone;
    const QString status = m_statusBox->currentData().toString();
    if (!status.isEmpty())
        payload[QStringLiteral("status")] = status;
    if (m_dateCheck->isChecked()) {
        payload[QStringLiteral("dateFrom")] = m_dateFrom->date().toString(QStringLiteral("yyyy-MM-dd"));
        payload[QStringLiteral("dateTo")] = m_dateTo->date().toString(QStringLiteral("yyyy-MM-dd"));
    }

    m_client->sendRequest(QStringLiteral("admin_order_list"), payload,
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("订单查询失败"), msg);
                                  return;
                              }
                              m_total = data[QStringLiteral("total")].toInt();
                              const QJsonArray orders = data[QStringLiteral("orders")].toArray();
                              int previousOrderId = -1;
                              const auto selected = m_table->selectedItems();
                              if (!selected.isEmpty() && m_table->item(selected.first()->row(), 0))
                                  previousOrderId = m_table->item(selected.first()->row(), 0)->data(Qt::UserRole).toInt();
                              m_table->setRowCount(orders.size());
                              for (int row = 0; row < orders.size(); ++row) {
                                  const QJsonObject o = orders.at(row).toObject();
                                  const int orderId = o[QStringLiteral("orderId")].toInt();

                                  QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(orderId));
                                  idItem->setData(Qt::UserRole, orderId);
                                  m_table->setItem(row, 0, idItem);
                                  m_table->setItem(row, 1, new QTableWidgetItem(o[QStringLiteral("userPhone")].toString()));
                                  m_table->setItem(row, 2, new QTableWidgetItem(o[QStringLiteral("stationName")].toString()));
                                  m_table->setItem(row, 3, new QTableWidgetItem(o[QStringLiteral("pileCode")].toString()));

                                  const QString status = o[QStringLiteral("status")].toString();
                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::orderStatusText(status));
                                  statusItem->setForeground(UiEnums::orderStatusColor(status));
                                  m_table->setItem(row, 4, statusItem);

                                  m_table->setItem(row, 5, new QTableWidgetItem(fmtNum(o[QStringLiteral("energyKwh")], 3)));
                                  m_table->setItem(row, 6, new QTableWidgetItem(fmtNum(o[QStringLiteral("amount")], 2)));
                                  m_table->setItem(row, 7, new QTableWidgetItem(fmtTime(o[QStringLiteral("settledAt")])));

                                  m_table->setCellWidget(row, 8, createOrderOps(row));
                              }
                              // 重载后恢复选中：优先按 orderId 找回原行（跳过筛选隐藏行），否则选中第一可见行
                              m_ft->apply();
                              const int targetRow = m_ft->rowToSelect(previousOrderId);
                              if (targetRow >= 0)
                                  m_table->selectRow(targetRow);
                              updatePagination();
                          });
}

void OrderPage::showDetail(int orderId)
{
    m_client->sendRequest(QStringLiteral("admin_order_detail"), QJsonObject{{QStringLiteral("orderId"), orderId}},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("订单详情"), msg);
                                  return;
                              }
                              const QJsonObject order = data[QStringLiteral("order")].toObject();
                              const QJsonObject user = data[QStringLiteral("user")].toObject();
                              const QJsonObject station = data[QStringLiteral("station")].toObject();
                              const QJsonObject pile = data[QStringLiteral("pile")].toObject();

                              QDialog dialog(this);
                              dialog.setWindowTitle(QStringLiteral("订单详情 #%1").arg(order[QStringLiteral("orderId")].toInt()));
                              QVBoxLayout *dialogLayout = new QVBoxLayout(&dialog);

                              QScrollArea *scroll = new QScrollArea;
                              scroll->setWidgetResizable(true);
                              QWidget *content = new QWidget;
                              QFormLayout *form = new QFormLayout(content);

                              auto addSection = [form](const QString &title) {
                                  QLabel *label = new QLabel(title);
                                  label->setObjectName(QStringLiteral("sectionTitle"));
                                  form->addRow(label);
                              };
                              auto addRow = [form](const QString &name, const QString &value) {
                                  QLabel *valueLabel = new QLabel(value);
                                  valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
                                  form->addRow(name, valueLabel);
                              };

                              addSection(QStringLiteral("订单信息"));
                              addRow(QStringLiteral("订单号"), QString::number(order[QStringLiteral("orderId")].toInt()));
                              addRow(QStringLiteral("状态"), UiEnums::orderStatusText(order[QStringLiteral("status")].toString()));
                              addRow(QStringLiteral("下单手机号"), order[QStringLiteral("userPhone")].toString());
                              addRow(QStringLiteral("预约时间"), fmtTime(order[QStringLiteral("reservedAt")]));
                              addRow(QStringLiteral("开始时间"), fmtTime(order[QStringLiteral("startTime")]));
                              addRow(QStringLiteral("结束时间"), fmtTime(order[QStringLiteral("endTime")]));
                              addRow(QStringLiteral("结算时间"), fmtTime(order[QStringLiteral("settledAt")]));
                              addRow(QStringLiteral("电量(kWh)"), fmtNum(order[QStringLiteral("energyKwh")], 3));
                              addRow(QStringLiteral("单价(元/度)"), fmtNum(order[QStringLiteral("unitPrice")], 2));
                              addRow(QStringLiteral("金额(元)"), fmtNum(order[QStringLiteral("amount")], 2));

                              addSection(QStringLiteral("用户信息"));
                              addRow(QStringLiteral("用户ID"), QString::number(user[QStringLiteral("userId")].toInt()));
                              addRow(QStringLiteral("手机号"), user[QStringLiteral("phone")].toString());
                              addRow(QStringLiteral("昵称"), user[QStringLiteral("nickname")].toString());
                              addRow(QStringLiteral("余额(元)"), fmtNum(user[QStringLiteral("balance")], 2));
                              addRow(QStringLiteral("注册时间"), fmtTime(user[QStringLiteral("regTime")]));
                              addRow(QStringLiteral("状态"), UiEnums::userStatusText(user[QStringLiteral("status")].toString()));

                              addSection(QStringLiteral("站点信息"));
                              addRow(QStringLiteral("站点ID"), QString::number(station[QStringLiteral("stationId")].toInt()));
                              addRow(QStringLiteral("名称"), station[QStringLiteral("name")].toString());
                              addRow(QStringLiteral("地址"), station[QStringLiteral("address")].toString());
                              addRow(QStringLiteral("当前单价(元/度)"), fmtNum(station[QStringLiteral("pricePerKwh")], 2));

                              addSection(QStringLiteral("电桩信息"));
                              addRow(QStringLiteral("电桩ID"), QString::number(pile[QStringLiteral("pileId")].toInt()));
                              addRow(QStringLiteral("编号"), pile[QStringLiteral("code")].toString());
                              addRow(QStringLiteral("类型"), UiEnums::pileTypeText(pile[QStringLiteral("type")].toString()));
                              addRow(QStringLiteral("功率(kW)"), fmtNum(pile[QStringLiteral("powerKw")], 1));
                              addRow(QStringLiteral("当前状态"), UiEnums::pileStatusText(pile[QStringLiteral("status")].toString()));

                              scroll->setWidget(content);
                              dialogLayout->addWidget(scroll);
                              QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
                              connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                              dialogLayout->addWidget(buttons);
                              dialog.resize(480, 560);
                              dialog.exec();
                          });
}
