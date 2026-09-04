#include "socketclient.h"

#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>

SocketClient::SocketClient(QObject *parent)
    : QObject(parent), m_socket(new QTcpSocket(this)),
      m_heartbeatTimer(new QTimer(this)), m_reconnectTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::readyRead, this, &SocketClient::onReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        m_reconnectTimer->stop();
        emit connected();
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        emit disconnected();
        if (!m_host.isEmpty())
            m_reconnectTimer->start();
    });

    // 断线后持续重试，直到连上（连接失败只触发 errorOccurred，不会再触发 disconnected）
    m_reconnectTimer->setInterval(3000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!isConnected() && m_socket->state() != QAbstractSocket::ConnectingState)
            m_socket->connectToHost(m_host, m_port);
    });

    // 协议 5.1：心跳，每 10~30 秒一次
    m_heartbeatTimer->setInterval(15000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (isConnected())
            sendRequest(QStringLiteral("ping"), QJsonObject(), nullptr);
    });
    m_heartbeatTimer->start();
}

void SocketClient::connectToServer(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_socket->connectToHost(host, port);
}

bool SocketClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

int SocketClient::sendRequest(const QString &type, const QJsonObject &payload, Callback callback)
{
    if (!isConnected()) {
        emit requestError(QStringLiteral("未连接到服务器"));
        if (callback)
            callback(-1, QStringLiteral("未连接到服务器"), QJsonObject());
        return -1;
    }

    const int seq = ++m_seq;
    QJsonObject msg;
    msg[QStringLiteral("seq")] = seq;
    msg[QStringLiteral("type")] = type;
    msg[QStringLiteral("payload")] = payload;

    m_socket->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');

    if (callback)
        m_callbacks.insert(seq, callback);
    return seq;
}

void SocketClient::onReadyRead()
{
    m_buffer += m_socket->readAll();

    int idx;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        if (line.trimmed().isEmpty())
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        const int seq = obj[QStringLiteral("seq")].toInt(-1);
        const int code = obj[QStringLiteral("code")].toInt(-1);
        const QString msg = obj[QStringLiteral("msg")].toString();
        const QJsonObject data = obj[QStringLiteral("data")].isObject()
                                     ? obj[QStringLiteral("data")].toObject()
                                     : QJsonObject();

        if (code != 0)
            emit requestError(msg);

        Callback callback = m_callbacks.take(seq);
        if (callback)
            callback(code, msg, data);
    }
}
