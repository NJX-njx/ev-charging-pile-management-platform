#ifndef CONNECTION_H
#define CONNECTION_H

#include "handlers.h"

#include <QByteArray>
#include <QObject>

class QTcpSocket;

// 在独立线程中维护套接字、接收缓冲区和登录会话。
class Connection : public QObject
{
    Q_OBJECT
public:
    explicit Connection(qintptr socketDescriptor, QObject *parent = nullptr);

public slots:
    void init();

signals:
    void finished();

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void processLine(const QByteArray &line);
    void sendAndClose(const QByteArray &data);

    qintptr m_socketDescriptor;
    QString m_dbName;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
    Session m_session;
    bool m_closing = false;
};

#endif // CONNECTION_H
