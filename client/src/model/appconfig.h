#pragma once

#include <QSettings>
#include <QString>

struct AppConfig {
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 8888;

    static AppConfig load();
    void save() const;
};
