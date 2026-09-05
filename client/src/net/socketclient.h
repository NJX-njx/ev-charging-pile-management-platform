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
    // v2 双方式登录：password 与 code 二选一（同时缺省或同时提供服务端返回 2001）
    void login(const QString &phone, const QString &password, const QString &code, ResponseCallback cb);
    // 设置/修改密码成功后调用，使断线自动重登改用密码
    void setSessionPassword(const QString &password);

signals:
    void connected();
    void connectionLost(const QString &reason);
    void reconnectScheduled(int msec);
    void reloginFinished(bool ok, const QString &msg, const QJsonObject &data);
    void protocolError(const QString &detail);

private:
    void onReadyRead();
    void onDisconnected();
    void onSocketError();
    void handleLine(const QByteArray &line);
    void failAllPending(int code, const QString &msg);
    void invokeCallback(const ResponseCallback &cb, int code, const QString &msg,
                        const QJsonObject &data);
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
    QString m_password;
    QString m_code;
    bool m_loggedIn = false;
    bool m_manualClose = false;
    bool m_reloginPending = false;
    QString m_lastError;
};
