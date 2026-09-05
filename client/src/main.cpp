#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>

#include "mainwindow.h"
#include "model/appconfig.h"

namespace {

// 中文输入法（IME）环境兜底：课程虚拟机桌面会话使用 fcitx5（im-config 注入
// QT_IM_MODULE=fcitx），但 Ubuntu 22.04 的 Qt6 没有对应的平台输入上下文插件
//（fcitx5-frontend-qt6 不在 apt 源中），Qt 找不到插件后输入法整体失效，
// 所有输入框无法输入中文。fcitx5 自带 IBus Frontend（提供 ibus 协议），而 Qt6
// 自带 ibus 插件，因此当会话指定的模块插件缺失时回退到第一个可用插件。
// 输入框本身的输入法不受限（各字段按类型显式设置 inputMethodHints）。
void ensureImModulePluginAvailable()
{
    const QDir pluginDir(QLibraryInfo::path(QLibraryInfo::PluginsPath)
                         + QStringLiteral("/platforminputcontexts"));
    const QStringList plugins = pluginDir.entryList(QDir::Files);
    // 模块名与插件文件名不总一致（fcitx -> libfcitx5platforminputcontextplugin.so），
    // 只在本 Qt 的插件目录内按文件名前缀匹配，不会误认其他 Qt 版本的插件
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
