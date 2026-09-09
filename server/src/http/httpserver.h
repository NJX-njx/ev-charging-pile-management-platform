#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;

// Read-only JSON API; runs on the main thread and closes each connection after responding.
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
