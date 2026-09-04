#include "models.h"

#include <QDateTime>

UserInfo UserInfo::fromJson(const QJsonObject &o)
{
    UserInfo u;
    u.userId = static_cast<qint64>(o.value(QStringLiteral("userId")).toDouble());
    u.phone = o.value(QStringLiteral("phone")).toString();
    u.nickname = o.value(QStringLiteral("nickname")).toString();
    u.balance = o.value(QStringLiteral("balance")).toDouble();
    u.regTime = o.value(QStringLiteral("regTime")).toString();
    u.status = o.value(QStringLiteral("status")).toString();
    const QJsonValue avatar = o.value(QStringLiteral("avatar"));
    if (avatar.isObject()) {
        const QJsonObject a = avatar.toObject();
        u.avatarMime = a.value(QStringLiteral("mime")).toString();
        const QString b64 = a.value(QStringLiteral("base64")).toString();
        u.avatarBytes = QByteArray::fromBase64(b64.toUtf8());
        u.hasAvatar = !u.avatarBytes.isEmpty();
    }
    return u;
}

Station Station::fromJson(const QJsonObject &o)
{
    Station s;
    s.stationId = static_cast<qint64>(o.value(QStringLiteral("stationId")).toDouble());
    s.name = o.value(QStringLiteral("name")).toString();
    s.address = o.value(QStringLiteral("address")).toString();
    s.lng = o.value(QStringLiteral("lng")).toDouble();
    s.lat = o.value(QStringLiteral("lat")).toDouble();
    s.pricePerKwh = o.value(QStringLiteral("pricePerKwh")).toDouble();
    s.pileTotal = o.value(QStringLiteral("pileTotal")).toInt();
    s.pileIdle = o.value(QStringLiteral("pileIdle")).toInt();
    s.onlineRate = o.value(QStringLiteral("onlineRate")).toDouble();
    if (o.contains(QStringLiteral("distanceKm")) && o.value(QStringLiteral("distanceKm")).isDouble())
        s.distanceKm = o.value(QStringLiteral("distanceKm")).toDouble();
    return s;
}

Pile Pile::fromJson(const QJsonObject &o)
{
    Pile p;
    p.pileId = static_cast<qint64>(o.value(QStringLiteral("pileId")).toDouble());
    p.code = o.value(QStringLiteral("code")).toString();
    p.stationId = static_cast<qint64>(o.value(QStringLiteral("stationId")).toDouble());
    p.stationName = o.value(QStringLiteral("stationName")).toString();
    p.type = o.value(QStringLiteral("type")).toString();
    p.powerKw = o.value(QStringLiteral("powerKw")).toDouble();
    p.status = o.value(QStringLiteral("status")).toString();
    p.chargeCount = o.value(QStringLiteral("chargeCount")).toInt();
    p.chargeMinutes = o.value(QStringLiteral("chargeMinutes")).toInt();
    return p;
}

Order Order::fromJson(const QJsonObject &o)
{
    Order r;
    r.orderId = static_cast<qint64>(o.value(QStringLiteral("orderId")).toDouble());
    r.stationId = static_cast<qint64>(o.value(QStringLiteral("stationId")).toDouble());
    r.stationName = o.value(QStringLiteral("stationName")).toString();
    r.pileId = static_cast<qint64>(o.value(QStringLiteral("pileId")).toDouble());
    r.pileCode = o.value(QStringLiteral("pileCode")).toString();
    r.status = o.value(QStringLiteral("status")).toString();
    r.reservedAt = o.value(QStringLiteral("reservedAt")).toString();
    r.startTime = o.value(QStringLiteral("startTime")).toString();
    r.endTime = o.value(QStringLiteral("endTime")).toString();
    r.settledAt = o.value(QStringLiteral("settledAt")).toString();
    const QJsonValue energy = o.value(QStringLiteral("energyKwh"));
    if (energy.isDouble()) {
        r.hasEnergy = true;
        r.energyKwh = energy.toDouble();
    }
    r.unitPrice = o.value(QStringLiteral("unitPrice")).toDouble();
    const QJsonValue amount = o.value(QStringLiteral("amount"));
    if (amount.isDouble()) {
        r.hasAmount = true;
        r.amount = amount.toDouble();
    }
    return r;
}

QDateTime Order::startDateTime() const
{
    return QDateTime::fromString(startTime, Qt::ISODate);
}

QString formatDateTime(const QString &iso)
{
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid())
        return QString();
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}
