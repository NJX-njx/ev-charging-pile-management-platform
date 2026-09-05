#include "navigationdialog.h"

#include "map/tencentmapkey.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>

#ifdef EVCP_HAVE_WEBENGINE
#include <QWebEngineView>
#endif

bool NavigationDialog::isAvailable()
{
#ifdef EVCP_HAVE_WEBENGINE
    return true;
#else
    return false;
#endif
}

NavigationDialog::NavigationDialog(const QString &stationName, double fromLng, double fromLat,
                                   double toLng, double toLat, QWidget *parent)
    : QDialog(parent)
    , m_stationName(stationName)
    , m_fromLng(fromLng)
    , m_fromLat(fromLat)
    , m_toLng(toLng)
    , m_toLat(toLat)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("导航 - %1").arg(stationName));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *topRow = new QHBoxLayout();
    auto *modeLabel = new QLabel(QStringLiteral("出行方式"), this);
    modeLabel->setObjectName(QStringLiteral("hint"));
    topRow->addWidget(modeLabel, 0);
    m_driveButton = new QPushButton(QStringLiteral("驾车"), this);
    m_driveButton->setObjectName(QStringLiteral("segmentButton"));
    m_driveButton->setCheckable(true);
    m_driveButton->setChecked(true);
    m_walkButton = new QPushButton(QStringLiteral("步行"), this);
    m_walkButton->setObjectName(QStringLiteral("segmentButton"));
    m_walkButton->setCheckable(true);
    topRow->addWidget(m_driveButton, 1);
    topRow->addWidget(m_walkButton, 1);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setProperty("class", QStringLiteral("small"));
    topRow->addWidget(closeButton, 0);
    layout->addLayout(topRow);

#ifdef EVCP_HAVE_WEBENGINE
    m_view = new QWebEngineView(this);
    layout->addWidget(m_view, 1);
#else
    auto *hint = new QLabel(QStringLiteral("当前构建未包含地图组件，无法展示路线"), this);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setAlignment(Qt::AlignHCenter);
    layout->addWidget(hint, 1);
#endif

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_driveButton, &QPushButton::clicked, this, [this]() {
        m_driveButton->setChecked(true);
        m_walkButton->setChecked(false);
        loadRoute(QStringLiteral("drive"));
    });
    connect(m_walkButton, &QPushButton::clicked, this, [this]() {
        m_walkButton->setChecked(true);
        m_driveButton->setChecked(false);
        loadRoute(QStringLiteral("walk"));
    });

    resize(360, 560);
    loadRoute(QStringLiteral("drive"));
}

void NavigationDialog::loadRoute(const QString &type)
{
#ifdef EVCP_HAVE_WEBENGINE
    if (!m_view)
        return;
    // 腾讯地图 URI API：坐标格式为「纬度,经度」，referer 为应用标识（此处用 Key）
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), type);
    query.addQueryItem(QStringLiteral("from"), QStringLiteral("我的位置"));
    query.addQueryItem(QStringLiteral("fromcoord"),
                       QStringLiteral("%1,%2").arg(m_fromLat, 0, 'f', 6).arg(m_fromLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("to"), m_stationName);
    query.addQueryItem(QStringLiteral("tocoord"),
                       QStringLiteral("%1,%2").arg(m_toLat, 0, 'f', 6).arg(m_toLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("referer"), mapconfig::kTencentMapKey);
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
    url.setQuery(query);
    m_view->load(url);
#else
    Q_UNUSED(type);
#endif
}
