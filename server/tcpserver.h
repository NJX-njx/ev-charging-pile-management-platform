#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QTcpServer>

// Accepts sockets on the listening thread and hands each descriptor
// to a dedicated worker QThread (classic Qt per-connection threading).
class TcpServer : public QTcpServer
{
    Q_OBJECT
public:
    using QTcpServer::QTcpServer;

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};

#endif // TCPSERVER_H
