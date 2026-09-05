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
// 没有则清除该环境变量，让 Qt 按默认顺序（ibus/compose）选择可用输入法。
static void sanitizeInputMethodEnvironment()
{
    const QString module = QString::fromLocal8Bit(qgetenv("QT_IM_MODULE")).trimmed();
    if (module.isEmpty())
        return;

    const QDir pluginDir(QLibraryInfo::path(QLibraryInfo::PluginsPath)
                         + QStringLiteral("/platforminputcontexts"));
    const QStringList plugins = pluginDir.entryList(QDir::Files);
    for (const QString &file : plugins) {
        if (file.contains(module, Qt::CaseInsensitive))
            return;
    }
    qWarning("QT_IM_MODULE=%s 在当前 Qt %s 中没有对应输入法插件，已忽略该设置；"
             "如需中文输入法请安装对应 Qt 版本的输入法前端（如 fcitx5-frontend-qt6）",
             qPrintable(module), qVersion());
    qunsetenv("QT_IM_MODULE");
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
