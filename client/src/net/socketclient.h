#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>

class QTcpSocket;

class SocketClient : public QObject
{
    Q_OBJECT
public:
    // 业务回调 code < 0 为客户端本地错误：连接中断（结果未知）、超时、未连接
    static constexpr int kErrConnectionLost = -1;
    static constexpr int kErrTimeout = -2;
    static constexpr int kErrNotConnected = -3;

    using ResponseCallback = std::function<void(int code, const QString &msg, const QJsonObject &data)>;

    explicit SocketClient(QObject *parent = nullptr);
    ~SocketClient() override;

    void open(const QString &host, quint16 port);
    void logout();
    bool isConnected() const;
    bool isLoggedIn() const;
    QString phone() const;
    QString serverDescription() const;

    qint64 sendRequest(const QString &type, const QJsonObject &payload, ResponseCallback cb);
    void login(const QString &phone, ResponseCallback cb);

signals:
    void connected();
    void connectionLost(const QString &reason);
    void reconnectScheduled(int msec);
    void reloginFinished(bool ok, const QString &msg);
    void protocolError(const QString &detail);

private:
    void onReadyRead();
    void onDisconnected();
    void onSocketError();
    void handleLine(const QByteArray &line);
    void failAllPending(int code, const QString &msg);
    void startReconnect();
    void autoRelogin();
    void setPending(qint64 seq, const ResponseCallback &cb);
    ResponseCallback takePending(qint64 seq);

    QTcpSocket *m_socket;
    QByteArray m_buffer;
    qint64 m_nextSeq = 1;
    QHash<qint64, ResponseCallback> m_pending;
    QHash<qint64, QTimer *> m_timers;
    QTimer *m_pingTimer;
    QTimer *m_reconnectTimer;
    QString m_host;
    quint16 m_port = 8888;
    QString m_phone;
    bool m_loggedIn = false;
    bool m_manualClose = false;
    bool m_reloginPending = false;
    QString m_lastError;
};
