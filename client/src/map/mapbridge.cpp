#include "mapbridge.h"

#include "tencentmapkey.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#ifdef EVCP_HAVE_WEBENGINE
#include <QWebEnginePage>
#endif

namespace {
const QString kTitlePrefix = QStringLiteral("EVCP_GEOCODE:");
constexpr int kGeocodeTimeoutMs = 10000;
}

bool MapBridge::isConfigured()
{
#ifdef EVCP_HAVE_WEBENGINE
    return !mapconfig::kTencentMapKey.isEmpty();
#else
    return false;
#endif
}

MapBridge::MapBridge(QObject *parent)
    : QObject(parent)
{
}

MapBridge::~MapBridge() = default;

bool MapBridge::isReady() const
{
    return m_ready;
}

void MapBridge::geocode(const QString &address, GeocodeCallback cb)
{
#ifdef EVCP_HAVE_WEBENGINE
    if (m_cb) {
        cb(false, 0, 0, QStringLiteral("上一个地址解析尚未完成"));
        return;
    }
    m_pendingAddress = address;
    m_cb = std::move(cb);
    QTimer::singleShot(kGeocodeTimeoutMs, this, [this]() {
        if (m_cb)
            failPending(QStringLiteral("地址解析超时，请检查网络或改用手动输入经纬度"));
    });
    ensurePage();
    if (m_page && m_ready)
        runPending();
#else
    cb(false, 0, 0, QStringLiteral("本构建未包含 WebEngine 模块"));
#endif
}

void MapBridge::ensurePage()
{
#ifdef EVCP_HAVE_WEBENGINE
    if (m_page)
        return;
    QFile f(QStringLiteral(":/map.html"));
    if (!f.open(QIODevice::ReadOnly)) {
        failPending(QStringLiteral("地图资源缺失"));
        return;
    }
    QString html = QString::fromUtf8(f.readAll());
    html.replace(QStringLiteral("__TENCENT_MAP_KEY__"), mapconfig::kTencentMapKey);

    m_page = new QWebEnginePage(this);
    connect(m_page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        if (!ok)
            failPending(QStringLiteral("地图页面加载失败"));
    });
    connect(m_page, &QWebEnginePage::titleChanged, this, [this](const QString &title) {
        if (!title.startsWith(kTitlePrefix))
            return;
        const QJsonObject obj = QJsonDocument::fromJson(title.mid(kTitlePrefix.size()).toUtf8()).object();
        if (obj.contains(QStringLiteral("ready"))) {
            if (obj.value(QStringLiteral("ready")).toBool()) {
                m_ready = true;
                runPending();
            } else {
                failPending(QStringLiteral("地图 SDK 初始化失败：%1")
                                .arg(obj.value(QStringLiteral("error")).toString()));
            }
            return;
        }
        if (!m_cb)
            return;
        GeocodeCallback cb = std::move(m_cb);
        m_cb = nullptr;
        m_pendingAddress.clear();
        if (obj.value(QStringLiteral("ok")).toBool())
            cb(true, obj.value(QStringLiteral("lng")).toDouble(),
               obj.value(QStringLiteral("lat")).toDouble(), QString());
        else {
            const QString detail = obj.value(QStringLiteral("error")).toString();
            cb(false, 0, 0,
               detail.isEmpty()
                   ? QStringLiteral("地址解析失败，请换更详细的地址或手动输入经纬度")
                   : QStringLiteral("地址解析失败：%1").arg(detail));
        }
    });
    m_page->setHtml(html, QUrl(QStringLiteral("https://map.qq.com/")));
#else
    failPending(QStringLiteral("本构建未包含 WebEngine 模块"));
#endif
}

void MapBridge::runPending()
{
#ifdef EVCP_HAVE_WEBENGINE
    if (!m_cb || !m_page || !m_ready)
        return;
    const QString js = QStringLiteral("geocodeAddress(%1)")
                           .arg(QString::fromUtf8(QJsonDocument(QJsonArray{m_pendingAddress})
                                                      .toJson(QJsonDocument::Compact)));
    m_page->runJavaScript(js);
#endif
}

void MapBridge::failPending(const QString &error)
{
    if (!m_cb)
        return;
    GeocodeCallback cb = std::move(m_cb);
    m_cb = nullptr;
    m_pendingAddress.clear();
    cb(false, 0, 0, error);
}
