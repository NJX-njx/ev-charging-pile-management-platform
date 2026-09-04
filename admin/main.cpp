#include <QApplication>

#include "net/socketclient.h"
#include "ui/loginwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    SocketClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), 8888);

    LoginWindow login(&client);
    login.show();

    return app.exec();
}
