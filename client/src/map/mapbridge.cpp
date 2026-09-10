#include "mapbridge.h"
#include "tencentmapkey.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <cmath>

MapBridge::MapBridge(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

void MapBridge::geocode(const QString &address, GeocodeCallback cb)
{
    QString url = QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/?address=@ADDR@&key=@KEY@");
    url.replace(QStringLiteral("@ADDR@"), QString::fromUtf8(QUrl::toPercentEncoding(address)))
       .replace(QStringLiteral("@KEY@"), mapconfig::kTencentMapKey);
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000);
    QNetworkReply *reply = m_network->get(request);
    // 接收对象销毁时连接自动断开，不回调已经关闭的页面。
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        GeocodeResult result;
        if (reply->error() != QNetworkReply::NoError) {
            result.error = QStringLiteral("地址解析请求失败或超时（错误码：%1）").arg(int(reply->error()));
            cb(result);
            return;
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &error);
        const QJsonObject root = doc.object();
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error = QStringLiteral("地图服务返回的数据格式错误");
        } else if (root.value(QStringLiteral("status")).toInt(-1) != 0) {
            result.error = QStringLiteral("地址解析失败（服务状态码：%1），请核对地址和地图授权")
                               .arg(root.value(QStringLiteral("status")).toInt(-1));
        } else {
            const QJsonObject data = root.value(QStringLiteral("result")).toObject();
            const QJsonObject location = data.value(QStringLiteral("location")).toObject();
            result.lng = location.value(QStringLiteral("lng")).toDouble(qQNaN());
            result.lat = location.value(QStringLiteral("lat")).toDouble(qQNaN());
            result.title = data.value(QStringLiteral("title")).toString();
            result.ok = std::isfinite(result.lng) && std::isfinite(result.lat)
                && result.lng >= -180 && result.lng <= 180 && result.lat >= -90 && result.lat <= 90;
            if (!result.ok) result.error = QStringLiteral("地图服务未返回有效坐标");
        }
        cb(result);
    });
}
