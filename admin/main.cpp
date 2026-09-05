#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>

#include "net/socketclient.h"
#include "ui/loginwindow.h"

// 课程虚拟机（Ubuntu 22.04）的桌面会话在 ~/.profile 中导出 QT_IM_MODULE=fcitx，
// 但仓库目标 Qt 6.2 只随附 ibus/compose 输入法插件，fcitx5-frontend-qt5 提供的
// fcitx 插件仅对 Qt5 可用；QT_IM_MODULE 指向不存在的插件时 Qt 不再加载任何输入法，
// 表现为所有 QLineEdit 都无法输入中文（启动期一次性生效，与具体输入框无关）。
// 启动时校验 QT_IM_MODULE 是否有对应当前 Qt 的 platforminputcontexts 插件，
// 没有则按 fcitx→ibus→compose 顺序回退到第一个可用插件（fcitx5 自带 IBus Frontend，
// 经 Qt6 的 ibus 插件即可正常输入中文），与 client/src/main.cpp 同款逻辑。
static void sanitizeInputMethodEnvironment()
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
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC")));

    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    // 不再启动即连接固定地址，服务器地址/端口由登录页输入并经 QSettings 持久化
    SocketClient client;

    LoginWindow login(&client);
    login.show();

    return app.exec();
}
