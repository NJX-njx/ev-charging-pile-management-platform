#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QTcpSocket;
class QTimer;

class SocketClient : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(int code, const QString &msg, const QJsonObject &data)>;

    explicit SocketClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    // 放弃当前连接与自动重连（登录页连接超时/更换地址时使用）
    void abortConnection();
    int sendRequest(const QString &type, const QJsonObject &payload, Callback callback);
    bool isConnected() const;
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }

signals:
    void connected();
    void disconnected();
    void requestError(const QString &msg);

private slots:
    void onReadyRead();

private:
    QTcpSocket *m_socket;
    QTimer *m_heartbeatTimer;
    QTimer *m_reconnectTimer;
    QString m_host;
    quint16 m_port = 0;
    QByteArray m_buffer;
    int m_seq = 0;
    QHash<int, Callback> m_callbacks;
};
