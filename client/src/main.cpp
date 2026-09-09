#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QLoggingCategory>

#include "mainwindow.h"
#include "model/appconfig.h"

namespace {

// 会话指定的输入法插件缺失时，选择当前安装中可用的插件。
void ensureImModulePluginAvailable()
{
    const QDir pluginDir(QLibraryInfo::path(QLibraryInfo::PluginsPath)
                         + QStringLiteral("/platforminputcontexts"));
    const QStringList plugins = pluginDir.entryList(QDir::Files);
    // 模块名可能只对应插件文件名的前缀。
    auto hasPlugin = [&plugins](const QString &module) {
        for (const QString &f : plugins)
            if (f.startsWith(QLatin1String("lib") + module, Qt::CaseInsensitive))
                return true;
        return false;
    };
    const QByteArray current = qgetenv("QT_IM_MODULE");
    if (!current.isEmpty() && current != "none"
        && hasPlugin(QString::fromUtf8(current)))
        return; // 会话指定的输入法插件可用，不干预
    for (const char *candidate : {"fcitx", "ibus", "compose"}) {
        if (hasPlugin(QLatin1String(candidate))) {
            qputenv("QT_IM_MODULE", candidate);
            return;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // WebEngine 渲染要求在创建 QApplication 前启用 GL 上下文共享。
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    // 关闭 WebEngine 初始化参数日志。
    QLoggingCategory::setFilterRules(QStringLiteral("qt.webenginecontext.debug=false\n"
                                                    "qt.webenginecontext.info=false"));
    ensureImModulePluginAvailable();
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
