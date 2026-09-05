#include "socketclient.h"

#include <QJsonDocument>
#include <QSignalBlocker>
#include <QTcpSocket>

namespace {
constexpr int kRequestTimeoutMs = 15000;
constexpr int kPingIntervalMs = 15000;
constexpr int kReconnectDelayMs = 3000;
constexpr qint64 kMaxMessageBytes = 2 * 1024 * 1024;
}

SocketClient::SocketClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_pingTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_pingTimer->setInterval(kPingIntervalMs);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::readyRead, this, &SocketClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &SocketClient::onDisconnected);
    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        m_pingTimer->start();
        emit connected();
        if (!m_phone.isEmpty() && m_loggedIn && !m_reloginPending)
            autoRelogin();
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        onSocketError();
    });
    connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        if (m_socket->state() == QAbstractSocket::ConnectedState)
            sendRequest(QStringLiteral("ping"), QJsonObject{}, [](int, const QString &, const QJsonObject &) {});
    });
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_socket->state() == QAbstractSocket::UnconnectedState)
            open(m_host, m_port);
    });
}

SocketClient::~SocketClient() = default;

void SocketClient::open(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_manualClose = false;
    m_buffer.clear();
    m_nextSeq = 1;
    failAllPending(kErrNotConnected, QStringLiteral("连接已重建，请求未完成"));
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        const QSignalBlocker blocker(m_socket);
        m_socket->abort();
    }
    m_socket->connectToHost(host, port);
}

void SocketClient::logout()
{
    m_manualClose = true;
    m_loggedIn = false;
    m_phone.clear();
    m_password.clear();
    m_code.clear();
    m_pingTimer->stop();
    m_reconnectTimer->stop();
    failAllPending(kErrNotConnected, QStringLiteral("已退出登录"));
    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
}

bool SocketClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool SocketClient::isLoggedIn() const
{
    return m_loggedIn;
}

QString SocketClient::phone() const
{
    return m_phone;
}

QString SocketClient::serverDescription() const
{
    return QStringLiteral("%1:%2").arg(m_host).arg(m_port);
}

qint64 SocketClient::sendRequest(const QString &type, const QJsonObject &payload, ResponseCallback cb)
{
    if (!isConnected()) {
        if (cb) {
            QTimer::singleShot(0, this, [cb]() {
                cb(kErrNotConnected, QStringLiteral("未连接到服务器"), QJsonObject{});
            });
        }
        return -1;
    }

    const qint64 seq = m_nextSeq++;
    QJsonObject req;
    req.insert(QStringLiteral("seq"), static_cast<double>(seq));
    req.insert(QStringLiteral("type"), type);
    req.insert(QStringLiteral("payload"), payload);
    const QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    if (m_socket->write(line) == -1) {
        if (cb) {
            QTimer::singleShot(0, this, [cb]() {
                cb(kErrConnectionLost, QStringLiteral("发送失败，操作结果未知"), QJsonObject{});
            });
        }
        return -1;
    }
    if (cb)
        setPending(seq, cb);
    return seq;
}

void SocketClient::login(const QString &phone, const QString &password, const QString &code,
                         ResponseCallback cb)
{
    m_phone = phone;
    QJsonObject payload{{QStringLiteral("phone"), phone}};
    if (!password.isEmpty())
        payload.insert(QStringLiteral("password"), password);
    else if (!code.isEmpty())
        payload.insert(QStringLiteral("code"), code);
    sendRequest(QStringLiteral("user_login"), payload,
                [this, password, code, cb](int rc, const QString &msg, const QJsonObject &data) {
                    if (rc == 0) {
                        m_loggedIn = true;
                        m_password = password;
                        m_code = code;
                    }
                    if (cb)
                        cb(rc, msg, data);
                });
}

void SocketClient::setSessionPassword(const QString &password)
{
    m_password = password;
}

void SocketClient::setPending(qint64 seq, const ResponseCallback &cb)
{
    m_pending.insert(seq, cb);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kRequestTimeoutMs);
    m_timers.insert(seq, timer);
    connect(timer, &QTimer::timeout, this, [this, seq]() {
        ResponseCallback cb = takePending(seq);
        if (cb)
            invokeCallback(cb, kErrTimeout, QStringLiteral("请求超时，请稍后重试或刷新状态"),
                           QJsonObject{});
    });
    timer->start();
}

SocketClient::ResponseCallback SocketClient::takePending(qint64 seq)
{
    ResponseCallback cb = m_pending.take(seq);
    QTimer *timer = m_timers.take(seq);
    if (timer) {
        timer->stop();
        timer->deleteLater();
    }
    return cb;
}

void SocketClient::failAllPending(int code, const QString &msg)
{
    const QList<qint64> seqs = m_pending.keys();
    for (qint64 seq : seqs) {
        ResponseCallback cb = takePending(seq);
        if (cb)
            invokeCallback(cb, code, msg, QJsonObject{});
    }
}

// 业务回调一律排队到下一轮事件循环再执行：回调里经常会弹模态框/嵌套事件循环，
// 若直接在 readyRead/disconnected 等 SocketClient 信号处理栈内执行，readyRead
// 在槽返回前不会再次发射（Qt 不递归发射），后续服务端响应会被饿死，进而引发
// 请求超时、按钮卡死，以及回调访问已销毁对象的崩溃。
void SocketClient::invokeCallback(const ResponseCallback &cb, int code, const QString &msg,
                                  const QJsonObject &data)
{
    QTimer::singleShot(0, this, [cb, code, msg, data]() {
        cb(code, msg, data);
    });
}

void SocketClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    if (!m_buffer.contains('\n') && m_buffer.size() > kMaxMessageBytes) {
        emit protocolError(QStringLiteral("服务端消息超过 2 MiB，断开连接"));
        m_socket->abort();
        return;
    }
    int idx;
    while ((idx = m_buffer.indexOf('\n')) != -1) {
        QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;
        handleLine(line);
    }
}

void SocketClient::handleLine(const QByteArray &line)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit protocolError(QStringLiteral("无法解析的服务端消息"));
        return;
    }
    const QJsonObject obj = doc.object();
    const qint64 seq = static_cast<qint64>(obj.value(QStringLiteral("seq")).toDouble(-1));
    const int code = obj.value(QStringLiteral("code")).toInt(-1);
    const QString msg = obj.value(QStringLiteral("msg")).toString();
    const QJsonValue dataVal = obj.value(QStringLiteral("data"));

    if (seq == -1 || obj.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        emit protocolError(msg.isEmpty() ? QStringLiteral("服务端返回错误信封") : msg);
        return;
    }

    ResponseCallback cb = takePending(seq);
    if (!cb) {
        emit protocolError(QStringLiteral("收到未匹配的响应 seq=%1").arg(seq));
        return;
    }
    invokeCallback(cb, code, msg, dataVal.isObject() ? dataVal.toObject() : QJsonObject{});
}

void SocketClient::onDisconnected()
{
    m_pingTimer->stop();
    failAllPending(kErrConnectionLost, QStringLiteral("网络中断，操作结果未知，请刷新状态确认"));
    if (m_manualClose)
        return;
    const QString reason = m_lastError.isEmpty() ? QStringLiteral("连接已断开") : m_lastError;
    m_lastError.clear();
    emit connectionLost(reason);
    startReconnect();
}

void SocketClient::onSocketError()
{
    m_lastError = m_socket->errorString();
}

void SocketClient::startReconnect()
{
    if (m_manualClose || m_reconnectTimer->isActive())
        return;
    emit reconnectScheduled(kReconnectDelayMs);
    m_reconnectTimer->start(kReconnectDelayMs);
}

void SocketClient::autoRelogin()
{
    m_reloginPending = true;
    QJsonObject payload{{QStringLiteral("phone"), m_phone}};
    if (!m_password.isEmpty())
        payload.insert(QStringLiteral("password"), m_password);
    else if (!m_code.isEmpty())
        payload.insert(QStringLiteral("code"), m_code);
    sendRequest(QStringLiteral("user_login"), payload,
                [this](int rc, const QString &msg, const QJsonObject &data) {
                    m_reloginPending = false;
                    m_loggedIn = (rc == 0);
                    emit reloginFinished(rc == 0, msg, data);
                });
}
