#include "tcpserver.h"

#include "connection.h"

#include <QThread>

void TcpServer::incomingConnection(qintptr socketDescriptor)
{
    QThread *thread = new QThread;
    Connection *connection = new Connection(socketDescriptor);
    connection->moveToThread(thread);
    connect(thread, &QThread::started, connection, &Connection::init);
    connect(connection, &Connection::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, connection, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
