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
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

namespace {

// 腾讯导航 H5 自身使用已废弃的 Application Cache（<html manifest="nav.appcache">），
// Chromium 会反复输出 deprecation / origin-trial 警告。这是对方页面行为、不影响
// 功能且无法由我方修复，只把这两条已知噪音从应用日志滤掉，其余 JS 控制台消息
// 照常输出（便于调试）
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
        // 腾讯 routeplan 页面按 UA 分流：默认桌面 UA 返回横屏桌面版 H5，竖屏
        // 窗口内出现横向滚动。为本对话框单独建 profile 设移动端 UA（默认
        // profile 与 mapbridge 定位页共享，不能全局改），使其返回竖屏移动版。
        // profile 必须比 page 长寿：挂在对话框上（晚于 m_stack 创建而最后析构），
        // 若挂在 view 上会先于 page 析构，触发 use-after-free 崩溃
        auto *profile = new QWebEngineProfile(this);
        profile->setHttpUserAgent(QStringLiteral(
            "Mozilla/5.0 (Linux; Android 13; Pixel 6) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36"));
        // 移动版 H5 的按钮直接绑定 touchend/touchstart（如地图缩放、下载入口），
        // 桌面 WebEngine 只派发鼠标事件，这些处理器永远不会触发。在导航专用
        // profile 注入 DocumentCreation/MainWorld 脚本，把鼠标按下/移动/抬起
        // 翻译成对应触摸事件；脚本仅在无真实触摸能力的环境启用
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
    document.addEventListener('mousedown', function (e) { pressTarget = e.target; fire('touchstart', e); }, true);
    document.addEventListener('mousemove', function (e) { if (pressTarget) fire('touchmove', e); }, true);
    document.addEventListener('mouseup', function (e) { if (pressTarget) { fire('touchend', e); pressTarget = null; } }, true);
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
#endif
}
