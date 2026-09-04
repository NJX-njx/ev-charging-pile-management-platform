#include <QApplication>
#include <QFile>

#include "net/socketclient.h"
#include "ui/loginwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC")));

    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    SocketClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), 8888);

    LoginWindow login(&client);
    login.show();

    return app.exec();
}
