#include "pilestatuspage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "uienums.h"

PileStatusPage::PileStatusPage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *cards = new QHBoxLayout;
    cards->addWidget(createStatusCard(QStringLiteral("空闲"), &m_idleValue));
    cards->addWidget(createStatusCard(QStringLiteral("在用"), &m_inUseValue));
    cards->addWidget(createStatusCard(QStringLiteral("故障"), &m_faultValue));
    root->addLayout(cards);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("全部电桩")));
    controls->addStretch();
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    controls->addWidget(refreshBtn);
    root->addLayout(controls);

    m_table = new QTableWidget;
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("编号"), QStringLiteral("所属站"), QStringLiteral("类型"),
        QStringLiteral("功率(kW)"), QStringLiteral("状态"),
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    connect(refreshBtn, &QPushButton::clicked, this, &PileStatusPage::refresh);
}

QWidget *PileStatusPage::createStatusCard(const QString &title, QLabel **valueLabel)
{
    QFrame *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout *layout = new QVBoxLayout(card);

    QLabel *titleLabel = new QLabel(title);
    QLabel *value = new QLabel(QStringLiteral("--"));
    QFont font = value->font();
    font.setPointSize(20);
    font.setBold(true);
    value->setFont(font);

    layout->addWidget(titleLabel);
    layout->addWidget(value);
    *valueLabel = value;
    return card;
}

void PileStatusPage::refresh()
{
    m_client->sendRequest(QStringLiteral("pile_status_overview"), QJsonObject(),
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const int idle = data[QStringLiteral("idle")].toInt();
                              const int inUse = data[QStringLiteral("inUse")].toInt();
                              const int fault = data[QStringLiteral("fault")].toInt();
                              const int total = idle + inUse + fault;
                              auto format = [total](int n) {
                                  const double pct = total > 0 ? 100.0 * n / total : 0.0;
                                  return QStringLiteral("%1（%2%）").arg(n).arg(pct, 0, 'f', 1);
                              };
                              m_idleValue->setText(format(idle));
                              m_inUseValue->setText(format(inUse));
                              m_faultValue->setText(format(fault));
                          });

    m_client->sendRequest(QStringLiteral("pile_list"), QJsonObject{{QStringLiteral("stationId"), 0}},
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              const QJsonArray piles = data[QStringLiteral("piles")].toArray();
                              m_table->setRowCount(piles.size());
                              for (int row = 0; row < piles.size(); ++row) {
                                  const QJsonObject p = piles.at(row).toObject();
                                  m_table->setItem(row, 0, new QTableWidgetItem(p[QStringLiteral("code")].toString()));
                                  m_table->setItem(row, 1, new QTableWidgetItem(p[QStringLiteral("stationName")].toString()));
                                  m_table->setItem(row, 2, new QTableWidgetItem(UiEnums::pileTypeText(p[QStringLiteral("type")].toString())));
                                  m_table->setItem(row, 3, new QTableWidgetItem(QString::number(p[QStringLiteral("powerKw")].toInt())));
                                  const QString status = p[QStringLiteral("status")].toString();
                                  QTableWidgetItem *statusItem = new QTableWidgetItem(UiEnums::pileStatusText(status));
                                  statusItem->setForeground(UiEnums::pileStatusColor(status));
                                  m_table->setItem(row, 4, statusItem);
                              }
                          });
}
