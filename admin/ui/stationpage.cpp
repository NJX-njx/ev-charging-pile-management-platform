#include "stationpage.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
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
    controls->addStretch();
    QPushButton *pilesBtn = new QPushButton(QStringLiteral("查看站内电桩"));
    controls->addWidget(pilesBtn);
    QPushButton *addBtn = new QPushButton(QStringLiteral("新增站点"));
    addBtn->setProperty("primary", true);
    controls->addWidget(addBtn);
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    controls->addWidget(refreshBtn);
    root->addLayout(controls);

    m_table = new QTableWidget;
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("站名"), QStringLiteral("地址"),
        QStringLiteral("经度"), QStringLiteral("纬度"), QStringLiteral("单价(元/度)"),
        QStringLiteral("总桩数"), QStringLiteral("在线率"),
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    root->addWidget(m_table, 1);

    connect(refreshBtn, &QPushButton::clicked, this, &StationPage::refresh);
    connect(addBtn, &QPushButton::clicked, this, &StationPage::onAddStation);
    connect(pilesBtn, &QPushButton::clicked, this, &StationPage::onShowPiles);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        onShowPiles();
    });
}

void StationPage::refresh()
{
    m_client->sendRequest(QStringLiteral("station_list"), QJsonObject(),
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const QJsonArray stations = data[QStringLiteral("stations")].toArray();
                              m_table->setRowCount(stations.size());
                              for (int row = 0; row < stations.size(); ++row) {
                                  const QJsonObject s = stations.at(row).toObject();

                                  QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(s[QStringLiteral("stationId")].toInt()));
                                  idItem->setData(Qt::UserRole, s[QStringLiteral("stationId")].toInt());
                                  m_table->setItem(row, 0, idItem);
                                  m_table->setItem(row, 1, new QTableWidgetItem(s[QStringLiteral("name")].toString()));
                                  m_table->setItem(row, 2, new QTableWidgetItem(s[QStringLiteral("address")].toString()));
                                  m_table->setItem(row, 3, new QTableWidgetItem(QString::number(s[QStringLiteral("lng")].toDouble(), 'f', 6)));
                                  m_table->setItem(row, 4, new QTableWidgetItem(QString::number(s[QStringLiteral("lat")].toDouble(), 'f', 6)));
                                  m_table->setItem(row, 5, new QTableWidgetItem(QString::number(s[QStringLiteral("pricePerKwh")].toDouble(), 'f', 2)));
                                  m_table->setItem(row, 6, new QTableWidgetItem(QString::number(s[QStringLiteral("pileTotal")].toInt())));
                                  m_table->setItem(row, 7, new QTableWidgetItem(QStringLiteral("%1%").arg(s[QStringLiteral("onlineRate")].toDouble() * 100, 0, 'f', 0)));
                              }
                          });
}

int StationPage::selectedStationId() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return -1;
    return m_table->item(items.first()->row(), 0)->data(Qt::UserRole).toInt();
}

void StationPage::onShowPiles()
{
    const int stationId = selectedStationId();
    if (stationId < 0) {
        QMessageBox::information(this, QStringLiteral("站内电桩"), QStringLiteral("请先选择一个充电站"));
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
                                  table->setItem(row, 2, new QTableWidgetItem(QString::number(p[QStringLiteral("powerKw")].toInt())));
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

    m_client->sendRequest(QStringLiteral("station_add"), payload,
                          [this](int code, const QString &msg, const QJsonObject &data) {
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
