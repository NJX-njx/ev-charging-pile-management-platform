#include "appconfig.h"

AppConfig AppConfig::load()
{
    QSettings s(QStringLiteral("NeusoftEVCP"), QStringLiteral("client"));
    AppConfig c;
    c.host = s.value(QStringLiteral("server/host"), c.host).toString();
    c.port = static_cast<quint16>(s.value(QStringLiteral("server/port"), c.port).toUInt());
    return c;
}

void AppConfig::save() const
{
    QSettings s(QStringLiteral("NeusoftEVCP"), QStringLiteral("client"));
    s.setValue(QStringLiteral("server/host"), host);
    s.setValue(QStringLiteral("server/port"), port);
}
