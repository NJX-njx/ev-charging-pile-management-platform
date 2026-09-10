#include "navigationdialog.h"

#include "map/tencentmapkey.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

namespace {

// 仅过滤腾讯导航页面的已知 Application Cache 警告。
class NavWebEnginePage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                  int lineNumber, const QString &sourceID) override
    {
        if (message.contains(QLatin1String("Application Cache")))
            return;
        QWebEnginePage::javaScriptConsoleMessage(level, message, lineNumber, sourceID);
    }
};

} // namespace

QUrl NavigationDialog::buildRouteUrl(const QString &type, double fromLng, double fromLat,
                                     double toLng, double toLat, const QString &stationName,
                                     const QString &fromDescription)
{
    // 坐标使用显式「纬度,经度」，避免依赖桌面定位授权；referer 使用地图 Key。
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

    // 按主窗口尺寸预留边距，保证地图与路线面板可见。
    const QWidget *anchor = parent ? parent->window() : nullptr;
    if (anchor)
        resize(anchor->size() - QSize(24, 64));
    else
        resize(436, 896);
}

void NavigationDialog::loadRoute()
{
    if (!m_view) {
        m_view = new QWebEngineView(m_stack);
        // 独立 profile 使用移动端 UA，避免影响定位页。
        // profile 必须晚于 page 析构，防止访问已释放的对象。
        auto *profile = new QWebEngineProfile(this);
        profile->setHttpUserAgent(QStringLiteral(
            "Mozilla/5.0 (Linux; Android 13; Pixel 6) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36"));
        // 无触摸设备时，将鼠标事件转换为导航页面需要的触摸事件。
        QWebEngineScript touchShim;
        touchShim.setName(QStringLiteral("evcpTouchShim"));
        touchShim.setInjectionPoint(QWebEngineScript::DocumentCreation);
        touchShim.setWorldId(QWebEngineScript::MainWorld);
        touchShim.setRunsOnSubFrames(true);
        touchShim.setSourceCode(QStringLiteral(R"JS(
(function () {
    if ('ontouchstart' in window || navigator.maxTouchPoints > 0) return;
    window.__evcpTouchShim = true;
    var pressTarget = null;
    function fire(type, e) {
        var target = pressTarget || e.target;
        if (!target || !window.Touch) return;
        var touch;
        try {
            touch = new Touch({identifier: 0, target: target,
                clientX: e.clientX, clientY: e.clientY,
                pageX: e.pageX, pageY: e.pageY,
                screenX: e.screenX, screenY: e.screenY});
        } catch (err) { return; }
        var active = (type === 'touchend') ? [] : [touch];
        var ev;
        try {
            ev = new TouchEvent(type, {cancelable: true, bubbles: true,
                touches: active, targetTouches: active, changedTouches: [touch]});
        } catch (err) { return; }
        target.dispatchEvent(ev);
    }
    // 仅转换真实输入，避免合成鼠标事件再次触发转换而无限递归。
    document.addEventListener('mousedown', function (e) {
        if (!e.isTrusted) return;
        pressTarget = e.target; fire('touchstart', e);
    }, true);
    document.addEventListener('mousemove', function (e) {
        if (!e.isTrusted || !pressTarget) return;
        fire('touchmove', e);
    }, true);
    document.addEventListener('mouseup', function (e) {
        if (!e.isTrusted || !pressTarget) return;
        fire('touchend', e); pressTarget = null;
    }, true);
})();
)JS"));
        profile->scripts()->insert(touchShim);
        m_view->setPage(new NavWebEnginePage(profile, m_view));
        m_stack->addWidget(m_view);
    }
    m_view->load(buildRouteUrl(m_mode, m_fromLng, m_fromLat, m_toLng, m_toLat,
                               m_stationName, m_fromDescription));
    m_stack->setCurrentWidget(m_view);
    m_loaded = true;
}
