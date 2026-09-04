#include "database.h"
#include "httpserver.h"
#include "tcpserver.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>

namespace {

bool parsePort(const QCommandLineParser &parser, const QCommandLineOption &option,
               quint16 fallback, quint16 &out)
{
    if (!parser.isSet(option)) {
        out = fallback;
        return true;
    }
    bool ok = false;
    const ulong value = parser.value(option).toULong(&ok);
    if (!ok || value == 0 || value > 65535)
        return false;
    out = static_cast<quint16>(value);
    return true;
}

bool parseAddress(const QString &text, QHostAddress &out)
{
    if (text.compare(QLatin1String("any"), Qt::CaseInsensitive) == 0) {
        out = QHostAddress(QHostAddress::AnyIPv4);
        return true;
    }
    QHostAddress address(text);
    if (address.isNull())
        return false;
    out = address;
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("server"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("EV charging pile management platform socket server"));
    parser.addHelpOption();
    const QCommandLineOption tcpPort(QStringLiteral("tcp-port"),
                                     QStringLiteral("TCP listen port"), QStringLiteral("port"),
                                     QStringLiteral("8888"));
    const QCommandLineOption httpPort(QStringLiteral("http-port"),
                                      QStringLiteral("HTTP listen port"), QStringLiteral("port"),
                                      QStringLiteral("8080"));
    const QCommandLineOption tcpHost(QStringLiteral("tcp-host"),
                                     QStringLiteral("TCP bind address"), QStringLiteral("address"),
                                     QStringLiteral("0.0.0.0"));
    const QCommandLineOption httpHost(QStringLiteral("http-host"),
                                      QStringLiteral("HTTP bind address"),
                                      QStringLiteral("address"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption dbPath(QStringLiteral("db"),
                                    QStringLiteral("SQLite database file path"),
                                    QStringLiteral("path"), QStringLiteral("./charging.db"));
    parser.addOption(tcpPort);
    parser.addOption(httpPort);
    parser.addOption(tcpHost);
    parser.addOption(httpHost);
    parser.addOption(dbPath);
    parser.process(app);

    quint16 tcpPortValue = 0, httpPortValue = 0;
    if (!parsePort(parser, tcpPort, 8888, tcpPortValue)
        || !parsePort(parser, httpPort, 8080, httpPortValue)) {
        qCritical() << "invalid port argument";
        return 1;
    }
    QHostAddress tcpAddress, httpAddress;
    if (!parseAddress(parser.value(tcpHost), tcpAddress)
        || !parseAddress(parser.value(httpHost), httpAddress)) {
        qCritical() << "invalid host argument";
        return 1;
    }

    Database::configure(parser.value(dbPath));
    QString error;
    if (!Database::initialize(&error)) {
        qCritical() << "failed to initialize database:" << error;
        return 1;
    }

    TcpServer tcpServer;
    if (!tcpServer.listen(tcpAddress, tcpPortValue)) {
        qCritical() << "failed to listen on TCP" << tcpAddress.toString() << tcpPortValue
                    << ":" << tcpServer.errorString();
        return 1;
    }
    HttpServer httpServer;
    if (!httpServer.listenOn(httpAddress, httpPortValue)) {
        qCritical() << "failed to listen on HTTP" << httpAddress.toString() << httpPortValue;
        return 1;
    }
    qInfo() << "TCP listening on" << tcpAddress.toString() << tcpPortValue;
    qInfo() << "HTTP listening on" << httpAddress.toString() << httpPortValue;
    qInfo() << "database:" << parser.value(dbPath);
    return app.exec();
}
