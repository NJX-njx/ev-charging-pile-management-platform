#include "connection.h"

#include "database.h"
#include "protocol.h"

#include <QAtomicInt>
#include <QTcpSocket>

namespace {
constexpr qint64 kMaxMessageBytes = 2 * 1024 * 1024;
}

Connection::Connection(qintptr socketDescriptor, QObject *parent)
    : QObject(parent)
    , m_socketDescriptor(socketDescriptor)
{
    static QAtomicInt counter = 0;
    m_dbName = QStringLiteral("tcp-%1").arg(counter.fetchAndAddRelaxed(1) + 1);
}

void Connection::init()
{
    m_socket = new QTcpSocket(this);
    if (!m_socket->setSocketDescriptor(m_socketDescriptor)) {
        emit finished();
        return;
    }
    connect(m_socket, &QTcpSocket::readyRead, this, &Connection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &Connection::onDisconnected);
}

void Connection::onReadyRead()
{
    if (m_closing)
        return;
    m_buffer.append(m_socket->readAll());
    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0) {
            if (m_buffer.size() > kMaxMessageBytes) {
                m_buffer.clear();
                Response r{4001, QStringLiteral("message too large"),
                           QJsonValue(QJsonValue::Null)};
                sendAndClose(Protocol::buildResponse(-1, QStringLiteral("error"), r));
            }
            return;
        }
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.size() + 1 > kMaxMessageBytes) {
            Response r{4001, QStringLiteral("message too large"),
                       QJsonValue(QJsonValue::Null)};
            sendAndClose(Protocol::buildResponse(-1, QStringLiteral("error"), r));
            return;
        }
        processLine(line);
        if (m_closing)
            return;
    }
}

void Connection::processLine(const QByteArray &line)
{
    const Protocol::Envelope env = Protocol::parseEnvelope(line);
    if (!env.ok) {
        Response r{3001, QStringLiteral("invalid message"), QJsonValue(QJsonValue::Null)};
        m_socket->write(Protocol::buildResponse(env.seq, env.type, r));
        return;
    }
    bool closeConnection = false;
    const Response r = Handlers::dispatch(env.type, env.payload, m_session,
                                          Database::connection(m_dbName), closeConnection);
    const QByteArray out = Protocol::buildResponse(env.seq, env.type, r);
    if (closeConnection) {
        sendAndClose(out);
        return;
    }
    m_socket->write(out);
}

void Connection::sendAndClose(const QByteArray &data)
{
    m_closing = true;
    m_socket->write(data);
    m_socket->flush();
    m_socket->disconnectFromHost();
}

void Connection::onDisconnected()
{
    Database::remove(m_dbName);
    emit finished();
}
