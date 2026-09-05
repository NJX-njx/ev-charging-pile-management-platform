#ifndef CONNECTION_H
#define CONNECTION_H

#include "handlers.h"

#include <QByteArray>
#include <QObject>

class QTcpSocket;

// Lives in its own QThread; owns the socket, the receive buffer and the session.
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
