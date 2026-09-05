#include "findstationpage.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

#include "map/mapbridge.h"
#include "net/socketclient.h"
#include "pages/navigationdialog.h"
#include "ui/uienums.h"

#include <QPointer>

namespace {

// 预设区域定位（说明书要求「下拉选择区域或手动输入地址进行定位（软件层面模拟GPS）」）：
// 北京常用区域，坐标硬编码（区中心），选中即以该坐标查找附近站点
struct PresetRegion {
    const char *name;
    double lng;
    double lat;
};

// 下拉第一项「全部区域（默认）」的定位点：北京市中心
constexpr PresetRegion kDefaultRegion = {"全部区域（默认）", 116.397, 39.909};

constexpr PresetRegion kPresetRegions[] = {
    {"房山区（良乡）", 116.14, 39.74},
    {"海淀区", 116.30, 39.98},
    {"朝阳区", 116.48, 39.95},
    {"丰台区", 116.28, 39.86},
    {"东城区", 116.42, 39.93},
    {"西城区", 116.37, 39.91},
};

class StationCard : public QFrame
{
    Q_OBJECT
public:
    explicit StationCard(const Station &station, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_station(station)
    {
        setObjectName(QStringLiteral("stationCard"));
        setCursor(Qt::PointingHandCursor);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(6);

        auto *topRow = new QHBoxLayout();
        auto *name = new QLabel(station.name, this);
        name->setObjectName(QStringLiteral("cardTitle"));
        topRow->addWidget(name, 1);
        if (station.distanceKm >= 0) {
            // 说明书：「点击距离信息」触发一键导航
            auto *dist = new QPushButton(QStringLiteral("%1 km").arg(station.distanceKm, 0, 'f', 1), this);
            dist->setProperty("class", QStringLiteral("link"));
            dist->setCursor(Qt::PointingHandCursor);
            dist->setToolTip(QStringLiteral("点击距离导航到该站点"));
            connect(dist, &QPushButton::clicked, this, [this]() {
                emit navigateClicked();
            });
            topRow->addWidget(dist, 0);
        }
        auto *navButton = new QPushButton(QStringLiteral("导航"), this);
        navButton->setProperty("class", QStringLiteral("small"));
        connect(navButton, &QPushButton::clicked, this, [this]() {
            emit navigateClicked();
        });
        topRow->addWidget(navButton, 0);
        layout->addLayout(topRow);

        auto *addr = new QLabel(station.address, this);
        addr->setObjectName(QStringLiteral("hint"));
        layout->addWidget(addr);

        auto *info = new QLabel(
            QStringLiteral("电价 ¥%1/kWh · 空闲 %2/%3 桩 · 在线率 %4%")
                .arg(station.pricePerKwh, 0, 'f', 2)
                .arg(station.pileIdle)
                .arg(station.pileTotal)
                .arg(qRound(station.onlineRate * 100)),
            this);
        layout->addWidget(info);
    }

    Station station() const { return m_station; }

signals:
    void clicked();
    void navigateClicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
        QFrame::mouseReleaseEvent(event);
    }

private:
    Station m_station;
};

} // namespace

FindStationPage::FindStationPage(SocketClient *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("找站"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);

    auto *searchCard = new QFrame(this);
    searchCard->setObjectName(QStringLiteral("card"));
    auto *searchLayout = new QVBoxLayout(searchCard);
    searchLayout->setContentsMargins(16, 12, 16, 12);
    searchLayout->setSpacing(8);

    auto *regionRow = new QHBoxLayout();
    auto *regionLabel = new QLabel(QStringLiteral("区域定位"), searchCard);
    regionLabel->setObjectName(QStringLiteral("hint"));
    m_regionCombo = new QComboBox(searchCard);
    // 第一项「全部区域（默认）」也带坐标（北京市中心）：不选具体区域时默认按它定位，
    // 进入找站页即有站点列表（见 showEvent）
    m_regionCombo->addItem(QString::fromUtf8(kDefaultRegion.name),
                           QPointF(kDefaultRegion.lng, kDefaultRegion.lat));
    for (const PresetRegion &r : kPresetRegions)
        m_regionCombo->addItem(QString::fromUtf8(r.name), QPointF(r.lng, r.lat));
    regionRow->addWidget(regionLabel, 0);
    regionRow->addWidget(m_regionCombo, 1);
    searchLayout->addLayout(regionRow);

    auto *addrRow = new QHBoxLayout();
    m_addressEdit = new QLineEdit(searchCard);
    m_addressEdit->setPlaceholderText(QStringLiteral("输入区域或地址，如：海淀区中关村"));
    // 地址允许中文：显式 ImhNone，输入法不受限
    m_addressEdit->setInputMethodHints(Qt::ImhNone);
    m_geocodeButton = new QPushButton(QStringLiteral("解析地址"), searchCard);
    m_geocodeButton->setProperty("class", QStringLiteral("small"));
    addrRow->addWidget(m_addressEdit, 1);
    addrRow->addWidget(m_geocodeButton, 0);
    searchLayout->addLayout(addrRow);

    if (!MapBridge::isConfigured()) {
        auto *mapHint = new QLabel(QStringLiteral("未配置腾讯地图 Key，地址解析不可用；可用区域下拉或手动经纬度定位"), searchCard);
        mapHint->setObjectName(QStringLiteral("hint"));
        mapHint->setWordWrap(true);
        searchLayout->addWidget(mapHint);
    }

    auto *coordRow = new QHBoxLayout();
    m_lngEdit = new QLineEdit(searchCard);
    m_lngEdit->setPlaceholderText(QStringLiteral("经度 lng"));
    m_lngEdit->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    m_latEdit = new QLineEdit(searchCard);
    m_latEdit->setPlaceholderText(QStringLiteral("纬度 lat"));
    m_latEdit->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    m_searchButton = new QPushButton(QStringLiteral("查找附近站点"), searchCard);
    m_searchButton->setProperty("class", QStringLiteral("smallPrimary"));
    coordRow->addWidget(m_lngEdit, 1);
    coordRow->addWidget(m_latEdit, 1);
    coordRow->addWidget(m_searchButton, 0);
    searchLayout->addLayout(coordRow);

    root->addWidget(searchCard);

    auto *listHeader = new QHBoxLayout();
    auto *listTitle = new QLabel(QStringLiteral("附近站点"), this);
    listTitle->setObjectName(QStringLiteral("cardTitle"));
    m_refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    m_refreshButton->setProperty("class", QStringLiteral("small"));
    listHeader->addWidget(listTitle, 1);
    listHeader->addWidget(m_refreshButton, 0);
    root->addLayout(listHeader);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_listContainer = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(12);
    m_emptyHint = new QLabel(QStringLiteral("输入地址或经纬度后查找附近站点"), m_listContainer);
    m_emptyHint->setObjectName(QStringLiteral("hint"));
    m_emptyHint->setAlignment(Qt::AlignHCenter);
    m_listLayout->addWidget(m_emptyHint);
    m_listLayout->addStretch(1);
    scroll->setWidget(m_listContainer);
    root->addWidget(scroll, 1);

    connect(m_regionCombo, QOverload<int>::of(&QComboBox::activated),
            this, &FindStationPage::onRegionSelected);
    connect(m_geocodeButton, &QPushButton::clicked, this, &FindStationPage::onGeocodeClicked);
    connect(m_searchButton, &QPushButton::clicked, this, &FindStationPage::onSearchClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &FindStationPage::onRefreshClicked);
}

void FindStationPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 「全部区域（默认）」：登录后首次进入找站页即以北京市中心默认坐标查询，
    // 不选区域也有站点列表可看；仅在已连接时自动触发一次
    if (m_defaultSearched || !m_client->isConnected())
        return;
    const QVariant data = m_regionCombo->itemData(m_regionCombo->currentIndex());
    if (!data.isValid())
        return;
    m_defaultSearched = true;
    onRegionSelected(m_regionCombo->currentIndex());
}

void FindStationPage::onRegionSelected(int index)
{
    const QVariant data = m_regionCombo->itemData(index);
    if (!data.isValid())
        return;
    const QPointF coord = data.toPointF();
    m_lngEdit->setText(QString::number(coord.x(), 'f', 6));
    m_latEdit->setText(QString::number(coord.y(), 'f', 6));
    m_lastLocationDesc = m_regionCombo->itemText(index);
    searchNearby(coord.x(), coord.y());
}

void FindStationPage::onGeocodeClicked()
{
    const QString address = m_addressEdit->text().trimmed();
    if (address.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("解析地址"), QStringLiteral("请输入区域或地址"));
        return;
    }
    if (!MapBridge::isConfigured()) {
        QMessageBox::warning(this, QStringLiteral("解析地址"),
                             QStringLiteral("未配置腾讯地图 Key，请改用手动经纬度输入"));
        return;
    }
    if (!m_map)
        m_map = new MapBridge(this);
    setBusy(true);
    m_map->geocode(address, [this, address](bool ok, double lng, double lat, const QString &error) {
        setBusy(false);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("解析地址"), error);
            return;
        }
        m_lngEdit->setText(QString::number(lng, 'f', 6));
        m_latEdit->setText(QString::number(lat, 'f', 6));
        m_lastLocationDesc = address;
        searchNearby(lng, lat);
    });
}

void FindStationPage::onSearchClicked()
{
    bool okLng = false;
    bool okLat = false;
    const double lng = m_lngEdit->text().trimmed().toDouble(&okLng);
    const double lat = m_latEdit->text().trimmed().toDouble(&okLat);
    if (!okLng || !okLat || lng < -180 || lng > 180 || lat < -90 || lat > 90) {
        QMessageBox::warning(this, QStringLiteral("查找附近站点"),
                             QStringLiteral("请输入正确的经纬度（经度 -180~180，纬度 -90~90）"));
        return;
    }
    m_lastLocationDesc = QStringLiteral("手动输入坐标");
    searchNearby(lng, lat);
}

void FindStationPage::onRefreshClicked()
{
    if (!m_hasLastCoord) {
        QMessageBox::information(this, QStringLiteral("刷新"), QStringLiteral("请先查找附近站点"));
        return;
    }
    searchNearby(m_lastLng, m_lastLat);
}

void FindStationPage::searchNearby(double lng, double lat)
{
    setBusy(true);
    m_client->sendRequest(QStringLiteral("nearby_station_list"),
                          QJsonObject{{QStringLiteral("lng"), lng},
                                      {QStringLiteral("lat"), lat},
                                      {QStringLiteral("limit"), 50}},
                          [this, lng, lat](int code, const QString &msg, const QJsonObject &data) {
                              setBusy(false);
                              if (code != 0) {
                                  if (code != SocketClient::kErrConnectionLost)
                                      QMessageBox::warning(this, QStringLiteral("查找附近站点"), msg);
                                  else
                                      QMessageBox::warning(this, QStringLiteral("查找附近站点"),
                                                           QStringLiteral("网络中断，请稍后重试"));
                                  return;
                              }
                              m_hasLastCoord = true;
                              m_lastLng = lng;
                              m_lastLat = lat;
                              QList<Station> stations;
                              const QJsonArray arr = data.value(QStringLiteral("stations")).toArray();
                              stations.reserve(arr.size());
                              for (const QJsonValue &v : arr)
                                  stations.append(Station::fromJson(v.toObject()));
                              renderStations(stations);
                          });
}

void FindStationPage::renderStations(const QList<Station> &stations)
{
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (stations.isEmpty()) {
        auto *hint = new QLabel(QStringLiteral("附近暂无站点"), m_listContainer);
        hint->setObjectName(QStringLiteral("hint"));
        hint->setAlignment(Qt::AlignHCenter);
        m_listLayout->addWidget(hint);
    }
    for (const Station &s : stations) {
        auto *card = new StationCard(s, m_listContainer);
        connect(card, &StationCard::clicked, this, [this, s]() {
            showStationDetail(s);
        });
        connect(card, &StationCard::navigateClicked, this, [this, s]() {
            onNavigateToStation(s);
        });
        m_listLayout->addWidget(card);
    }
    m_listLayout->addStretch(1);
}

void FindStationPage::showStationDetail(const Station &station)
{
    m_client->sendRequest(QStringLiteral("station_detail"),
                          QJsonObject{{QStringLiteral("stationId"), static_cast<double>(station.stationId)}},
                          [this, station](int code, const QString &msg, const QJsonObject &data) {
                              if (code != 0) {
                                  QMessageBox::warning(this, QStringLiteral("站点详情"),
                                                       msg.isEmpty() ? QStringLiteral("查询失败") : msg);
                                  return;
                              }
                              const Station detail = Station::fromJson(
                                  data.value(QStringLiteral("station")).toObject());
                              QList<Pile> piles;
                              const QJsonArray arr = data.value(QStringLiteral("piles")).toArray();
                              for (const QJsonValue &v : arr)
                                  piles.append(Pile::fromJson(v.toObject()));

                              // 非模态展示：不得在响应回调里 exec() 嵌套事件循环
                              // （回调栈在 SocketClient::onReadyRead 内时，嵌套循环会
                              // 饿死后续 readyRead，见 SocketClient::invokeCallback）
                              auto *dialog = new QDialog(this);
                              dialog->setAttribute(Qt::WA_DeleteOnClose);
                              dialog->setWindowTitle(detail.name);
                              auto *dlgLayout = new QVBoxLayout(dialog);
                              dlgLayout->setContentsMargins(16, 16, 16, 16);
                              dlgLayout->setSpacing(8);

                              auto *info = new QLabel(
                                  QStringLiteral("%1\n电价 ¥%2/kWh · 空闲 %3/%4 桩 · 在线率 %5%")
                                      .arg(detail.address)
                                      .arg(detail.pricePerKwh, 0, 'f', 2)
                                      .arg(detail.pileIdle)
                                      .arg(detail.pileTotal)
                                      .arg(qRound(detail.onlineRate * 100)),
                                  dialog);
                              info->setObjectName(QStringLiteral("hint"));
                              dlgLayout->addWidget(info);

                              auto *divider = new QFrame(dialog);
                              divider->setObjectName(QStringLiteral("divider"));
                              dlgLayout->addWidget(divider);

                              auto *scroll = new QScrollArea(dialog);
                              scroll->setWidgetResizable(true);
                              auto *container = new QWidget(scroll);
                              auto *pilesLayout = new QVBoxLayout(container);
                              pilesLayout->setContentsMargins(0, 0, 0, 0);
                              pilesLayout->setSpacing(8);
                              if (piles.isEmpty()) {
                                  auto *hint = new QLabel(QStringLiteral("该站点暂无电桩"), container);
                                  hint->setObjectName(QStringLiteral("hint"));
                                  pilesLayout->addWidget(hint);
                              }
                              for (const Pile &p : piles) {
                                  auto *row = new QFrame(container);
                                  row->setObjectName(QStringLiteral("card"));
                                  auto *rowLayout = new QHBoxLayout(row);
                                  rowLayout->setContentsMargins(12, 8, 12, 8);
                                  auto *codeLabel = new QLabel(
                                      QStringLiteral("%1 · %2 · %3kW")
                                          .arg(p.code, ui::pileTypeText(p.type))
                                          .arg(p.powerKw, 0, 'f', 0),
                                      row);
                                  rowLayout->addWidget(codeLabel, 1);
                                  auto *statusLabel = new QLabel(ui::pileStatusText(p.status), row);
                                  ui::setState(statusLabel, p.status);
                                  rowLayout->addWidget(statusLabel, 0);
                                  if (p.status == QLatin1String("idle")) {
                                      auto *reserveBtn = new QPushButton(QStringLiteral("预约"), row);
                                      reserveBtn->setProperty("class", QStringLiteral("smallPrimary"));
                                      connect(reserveBtn, &QPushButton::clicked, this,
                                              [this, p, dialog]() {
                                                  reservePile(p.pileId, p.code, dialog);
                                              });
                                      rowLayout->addWidget(reserveBtn, 0);
                                  }
                                  pilesLayout->addWidget(row);
                              }
                              pilesLayout->addStretch(1);
                              scroll->setWidget(container);
                              dlgLayout->addWidget(scroll, 1);

                              auto *closeBtn = new QPushButton(QStringLiteral("关闭"), dialog);
                              connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
                              dlgLayout->addWidget(closeBtn);
                              dialog->resize(340, 420);
                              dialog->show();
                          });
}

void FindStationPage::reservePile(qint64 pileId, const QString &pileCode, QDialog *dialog)
{
    const QPointer<QDialog> dialogGuard(dialog);

    // 协议 v2.1：预约要求余额 > 0。本地缓存余额已知且为 0 时直接拦截，不发请求
    //（余额按分量化，<0.005 元即视为 0）；缓存未知（<0）时放行，由服务端 3004 兜底。
    if (m_knownBalance >= 0.0 && m_knownBalance < 0.005) {
        QMessageBox::warning(dialog, QStringLiteral("预约"),
                             QStringLiteral("余额不足，请先充值"));
        return;
    }

    if (dialogGuard) {
        const QList<QPushButton *> buttons = dialogGuard->findChildren<QPushButton *>();
        for (QPushButton *b : buttons)
            b->setEnabled(false);
    }
    m_client->sendRequest(QStringLiteral("charge_reserve"),
                          QJsonObject{{QStringLiteral("pileId"), static_cast<double>(pileId)}},
                          [this, dialogGuard, pileCode](int code, const QString &msg, const QJsonObject &) {
                              // 请求在途期间用户可能已关闭详情对话框（WA_DeleteOnClose），
                              // 所有对对话框的访问必须经 QPointer 判空
                              if (code == 0) {
                                  QMessageBox::information(dialogGuard ? static_cast<QWidget *>(dialogGuard.data())
                                                                       : static_cast<QWidget *>(this),
                                                           QStringLiteral("预约"),
                                                           QStringLiteral("电桩 %1 预约成功").arg(pileCode));
                                  if (dialogGuard)
                                      dialogGuard->accept();
                                  emit orderStateDirty();
                                  return;
                              }
                              if (code == SocketClient::kErrConnectionLost) {
                                  QMessageBox::warning(dialogGuard ? static_cast<QWidget *>(dialogGuard.data())
                                                                   : static_cast<QWidget *>(this),
                                                       QStringLiteral("预约"),
                                                       QStringLiteral("网络中断，预约结果未知，请在充电页确认订单状态"));
                                  if (dialogGuard)
                                      dialogGuard->accept();
                                  emit orderStateDirty();
                                  return;
                              }
                              if (dialogGuard) {
                                  const QList<QPushButton *> buttons =
                                      dialogGuard->findChildren<QPushButton *>();
                                  for (QPushButton *b : buttons)
                                      b->setEnabled(true);
                              }
                              if (code == 3004) {
                                  // 与服务端权威余额对齐，后续预约走本地拦截
                                  m_knownBalance = 0.0;
                                  QMessageBox::warning(dialogGuard ? static_cast<QWidget *>(dialogGuard.data())
                                                                   : static_cast<QWidget *>(this),
                                                       QStringLiteral("预约"),
                                                       QStringLiteral("余额不足，请先充值"));
                                  emit requestRecharge();
                                  return;
                              }
                              QMessageBox::warning(dialogGuard ? static_cast<QWidget *>(dialogGuard.data())
                                                               : static_cast<QWidget *>(this),
                                                   QStringLiteral("预约"),
                                                   msg.isEmpty() ? QStringLiteral("预约失败") : msg);
                          });
}

void FindStationPage::onNavigateToStation(const Station &station)
{
    if (!NavigationDialog::isAvailable()) {
        QMessageBox::information(this, QStringLiteral("导航"),
                                 QStringLiteral("当前构建未包含地图组件或未配置腾讯地图 Key，无法导航"));
        return;
    }
    if (!m_hasLastCoord) {
        QMessageBox::information(this, QStringLiteral("导航"),
                                 QStringLiteral("请先选择区域、解析地址或输入经纬度，作为导航起点"));
        return;
    }
    // 非模态 + WA_DeleteOnClose；QWebEngineView 在点击「导航」按钮时才创建并加载路线
    auto *dialog = new NavigationDialog(station.name, m_lastLng, m_lastLat, m_lastLocationDesc,
                                        station.lng, station.lat, this);
    dialog->show();
}

void FindStationPage::setKnownBalance(double balance)
{
    m_knownBalance = balance;
}

void FindStationPage::setBusy(bool busy)
{
    m_geocodeButton->setEnabled(busy ? false : true);
    m_searchButton->setEnabled(!busy);
    m_refreshButton->setEnabled(!busy);
}

#include "findstationpage.moc"
