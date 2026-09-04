#include <QApplication>
#include <QCommandLineParser>
#include <QFile>

#include "mainwindow.h"
#include "model/appconfig.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("evcp-client"));
    QApplication::setOrganizationName(QStringLiteral("NeusoftEVCP"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("东软电动汽车充电桩应用管理平台 - Qt 用户端"));
    parser.addHelpOption();
    const QCommandLineOption hostOption(QStringLiteral("host"),
                                        QStringLiteral("Socket 服务端地址（默认取设置或 127.0.0.1）"),
                                        QStringLiteral("host"));
    const QCommandLineOption portOption(QStringLiteral("port"),
                                        QStringLiteral("Socket 服务端端口（默认取设置或 8888）"),
                                        QStringLiteral("port"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.process(app);

    AppConfig config = AppConfig::load();
    if (parser.isSet(hostOption))
        config.host = parser.value(hostOption);
    if (parser.isSet(portOption)) {
        bool ok = false;
        const quint32 port = parser.value(portOption).toUInt(&ok);
        if (ok && port > 0 && port <= 65535)
            config.port = static_cast<quint16>(port);
    }

    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    MainWindow w(config);
    w.show();
    return QApplication::exec();
}
