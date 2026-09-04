#pragma once

#include <QObject>

#include <functional>

class QWebEnginePage;

class MapBridge : public QObject
{
    Q_OBJECT
public:
    using GeocodeCallback = std::function<void(bool ok, double lng, double lat, const QString &error)>;

    explicit MapBridge(QObject *parent = nullptr);
    ~MapBridge() override;

    static bool isConfigured();
    bool isReady() const;
    void geocode(const QString &address, GeocodeCallback cb);

private:
    void ensurePage();
    void runPending();
    void failPending(const QString &error);

    QWebEnginePage *m_page = nullptr;
    bool m_ready = false;
    QString m_pendingAddress;
    GeocodeCallback m_cb;
};
