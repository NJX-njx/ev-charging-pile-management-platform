#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QTcpServer>

// 监听线程接收连接，将套接字描述符交给独立工作线程。
// 每个连接对应一个工作线程。
class TcpServer : public QTcpServer
{
    Q_OBJECT
public:
    using QTcpServer::QTcpServer;

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};

#endif // TCPSERVER_H
