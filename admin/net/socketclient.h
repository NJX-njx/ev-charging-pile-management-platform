#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QTcpSocket;

class SocketClient : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(int code, const QString &msg, const QJsonObject &data)>;

    explicit SocketClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    int sendRequest(const QString &type, const QJsonObject &payload, Callback callback);
    bool isConnected() const;

signals:
    void connected();
    void disconnected();

private slots:
    void onReadyRead();

private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;
    int m_seq = 0;
    QHash<int, Callback> m_callbacks;
};
