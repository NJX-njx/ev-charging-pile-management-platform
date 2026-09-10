#pragma once
#include <QObject>
#include <functional>
class QNetworkAccessManager;

// 地址解析结果保留匹配地点，便于用户核对。
struct GeocodeResult {
    bool ok = false;
    double lng = 0;
    double lat = 0;
    QString title;
    QString error;
};
class MapBridge : public QObject {
    Q_OBJECT
public:
    using GeocodeCallback = std::function<void(const GeocodeResult &)>;
    explicit MapBridge(QObject *parent = nullptr);
    void geocode(const QString &address, GeocodeCallback cb);
private:
    QNetworkAccessManager *m_network;
};
