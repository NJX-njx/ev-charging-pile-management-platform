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
        QStringLiteral("电动汽车充电桩管理平台服务端"));
    parser.addHelpOption();
    const QCommandLineOption tcpPort(QStringLiteral("tcp-port"),
                                     QStringLiteral("业务接口监听端口"), QStringLiteral("port"),
                                     QStringLiteral("8888"));
    const QCommandLineOption httpPort(QStringLiteral("http-port"),
                                      QStringLiteral("网页接口监听端口"), QStringLiteral("port"),
                                      QStringLiteral("8080"));
    const QCommandLineOption tcpHost(QStringLiteral("tcp-host"),
                                     QStringLiteral("业务接口监听地址"), QStringLiteral("address"),
                                     QStringLiteral("0.0.0.0"));
    const QCommandLineOption httpHost(QStringLiteral("http-host"),
                                      QStringLiteral("网页接口监听地址"),
                                      QStringLiteral("address"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption dbPath(QStringLiteral("db"),
                                    QStringLiteral("数据库文件路径"),
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
        qCritical() << "端口参数无效";
        return 1;
    }
    QHostAddress tcpAddress, httpAddress;
    if (!parseAddress(parser.value(tcpHost), tcpAddress)
        || !parseAddress(parser.value(httpHost), httpAddress)) {
        qCritical() << "监听地址参数无效";
        return 1;
    }

    Database::configure(parser.value(dbPath));
    QString error;
    if (!Database::initialize(&error)) {
        qCritical() << "初始化数据库失败：" << error;
        return 1;
    }

    TcpServer tcpServer;
    if (!tcpServer.listen(tcpAddress, tcpPortValue)) {
        qCritical() << "业务接口监听失败" << tcpAddress.toString() << tcpPortValue
                    << ":" << tcpServer.errorString();
        return 1;
    }
    HttpServer httpServer;
    if (!httpServer.listenOn(httpAddress, httpPortValue)) {
        qCritical() << "网页接口监听失败" << httpAddress.toString() << httpPortValue;
        return 1;
    }
    qInfo() << "业务接口正在监听" << tcpAddress.toString() << tcpPortValue;
    qInfo() << "网页接口正在监听" << httpAddress.toString() << httpPortValue;
    qInfo() << "数据库路径：" << parser.value(dbPath);
    return app.exec();
}
