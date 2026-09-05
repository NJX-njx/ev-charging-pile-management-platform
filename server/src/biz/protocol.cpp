#include "protocol.h"

#include "timeutil.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <cmath>

namespace Protocol {

const char *const kUserSelect =
    "SELECT userId, phone, nickname, balanceFen, status, regTime, avatarMime, avatarBase64,"
    " passwordHash, deleted FROM users";

const char *const kStationAggregateSelect =
    "SELECT s.stationId, s.name, s.address, s.lng, s.lat, s.priceFenPerKwh,"
    " COUNT(p.pileId),"
    " COALESCE(SUM(p.status = 'idle'), 0),"
    " COALESCE(SUM(p.status IN ('idle', 'in_use')), 0)"
    " FROM stations s LEFT JOIN piles p ON p.stationId = s.stationId AND p.deleted = 0";

const char *const kPileSelect =
    "SELECT p.pileId, p.code, p.stationId, p.type, p.powerKw, p.status,"
    " p.chargeCount, p.chargeMinutes, s.name"
    " FROM piles p JOIN stations s ON s.stationId = p.stationId";

const char *const kOrderSelect =
    "SELECT o.orderId, o.stationId, o.pileId, o.status, o.unitPriceFen, o.reservedAt,"
    " o.startTime, o.endTime, o.settledAt, o.energyWh, o.amountFen, s.name, p.code, o.userId"
    " FROM orders o JOIN stations s ON s.stationId = o.stationId"
    " JOIN piles p ON p.pileId = o.pileId";

const char *const kAdminOrderSelect =
    "SELECT o.orderId, o.stationId, o.pileId, o.status, o.unitPriceFen, o.reservedAt,"
    " o.startTime, o.endTime, o.settledAt, o.energyWh, o.amountFen, s.name, p.code, o.userId,"
    " u.phone"
    " FROM orders o JOIN stations s ON s.stationId = o.stationId"
    " JOIN piles p ON p.pileId = o.pileId"
    " JOIN users u ON u.userId = o.userId";

Envelope parseEnvelope(const QByteArray &line)
{
    Envelope env;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return env;
    const QJsonObject obj = doc.object();
    bool valid = true;
    const QJsonValue seq = obj.value(QStringLiteral("seq"));
    const double seqNum = seq.toDouble(qQNaN());
    if (seq.isDouble() && seqNum >= 1.0 && std::fabs(seqNum - std::round(seqNum)) < 1e-9
        && seqNum <= 9007199254740992.0)
        env.seq = static_cast<qint64>(std::round(seqNum));
    else
        valid = false;
    const QJsonValue type = obj.value(QStringLiteral("type"));
    if (type.isString() && !type.toString().isEmpty())
        env.type = type.toString();
    else
        valid = false;
    const QJsonValue payload = obj.value(QStringLiteral("payload"));
    if (payload.isObject())
        env.payload = payload.toObject();
    else
        valid = false;
    env.ok = valid;
    return env;
}

QByteArray buildResponse(qint64 seq, const QString &type, const Response &response)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("seq"), static_cast<double>(seq));
    obj.insert(QStringLiteral("type"), type);
    obj.insert(QStringLiteral("code"), response.code);
    obj.insert(QStringLiteral("msg"), response.msg);
    if (response.data.isObject() || response.data.isArray())
        obj.insert(QStringLiteral("data"), response.data);
    else
        obj.insert(QStringLiteral("data"), QJsonValue::Null);
    QByteArray out = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    out.append('\n');
    return out;
}

bool readInt(const QJsonObject &obj, const QString &key, qint64 min, qint64 max, qint64 &out)
{
    const QJsonValue v = obj.value(key);
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (std::fabs(d - std::round(d)) > 1e-9)
        return false;
    const double r = std::round(d);
    if (r < static_cast<double>(min) || r > static_cast<double>(max))
        return false;
    out = static_cast<qint64>(r);
    return true;
}

bool readMoneyFen(const QJsonObject &obj, const QString &key, qint64 maxFen, qint64 &outFen)
{
    const QJsonValue v = obj.value(key);
    if (!v.isDouble())
        return false;
    const double fen = v.toDouble() * 100.0;
    if (std::fabs(fen - std::round(fen)) > 1e-6)
        return false;
    const qint64 value = static_cast<qint64>(std::round(fen));
    if (value <= 0 || value > maxFen)
        return false;
    outFen = value;
    return true;
}

bool readLngLat(const QJsonObject &obj, double &lng, double &lat)
{
    const QJsonValue lngValue = obj.value(QStringLiteral("lng"));
    const QJsonValue latValue = obj.value(QStringLiteral("lat"));
    if (!lngValue.isDouble() || !latValue.isDouble())
        return false;
    lng = lngValue.toDouble();
    lat = latValue.toDouble();
    return lng >= -180.0 && lng <= 180.0 && lat >= -90.0 && lat <= 90.0;
}

bool isValidPassword(const QString &password)
{
    if (password.length() < 6 || password.length() > 20)
        return false;
    for (const QChar c : password) {
        if (c.isSpace())
            return false;
    }
    return true;
}

QString passwordRecord(const QString &password)
{
    QByteArray salt(16, Qt::Uninitialized);
    for (int i = 0; i < salt.size(); ++i)
        salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    const QByteArray saltHex = salt.toHex();
    const QByteArray hash = QCryptographicHash::hash(saltHex + password.toUtf8(),
                                                     QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(saltHex) + QLatin1Char(':') + QString::fromLatin1(hash);
}

bool verifyPassword(const QString &record, const QString &password)
{
    const int sep = record.indexOf(QLatin1Char(':'));
    if (sep <= 0)
        return false;
    const QByteArray saltHex = record.left(sep).toLatin1();
    const QByteArray expected = record.mid(sep + 1).toLatin1();
    const QByteArray actual = QCryptographicHash::hash(saltHex + password.toUtf8(),
                                                       QCryptographicHash::Sha256).toHex();
    return actual == expected;
}

QJsonObject userJson(const QSqlQuery &q, bool withAvatar)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("userId"), q.value(0).toLongLong());
    obj.insert(QStringLiteral("phone"), q.value(1).toString());
    obj.insert(QStringLiteral("nickname"), q.value(2).toString());
    obj.insert(QStringLiteral("balance"), q.value(3).toLongLong() / 100.0);
    obj.insert(QStringLiteral("regTime"), TimeUtil::isoFromSecs(q.value(5).toLongLong()));
    obj.insert(QStringLiteral("status"), q.value(4).toString());
    obj.insert(QStringLiteral("hasPassword"), !q.value(8).isNull());
    if (withAvatar) {
        if (q.value(6).isNull() || q.value(7).isNull()) {
            obj.insert(QStringLiteral("avatar"), QJsonValue::Null);
        } else {
            QJsonObject avatar;
            avatar.insert(QStringLiteral("mime"), q.value(6).toString());
            avatar.insert(QStringLiteral("base64"), q.value(7).toString());
            obj.insert(QStringLiteral("avatar"), avatar);
        }
    }
    return obj;
}

QJsonObject stationSummaryJson(const QSqlQuery &q)
{
    const qint64 pileTotal = q.value(6).toLongLong();
    const qint64 pileIdle = q.value(7).toLongLong();
    const qint64 online = q.value(8).toLongLong();
    const double rate = pileTotal > 0
        ? std::round(static_cast<double>(online) * 100.0 / static_cast<double>(pileTotal)) / 100.0
        : 0.0;
    QJsonObject obj;
    obj.insert(QStringLiteral("stationId"), q.value(0).toLongLong());
    obj.insert(QStringLiteral("name"), q.value(1).toString());
    obj.insert(QStringLiteral("address"), q.value(2).toString());
    obj.insert(QStringLiteral("lng"), q.value(3).toDouble());
    obj.insert(QStringLiteral("lat"), q.value(4).toDouble());
    obj.insert(QStringLiteral("pricePerKwh"), q.value(5).toLongLong() / 100.0);
    obj.insert(QStringLiteral("pileTotal"), pileTotal);
    obj.insert(QStringLiteral("pileIdle"), pileIdle);
    obj.insert(QStringLiteral("onlineRate"), rate);
    return obj;
}

QJsonObject pileJson(const QSqlQuery &q)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("pileId"), q.value(0).toLongLong());
    obj.insert(QStringLiteral("code"), q.value(1).toString());
    obj.insert(QStringLiteral("stationId"), q.value(2).toLongLong());
    obj.insert(QStringLiteral("stationName"), q.value(8).toString());
    obj.insert(QStringLiteral("type"), q.value(3).toString());
    obj.insert(QStringLiteral("powerKw"), q.value(4).toDouble());
    obj.insert(QStringLiteral("status"), q.value(5).toString());
    obj.insert(QStringLiteral("chargeCount"), q.value(6).toLongLong());
    obj.insert(QStringLiteral("chargeMinutes"), q.value(7).toLongLong());
    return obj;
}

QJsonObject orderJson(const QSqlQuery &q)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("orderId"), q.value(0).toLongLong());
    obj.insert(QStringLiteral("stationId"), q.value(1).toLongLong());
    obj.insert(QStringLiteral("stationName"), q.value(11).toString());
    obj.insert(QStringLiteral("pileId"), q.value(2).toLongLong());
    obj.insert(QStringLiteral("pileCode"), q.value(12).toString());
    obj.insert(QStringLiteral("status"), q.value(3).toString());
    obj.insert(QStringLiteral("reservedAt"), TimeUtil::isoFromSecs(q.value(5).toLongLong()));
    auto timeOrNull = [&q](int index) -> QJsonValue {
        if (q.value(index).isNull())
            return QJsonValue(QJsonValue::Null);
        return QJsonValue(TimeUtil::isoFromSecs(q.value(index).toLongLong()));
    };
    obj.insert(QStringLiteral("startTime"), timeOrNull(6));
    obj.insert(QStringLiteral("endTime"), timeOrNull(7));
    obj.insert(QStringLiteral("settledAt"), timeOrNull(8));
    if (q.value(9).isNull())
        obj.insert(QStringLiteral("energyKwh"), QJsonValue::Null);
    else
        obj.insert(QStringLiteral("energyKwh"), q.value(9).toLongLong() / 1000.0);
    obj.insert(QStringLiteral("unitPrice"), q.value(4).toLongLong() / 100.0);
    if (q.value(10).isNull())
        obj.insert(QStringLiteral("amount"), QJsonValue::Null);
    else
        obj.insert(QStringLiteral("amount"), q.value(10).toLongLong() / 100.0);
    return obj;
}

QJsonObject adminOrderJson(const QSqlQuery &q)
{
    QJsonObject obj = orderJson(q);
    obj.insert(QStringLiteral("userPhone"), q.value(14).toString());
    return obj;
}

} // namespace Protocol
