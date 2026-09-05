#include "navigationdialog.h"

#include "map/tencentmapkey.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

#ifdef EVCP_HAVE_WEBENGINE
#include <QWebEngineView>
#endif

bool NavigationDialog::isAvailable()
{
#ifdef EVCP_HAVE_WEBENGINE
    return !mapconfig::kTencentMapKey.isEmpty();
#else
    return false;
#endif
}

QUrl NavigationDialog::buildRouteUrl(const QString &type, double fromLng, double fromLat,
                                     double toLng, double toLat, const QString &stationName,
                                     const QString &fromDescription)
{
    // 腾讯地图 URI API：坐标格式为「纬度,经度」；fromcoord 用显式坐标而非
    // CurrentLocation 字符串（后者依赖页面内定位授权，桌面端无法生成路线）；
    // referer 为应用标识（此处用已配置 Key）
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), type);
    query.addQueryItem(QStringLiteral("from"),
                       fromDescription.isEmpty() ? QStringLiteral("我的位置") : fromDescription);
    query.addQueryItem(QStringLiteral("fromcoord"),
                       QStringLiteral("%1,%2").arg(fromLat, 0, 'f', 6).arg(fromLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("to"), stationName);
    query.addQueryItem(QStringLiteral("tocoord"),
                       QStringLiteral("%1,%2").arg(toLat, 0, 'f', 6).arg(toLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("referer"), mapconfig::kTencentMapKey);
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
    url.setQuery(query);
    return url;
}

NavigationDialog::NavigationDialog(const QString &stationName, double fromLng, double fromLat,
                                   const QString &fromDescription, double toLng, double toLat,
                                   QWidget *parent)
    : QDialog(parent)
    , m_stationName(stationName)
    , m_fromDescription(fromDescription)
    , m_fromLng(fromLng)
    , m_fromLat(fromLat)
    , m_toLng(toLng)
    , m_toLat(toLat)
    , m_mode(QStringLiteral("drive"))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("导航 - %1").arg(stationName));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *infoCard = new QFrame(this);
    infoCard->setObjectName(QStringLiteral("card"));
    auto *infoLayout = new QVBoxLayout(infoCard);
    infoLayout->setContentsMargins(16, 12, 16, 12);
    infoLayout->setSpacing(6);
    const QString fromText = m_fromDescription.isEmpty()
        ? QStringLiteral("当前定位")
        : m_fromDescription;
    auto *fromLabel = new QLabel(QStringLiteral("起点：%1（%2, %3）")
                                     .arg(fromText)
                                     .arg(m_fromLat, 0, 'f', 6)
                                     .arg(m_fromLng, 0, 'f', 6),
                                 infoCard);
    fromLabel->setWordWrap(true);
    auto *toLabel = new QLabel(QStringLiteral("终点：%1（%2, %3）")
                                   .arg(m_stationName)
                                   .arg(m_toLat, 0, 'f', 6)
                                   .arg(m_toLng, 0, 'f', 6),
                               infoCard);
    toLabel->setWordWrap(true);
    infoLayout->addWidget(fromLabel);
    infoLayout->addWidget(toLabel);
    layout->addWidget(infoCard);

    auto *modeRow = new QHBoxLayout();
    auto *modeLabel = new QLabel(QStringLiteral("出行方式"), this);
    modeLabel->setObjectName(QStringLiteral("hint"));
    modeRow->addWidget(modeLabel, 0);
    m_driveButton = new QPushButton(QStringLiteral("驾车"), this);
    m_driveButton->setObjectName(QStringLiteral("segmentButton"));
    m_driveButton->setCheckable(true);
    m_driveButton->setChecked(true);
    m_walkButton = new QPushButton(QStringLiteral("步行"), this);
    m_walkButton->setObjectName(QStringLiteral("segmentButton"));
    m_walkButton->setCheckable(true);
    modeRow->addWidget(m_driveButton, 1);
    modeRow->addWidget(m_walkButton, 1);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setProperty("class", QStringLiteral("small"));
    modeRow->addWidget(closeButton, 0);
    layout->addLayout(modeRow);

    m_stack = new QStackedWidget(this);
    auto *placeholder = new QLabel(QStringLiteral("选择出行方式后，点击「导航」加载路线规划"), m_stack);
    placeholder->setObjectName(QStringLiteral("hint"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    m_stack->addWidget(placeholder);
    layout->addWidget(m_stack, 1);

    m_navButton = new QPushButton(QStringLiteral("导航"), this);
    m_navButton->setProperty("class", QStringLiteral("primary"));
    layout->addWidget(m_navButton);

#ifndef EVCP_HAVE_WEBENGINE
    m_navButton->setEnabled(false);
    placeholder->setText(QStringLiteral("当前构建未包含地图组件，无法展示路线"));
#endif

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_navButton, &QPushButton::clicked, this, &NavigationDialog::loadRoute);
    connect(m_driveButton, &QPushButton::clicked, this, [this]() {
        m_driveButton->setChecked(true);
        m_walkButton->setChecked(false);
        m_mode = QStringLiteral("drive");
        if (m_loaded)
            loadRoute();
    });
    connect(m_walkButton, &QPushButton::clicked, this, [this]() {
        m_walkButton->setChecked(true);
        m_driveButton->setChecked(false);
        m_mode = QStringLiteral("walk");
        if (m_loaded)
            loadRoute();
    });

    // 近全屏利用主窗口区域：旧固定 360×560 下路线规划 H5 的地图与结果
    // 面板显示不全；改为按顶层窗口尺寸留少量边距，无父窗时退回等效默认尺寸
    const QWidget *anchor = parent ? parent->window() : nullptr;
    if (anchor)
        resize(anchor->size() - QSize(24, 64));
    else
        resize(436, 896);
}

void NavigationDialog::loadRoute()
{
#ifdef EVCP_HAVE_WEBENGINE
    if (!m_view) {
        m_view = new QWebEngineView(m_stack);
        m_stack->addWidget(m_view);
    }
    m_view->load(buildRouteUrl(m_mode, m_fromLng, m_fromLat, m_toLng, m_toLat,
                               m_stationName, m_fromDescription));
    m_stack->setCurrentWidget(m_view);
    m_loaded = true;
#endif
}
