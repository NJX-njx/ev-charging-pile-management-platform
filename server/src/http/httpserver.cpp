#include "httpserver.h"

#include "database.h"
#include "stats.h"
#include "timeutil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>

namespace {
constexpr qint64 kMaxHeaderBytes = 64 * 1024;

QJsonObject body(int code, const QString &msg, const QJsonValue &data)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("code"), code);
    obj.insert(QStringLiteral("msg"), msg);
    obj.insert(QStringLiteral("data"), data);
    return obj;
}
} // namespace

bool HttpServer::listenOn(const QHostAddress &address, quint16 port)
{
    connect(&m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
    return m_server.listen(address, port);
}

void HttpServer::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &HttpServer::onDisconnected);
    }
}

void HttpServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_buffers.contains(socket))
        return;
    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer.size() > kMaxHeaderBytes) {
            sendJson(socket, 400, body(2001, QStringLiteral("bad request"),
                                       QJsonValue(QJsonValue::Null)));
            socket->flush();
            socket->disconnectFromHost();
        }
        return;
    }
    const QByteArray request = buffer.left(headerEnd);
    handleRequest(socket, request);
    socket->flush();
    socket->disconnectFromHost();
}

void HttpServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    m_buffers.remove(socket);
    socket->deleteLater();
}

void HttpServer::handleRequest(QTcpSocket *socket, const QByteArray &request)
{
    const QList<QByteArray> lines = request.split('\r');
    const QList<QByteArray> parts = lines.value(0).trimmed().split(' ');
    if (parts.size() < 2) {
        sendJson(socket, 400, body(2001, QStringLiteral("bad request"),
                                   QJsonValue(QJsonValue::Null)));
        return;
    }
    const QByteArray method = parts.at(0);
    const QByteArray target = parts.at(1);
    const int queryStart = target.indexOf('?');
    const QByteArray path = queryStart < 0 ? target : target.left(queryStart);
    const QByteArray query = queryStart < 0 ? QByteArray() : target.mid(queryStart + 1);

    if (method != "GET"
        || (path != "/api/v1/dashboard/overview" && path != "/api/v1/health")) {
        sendJson(socket, 404, body(2002, QStringLiteral("not found"),
                                   QJsonValue(QJsonValue::Null)));
        return;
    }

    if (path == "/api/v1/health") {
        QJsonObject data;
        data.insert(QStringLiteral("status"), QStringLiteral("up"));
        data.insert(QStringLiteral("serverTime"), TimeUtil::isoFromSecs(TimeUtil::nowSecs()));
        sendJson(socket, 200, body(0, QStringLiteral("ok"), data));
        return;
    }

    int range = 7;
    bool rangeValid = true;
    bool rangeSeen = false;
    const QList<QByteArray> params = query.split('&');
    for (const QByteArray &param : params) {
        if (param.isEmpty())
            continue;
        const int eq = param.indexOf('=');
        const QByteArray key = eq < 0 ? param : param.left(eq);
        const QByteArray value = eq < 0 ? QByteArray() : param.mid(eq + 1);
        if (key == "range") {
            rangeSeen = true;
            if (value == "7")
                range = 7;
            else if (value == "30")
                range = 30;
            else
                rangeValid = false;
        }
    }
    if (rangeSeen && !rangeValid) {
        sendJson(socket, 400, body(2001, QStringLiteral("range must be 7 or 30"),
                                   QJsonValue(QJsonValue::Null)));
        return;
    }

    const QSqlDatabase db = Database::connection(QStringLiteral("http"));
    QJsonObject data;
    data.insert(QStringLiteral("generatedAt"), TimeUtil::isoFromSecs(TimeUtil::nowSecs()));
    data.insert(QStringLiteral("revenue"), Stats::revenueSummary(db));
    data.insert(QStringLiteral("orders"), Stats::orderCounts(db));
    data.insert(QStringLiteral("energy"), Stats::energySums(db));
    data.insert(QStringLiteral("revenueTrend"), Stats::revenueTrend(db, range));
    data.insert(QStringLiteral("pileStatus"), Stats::pileStatusOverview(db));
    QJsonArray stations = Stats::stationSummaries(db);
    for (qsizetype i = 0; i < stations.size(); ++i) {
        QJsonObject station = stations.at(i).toObject();
        station.remove(QStringLiteral("lng"));
        station.remove(QStringLiteral("lat"));
        stations.replace(i, station);
    }
    data.insert(QStringLiteral("stations"), stations);
    sendJson(socket, 200, body(0, QStringLiteral("ok"), data));
}

void HttpServer::sendJson(QTcpSocket *socket, int statusCode, const QJsonObject &responseBody)
{
    const QByteArray payload = QJsonDocument(responseBody).toJson(QJsonDocument::Compact);
    const char *reason = "OK";
    if (statusCode == 400)
        reason = "Bad Request";
    else if (statusCode == 404)
        reason = "Not Found";
    else if (statusCode == 500)
        reason = "Internal Server Error";
    QByteArray head = "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + reason + "\r\n"
        + "Content-Type: application/json; charset=utf-8\r\n"
        + "Cache-Control: no-store\r\n"
        + "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
        + "Connection: close\r\n\r\n";
    socket->write(head);
    socket->write(payload);
}
