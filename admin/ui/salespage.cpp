#include "salespage.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "net/socketclient.h"

#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

SalesPage::SalesPage(SocketClient *client, QWidget *parent)
    : QWidget(parent), m_client(client)
{
    setObjectName(QStringLiteral("page"));
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *cards = new QHBoxLayout;
    cards->addWidget(createMetricCard(QStringLiteral("今日营收"), &m_todayValue));
    cards->addWidget(createMetricCard(QStringLiteral("本月营收"), &m_monthValue));
    cards->addWidget(createMetricCard(QStringLiteral("总营收"), &m_totalValue));
    root->addLayout(cards);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("营收趋势")));
    m_rangeBox = new QComboBox;
    m_rangeBox->addItem(QStringLiteral("近7日"), 7);
    m_rangeBox->addItem(QStringLiteral("近30日"), 30);
    controls->addWidget(m_rangeBox);
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    controls->addWidget(refreshBtn);
    controls->addStretch();
    root->addLayout(controls);

    QChartView *chartView = new QChartView;
    chartView->setRenderHint(QPainter::Antialiasing);
    QChart *chart = new QChart;
    chart->setTitle(QStringLiteral("营收趋势"));
    chartView->setChart(chart);
    m_chartArea = chartView;
    root->addWidget(m_chartArea, 1);

    connect(refreshBtn, &QPushButton::clicked, this, &SalesPage::refresh);
    connect(m_rangeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        updateTrend(m_rangeBox->currentData().toInt());
    });
}

QWidget *SalesPage::createMetricCard(const QString &title, QLabel **valueLabel)
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
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

void SalesPage::refresh()
{
    m_client->sendRequest(QStringLiteral("revenue_summary"), QJsonObject(),
                          [this](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;
                              m_todayValue->setText(QStringLiteral("¥ %1").arg(data[QStringLiteral("today")].toDouble(), 0, 'f', 2));
                              m_monthValue->setText(QStringLiteral("¥ %1").arg(data[QStringLiteral("month")].toDouble(), 0, 'f', 2));
                              m_totalValue->setText(QStringLiteral("¥ %1").arg(data[QStringLiteral("total")].toDouble(), 0, 'f', 2));
                          });
    updateTrend(m_rangeBox->currentData().toInt());
}

void SalesPage::updateTrend(int range)
{
    QJsonObject payload;
    payload[QStringLiteral("range")] = range;
    m_client->sendRequest(QStringLiteral("revenue_trend"), payload,
                          [this, range](int code, const QString &, const QJsonObject &data) {
                              if (code != 0)
                                  return;

                              QLineSeries *series = new QLineSeries;
                              QStringList categories;
                              const QJsonArray points = data[QStringLiteral("points")].toArray();
                              for (const QJsonValue &v : points) {
                                  const QJsonObject p = v.toObject();
                                  series->append(categories.size(), p[QStringLiteral("amount")].toDouble());
                                  categories << p[QStringLiteral("date")].toString().mid(5);
                              }

                              QChart *chart = new QChart;
                              chart->addSeries(series);
                              chart->setTitle(QStringLiteral("近%1日营收趋势（元）").arg(range));
                              chart->legend()->hide();

                              QBarCategoryAxis *axisX = new QBarCategoryAxis;
                              axisX->append(categories);
                              chart->addAxis(axisX, Qt::AlignBottom);
                              series->attachAxis(axisX);

                              QValueAxis *axisY = new QValueAxis;
                              axisY->setLabelFormat(QStringLiteral("%.0f"));
                              chart->addAxis(axisY, Qt::AlignLeft);
                              series->attachAxis(axisY);

                              static_cast<QChartView *>(m_chartArea)->setChart(chart);
                          });
}
