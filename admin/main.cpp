#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>

#include "net/socketclient.h"
#include "ui/loginwindow.h"

// 会话指定的输入法插件缺失时，选择当前安装中可用的插件。
static void sanitizeInputMethodEnvironment()
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
            if (current != candidate)
                qWarning("QT_IM_MODULE=%s 在当前 Qt 无对应插件，回退到 %s",
                         qPrintable(QString::fromUtf8(current)), candidate);
            qputenv("QT_IM_MODULE", candidate);
            return;
        }
    }
}

int main(int argc, char *argv[])
{
    sanitizeInputMethodEnvironment();

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NeusoftEVCP"));
    QApplication::setApplicationName(QStringLiteral("evcp-admin"));
    // 应用默认字体；控件样式集中在 QSS。
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));

    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    // 服务器地址与端口由登录页配置并持久化。
    SocketClient client;

    LoginWindow login(&client);
    login.show();

    return app.exec();
}
