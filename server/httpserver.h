#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;

// Minimal HTTP/1.1 read-only JSON API (Qt 6.2 has no QHttpServer).
// Runs on the main thread; every request is answered then the connection closes.
class HttpServer : public QObject
{
    Q_OBJECT
public:
    bool listenOn(const QHostAddress &address, quint16 port);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleRequest(QTcpSocket *socket, const QByteArray &request);
    void sendJson(QTcpSocket *socket, int statusCode, const QJsonObject &body);

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

#endif // HTTPSERVER_H
