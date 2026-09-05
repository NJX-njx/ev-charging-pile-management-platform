#include "handlers.h"

#include "stats.h"
#include "timeutil.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <cmath>

namespace {

constexpr qint64 kMaxId = (1LL << 52);
constexpr qint64 kMaxRechargeFen = 10000 * 100;
constexpr qint64 kMaxPriceFen = 1000000 * 100;
constexpr int kMaxAvatarBytes = 512 * 1024;

Response fail(int code, const QString &msg)
{
    return Response{code, msg, QJsonValue(QJsonValue::Null)};
}

Response ok(const QJsonValue &data)
{
    return Response{0, QStringLiteral("ok"), data};
}

bool exec(QSqlQuery &q)
{
    if (q.exec())
        return true;
    qWarning() << "SQL error:" << q.lastError().text();
    return false;
}

// Column 13 of kOrderSelect is o.userId, used for ownership checks only.
Response loadOrder(QSqlDatabase db, qint64 orderId, QSqlQuery &q)
{
    q = QSqlQuery(db);
    q.prepare(QString::fromLatin1(Protocol::kOrderSelect)
              + QStringLiteral(" WHERE o.orderId = ?"));
    q.addBindValue(orderId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("order not found"));
    return ok(QJsonValue());
}

Response orderDataResponse(const QSqlQuery &q)
{
    QJsonObject data;
    data.insert(QStringLiteral("order"), Protocol::orderJson(q));
    return ok(data);
}

Response hPing(const QJsonObject &, Session &, QSqlDatabase )
{
    QJsonObject data;
    data.insert(QStringLiteral("serverTime"), TimeUtil::isoFromSecs(TimeUtil::nowSecs()));
    return ok(data);
}

Response finishUserLogin(QSqlQuery &q, Session &s, bool isNew)
{
    if (q.value(4).toString() == QLatin1String("frozen"))
        return fail(1002, QStringLiteral("account frozen"));
    s.role = Session::User;
    s.userId = q.value(0).toLongLong();
    QJsonObject data;
    data.insert(QStringLiteral("isNew"), isNew);
    data.insert(QStringLiteral("user"), Protocol::userJson(q, true));
    return ok(data);
}

Response hUserLogin(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    const QString phone = p.value(QStringLiteral("phone")).toString();
    static const QRegularExpression phoneRe(QStringLiteral("^1[0-9]{10}$"));
    if (!phoneRe.match(phone).hasMatch())
        return fail(2001, QStringLiteral("invalid phone"));

    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kUserSelect)
              + QStringLiteral(" WHERE phone = ?"));
    q.addBindValue(phone);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next()) {
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral("INSERT INTO users (phone, nickname, balanceFen, status, regTime)"
                                   " VALUES (?, ?, 0, 'normal', ?)"));
        ins.addBindValue(phone);
        ins.addBindValue(QStringLiteral("用户") + phone.right(4));
        ins.addBindValue(TimeUtil::nowSecs());
        if (!exec(ins))
            return fail(5000, QStringLiteral("internal error"));
        QSqlQuery q2(db);
        q2.prepare(QString::fromLatin1(Protocol::kUserSelect)
                   + QStringLiteral(" WHERE phone = ?"));
        q2.addBindValue(phone);
        if (!exec(q2) || !q2.next())
            return fail(5000, QStringLiteral("internal error"));
        return finishUserLogin(q2, s, true);
    }
    return finishUserLogin(q, s, false);
}

Response hAdminLogin(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    if (!p.value(QStringLiteral("username")).isString()
        || !p.value(QStringLiteral("password")).isString())
        return fail(2001, QStringLiteral("invalid params"));
    const QString username = p.value(QStringLiteral("username")).toString();
    const QString password = p.value(QStringLiteral("password")).toString();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT adminId, passwordHash FROM admins WHERE username = ?"));
    q.addBindValue(username);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(1001, QStringLiteral("invalid credentials"));
    const QString stored = q.value(1).toString();
    const int sep = stored.indexOf(QLatin1Char(':'));
    const QByteArray saltHex = sep > 0 ? stored.left(sep).toLatin1() : QByteArray();
    const QByteArray expected = sep > 0 ? stored.mid(sep + 1).toLatin1() : QByteArray();
    const QByteArray actual = QCryptographicHash::hash(saltHex + password.toUtf8(),
                                                       QCryptographicHash::Sha256).toHex();
    if (actual != expected)
        return fail(1001, QStringLiteral("invalid credentials"));
    s.role = Session::Admin;
    s.adminId = q.value(0).toLongLong();
    QJsonObject data;
    data.insert(QStringLiteral("adminId"), s.adminId);
    data.insert(QStringLiteral("username"), username);
    return ok(data);
}

Response hProfileGet(const QJsonObject &, Session &s, QSqlDatabase db)
{
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kUserSelect)
              + QStringLiteral(" WHERE userId = ?"));
    q.addBindValue(s.userId);
    if (!exec(q) || !q.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("user"), Protocol::userJson(q, true));
    return ok(data);
}

Response hProfileUpdate(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    const bool hasNick = p.contains(QStringLiteral("nickname"));
    const bool hasAvatar = p.contains(QStringLiteral("avatar"));
    if (!hasNick && !hasAvatar)
        return fail(2001, QStringLiteral("nothing to update"));
    QString nickname;
    QString avatarMime;
    QString avatarB64;
    if (hasNick) {
        if (!p.value(QStringLiteral("nickname")).isString())
            return fail(2001, QStringLiteral("invalid nickname"));
        nickname = p.value(QStringLiteral("nickname")).toString().trimmed();
        if (nickname.isEmpty() || nickname.length() > 20)
            return fail(2001, QStringLiteral("invalid nickname"));
    }
    if (hasAvatar) {
        if (!p.value(QStringLiteral("avatar")).isObject())
            return fail(2001, QStringLiteral("invalid avatar"));
        const QJsonObject avatar = p.value(QStringLiteral("avatar")).toObject();
        avatarMime = avatar.value(QStringLiteral("mime")).toString();
        if (avatarMime != QLatin1String("image/jpeg") && avatarMime != QLatin1String("image/png"))
            return fail(2001, QStringLiteral("invalid avatar"));
        if (!avatar.value(QStringLiteral("base64")).isString())
            return fail(2001, QStringLiteral("invalid avatar"));
        avatarB64 = avatar.value(QStringLiteral("base64")).toString();
        const QByteArray raw = QByteArray::fromBase64(avatarB64.toUtf8(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        if (raw.isNull() && !avatarB64.isEmpty())
            return fail(2001, QStringLiteral("invalid avatar"));
        if (raw.size() > kMaxAvatarBytes)
            return fail(4001, QStringLiteral("avatar too large"));
    }
    QSqlQuery q(db);
    if (hasNick && hasAvatar) {
        q.prepare(QStringLiteral("UPDATE users SET nickname = ?, avatarMime = ?, avatarBase64 = ?"
                                 " WHERE userId = ?"));
        q.addBindValue(nickname);
        q.addBindValue(avatarMime);
        q.addBindValue(avatarB64);
    } else if (hasNick) {
        q.prepare(QStringLiteral("UPDATE users SET nickname = ? WHERE userId = ?"));
        q.addBindValue(nickname);
    } else {
        q.prepare(QStringLiteral("UPDATE users SET avatarMime = ?, avatarBase64 = ?"
                                 " WHERE userId = ?"));
        q.addBindValue(avatarMime);
        q.addBindValue(avatarB64);
    }
    q.addBindValue(s.userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    return hProfileGet(p, s, db);
}

Response hRecharge(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 fen = 0;
    if (!Protocol::readMoneyFen(p, QStringLiteral("amount"), kMaxRechargeFen, fen))
        return fail(2001, QStringLiteral("invalid amount"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE users SET balanceFen = balanceFen + ? WHERE userId = ?"));
    q.addBindValue(fen);
    q.addBindValue(s.userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("SELECT balanceFen FROM users WHERE userId = ?"));
    q2.addBindValue(s.userId);
    if (!exec(q2) || !q2.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("amount"), fen / 100.0);
    data.insert(QStringLiteral("balance"), q2.value(0).toLongLong() / 100.0);
    return ok(data);
}

double haversineKm(double lng1, double lat1, double lng2, double lat2)
{
    constexpr double kEarthRadiusKm = 6371.0;
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double dLat = (lat2 - lat1) * kDegToRad;
    const double dLng = (lng2 - lng1) * kDegToRad;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
        + std::cos(lat1 * kDegToRad) * std::cos(lat2 * kDegToRad)
            * std::sin(dLng / 2) * std::sin(dLng / 2);
    return 2.0 * kEarthRadiusKm * std::asin(std::sqrt(a));
}

Response hNearby(const QJsonObject &p, Session &, QSqlDatabase db)
{
    double lng = 0.0, lat = 0.0;
    if (!Protocol::readLngLat(p, lng, lat))
        return fail(2001, QStringLiteral("invalid coordinates"));
    qint64 limit = 50;
    if (p.contains(QStringLiteral("limit"))
        && !Protocol::readInt(p, QStringLiteral("limit"), 1, 100, limit))
        return fail(2001, QStringLiteral("invalid limit"));

    struct Item {
        QJsonObject station;
        double distanceKm;
    };
    QList<Item> items;
    const QJsonArray all = Stats::stationSummaries(db);
    for (const QJsonValue &v : all) {
        QJsonObject station = v.toObject();
        const double raw = haversineKm(lng, lat,
                                       station.value(QStringLiteral("lng")).toDouble(),
                                       station.value(QStringLiteral("lat")).toDouble());
        const double distanceKm = std::round(raw * 10.0) / 10.0;
        station.insert(QStringLiteral("distanceKm"), distanceKm);
        items.append({station, distanceKm});
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.distanceKm != b.distanceKm)
            return a.distanceKm < b.distanceKm;
        return a.station.value(QStringLiteral("stationId")).toDouble()
            < b.station.value(QStringLiteral("stationId")).toDouble();
    });
    QJsonArray stations;
    for (qint64 i = 0; i < limit && i < items.size(); ++i)
        stations.append(items[static_cast<int>(i)].station);
    QJsonObject data;
    data.insert(QStringLiteral("stations"), stations);
    return ok(data);
}

Response hStationDetail(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 stationId = 0;
    if (!Protocol::readInt(p, QStringLiteral("stationId"), 1, kMaxId, stationId))
        return fail(2001, QStringLiteral("invalid stationId"));
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kStationAggregateSelect)
              + QStringLiteral(" WHERE s.stationId = ? GROUP BY s.stationId"));
    q.addBindValue(stationId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("station not found"));
    const QJsonObject station = Protocol::stationSummaryJson(q);
    QSqlQuery piles(db);
    piles.prepare(QString::fromLatin1(Protocol::kPileSelect)
                  + QStringLiteral(" WHERE p.stationId = ? ORDER BY p.code"));
    piles.addBindValue(stationId);
    if (!exec(piles))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray pileArray;
    while (piles.next())
        pileArray.append(Protocol::pileJson(piles));
    QJsonObject data;
    data.insert(QStringLiteral("station"), station);
    data.insert(QStringLiteral("piles"), pileArray);
    return ok(data);
}

Response hActiveOrder(const QJsonObject &, Session &s, QSqlDatabase db)
{
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kOrderSelect)
              + QStringLiteral(" WHERE o.userId = ?"
                               " AND o.status IN ('reserved', 'charging', 'pending_payment')"
                               " ORDER BY o.orderId DESC LIMIT 1"));
    q.addBindValue(s.userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    if (q.next())
        data.insert(QStringLiteral("order"), Protocol::orderJson(q));
    else
        data.insert(QStringLiteral("order"), QJsonValue::Null);
    return ok(data);
}

Response hReserve(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 pileId = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileId"), 1, kMaxId, pileId))
        return fail(2001, QStringLiteral("invalid pileId"));
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT orderId FROM orders WHERE userId = ?"
                             " AND status IN ('reserved', 'charging', 'pending_payment') LIMIT 1"));
    q.addBindValue(s.userId);
    if (!exec(q)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (q.next()) {
        db.rollback();
        return fail(3005, QStringLiteral("unfinished order exists"));
    }
    QSqlQuery pq(db);
    pq.prepare(QStringLiteral("SELECT p.status, p.stationId, s.priceFenPerKwh"
                              " FROM piles p JOIN stations s ON s.stationId = p.stationId"
                              " WHERE p.pileId = ?"));
    pq.addBindValue(pileId);
    if (!exec(pq)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!pq.next()) {
        db.rollback();
        return fail(2002, QStringLiteral("pile not found"));
    }
    if (pq.value(0).toString() != QLatin1String("idle")) {
        db.rollback();
        return fail(3003, QStringLiteral("pile unavailable"));
    }
    const qint64 stationId = pq.value(1).toLongLong();
    const qint64 priceFen = pq.value(2).toLongLong();
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO orders (userId, stationId, pileId, status,"
                               " unitPriceFen, reservedAt) VALUES (?, ?, ?, 'reserved', ?, ?)"));
    ins.addBindValue(s.userId);
    ins.addBindValue(stationId);
    ins.addBindValue(pileId);
    ins.addBindValue(priceFen);
    ins.addBindValue(TimeUtil::nowSecs());
    if (!exec(ins)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE piles SET status = 'in_use' WHERE pileId = ?"));
    upd.addBindValue(pileId);
    if (!exec(upd)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery order;
    const Response loaded = loadOrder(db, ins.lastInsertId().toLongLong(), order);
    if (loaded.code != 0)
        return loaded;
    return orderDataResponse(order);
}

Response hStart(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 orderId = 0;
    if (!Protocol::readInt(p, QStringLiteral("orderId"), 1, kMaxId, orderId))
        return fail(2001, QStringLiteral("invalid orderId"));
    QSqlQuery q(db);
    const Response loaded = loadOrder(db, orderId, q);
    if (loaded.code != 0)
        return loaded;
    if (q.value(13).toLongLong() != s.userId)
        return fail(2002, QStringLiteral("order not found"));
    if (q.value(3).toString() != QLatin1String("reserved"))
        return fail(3002, QStringLiteral("order status must be reserved"));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE orders SET status = 'charging', startTime = ?"
                               " WHERE orderId = ?"));
    upd.addBindValue(TimeUtil::nowSecs());
    upd.addBindValue(orderId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery fresh;
    const Response reloaded = loadOrder(db, orderId, fresh);
    if (reloaded.code != 0)
        return reloaded;
    return orderDataResponse(fresh);
}

Response hStop(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 orderId = 0;
    if (!Protocol::readInt(p, QStringLiteral("orderId"), 1, kMaxId, orderId))
        return fail(2001, QStringLiteral("invalid orderId"));
    QSqlQuery q(db);
    const Response loaded = loadOrder(db, orderId, q);
    if (loaded.code != 0)
        return loaded;
    if (q.value(13).toLongLong() != s.userId)
        return fail(2002, QStringLiteral("order not found"));
    if (q.value(3).toString() != QLatin1String("charging"))
        return fail(3002, QStringLiteral("order status must be charging"));
    const qint64 startTime = q.value(6).toLongLong();
    const qint64 unitPriceFen = q.value(4).toLongLong();
    const qint64 pileId = q.value(2).toLongLong();
    QSqlQuery pq(db);
    pq.prepare(QStringLiteral("SELECT powerKw FROM piles WHERE pileId = ?"));
    pq.addBindValue(pileId);
    if (!exec(pq) || !pq.next())
        return fail(5000, QStringLiteral("internal error"));
    const double powerKw = pq.value(0).toDouble();
    const qint64 now = TimeUtil::nowSecs();
    const qint64 durationSecs = qMax<qint64>(0, now - startTime);
    const qint64 energyWh = static_cast<qint64>(
        std::llround(powerKw * static_cast<double>(durationSecs) * 1000.0 / 3600.0));
    const qint64 amountFen = (energyWh * unitPriceFen + 500) / 1000;
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE orders SET status = 'pending_payment', endTime = ?,"
                               " energyWh = ?, amountFen = ? WHERE orderId = ?"));
    upd.addBindValue(now);
    upd.addBindValue(energyWh);
    upd.addBindValue(amountFen);
    upd.addBindValue(orderId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery fresh;
    const Response reloaded = loadOrder(db, orderId, fresh);
    if (reloaded.code != 0)
        return reloaded;
    return orderDataResponse(fresh);
}

Response hSettle(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 orderId = 0;
    if (!Protocol::readInt(p, QStringLiteral("orderId"), 1, kMaxId, orderId))
        return fail(2001, QStringLiteral("invalid orderId"));
    QSqlQuery q(db);
    const Response loaded = loadOrder(db, orderId, q);
    if (loaded.code != 0)
        return loaded;
    if (q.value(13).toLongLong() != s.userId)
        return fail(2002, QStringLiteral("order not found"));
    if (q.value(3).toString() != QLatin1String("pending_payment"))
        return fail(3002, QStringLiteral("order status must be pending_payment"));
    const qint64 amountFen = q.value(10).toLongLong();
    const qint64 pileId = q.value(2).toLongLong();
    const qint64 startTime = q.value(6).toLongLong();
    const qint64 endTime = q.value(7).toLongLong();
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery bal(db);
    bal.prepare(QStringLiteral("SELECT balanceFen FROM users WHERE userId = ?"));
    bal.addBindValue(s.userId);
    if (!exec(bal) || !bal.next()) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (bal.value(0).toLongLong() < amountFen) {
        db.rollback();
        return fail(3004, QStringLiteral("insufficient balance"));
    }
    QSqlQuery deduct(db);
    deduct.prepare(QStringLiteral("UPDATE users SET balanceFen = balanceFen - ? WHERE userId = ?"));
    deduct.addBindValue(amountFen);
    deduct.addBindValue(s.userId);
    QSqlQuery done(db);
    done.prepare(QStringLiteral("UPDATE orders SET status = 'completed', settledAt = ?"
                                " WHERE orderId = ?"));
    done.addBindValue(TimeUtil::nowSecs());
    done.addBindValue(orderId);
    const qint64 minutes = static_cast<qint64>(
        std::llround(static_cast<double>(endTime - startTime) / 60.0));
    QSqlQuery release(db);
    release.prepare(QStringLiteral("UPDATE piles SET status = 'idle',"
                                   " chargeCount = chargeCount + 1,"
                                   " chargeMinutes = chargeMinutes + ? WHERE pileId = ?"));
    release.addBindValue(minutes);
    release.addBindValue(pileId);
    if (!exec(deduct) || !exec(done) || !exec(release)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery fresh;
    const Response reloaded = loadOrder(db, orderId, fresh);
    if (reloaded.code != 0)
        return reloaded;
    QSqlQuery bal2(db);
    bal2.prepare(QStringLiteral("SELECT balanceFen FROM users WHERE userId = ?"));
    bal2.addBindValue(s.userId);
    if (!exec(bal2) || !bal2.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("order"), Protocol::orderJson(fresh));
    data.insert(QStringLiteral("balance"), bal2.value(0).toLongLong() / 100.0);
    return ok(data);
}

Response hCancel(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 orderId = 0;
    if (!Protocol::readInt(p, QStringLiteral("orderId"), 1, kMaxId, orderId))
        return fail(2001, QStringLiteral("invalid orderId"));
    QSqlQuery q(db);
    const Response loaded = loadOrder(db, orderId, q);
    if (loaded.code != 0)
        return loaded;
    if (q.value(13).toLongLong() != s.userId)
        return fail(2002, QStringLiteral("order not found"));
    if (q.value(3).toString() != QLatin1String("reserved"))
        return fail(3002, QStringLiteral("order status must be reserved"));
    const qint64 pileId = q.value(2).toLongLong();
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery cancel(db);
    cancel.prepare(QStringLiteral("UPDATE orders SET status = 'cancelled' WHERE orderId = ?"));
    cancel.addBindValue(orderId);
    QSqlQuery release(db);
    release.prepare(QStringLiteral("UPDATE piles SET status = 'idle' WHERE pileId = ?"));
    release.addBindValue(pileId);
    if (!exec(cancel) || !exec(release)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("orderId"), orderId);
    data.insert(QStringLiteral("status"), QStringLiteral("cancelled"));
    data.insert(QStringLiteral("pileId"), pileId);
    data.insert(QStringLiteral("pileStatus"), QStringLiteral("idle"));
    return ok(data);
}

Response hOrderList(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 page = 1, pageSize = 20;
    if (p.contains(QStringLiteral("page"))
        && !Protocol::readInt(p, QStringLiteral("page"), 1, kMaxId, page))
        return fail(2001, QStringLiteral("invalid page"));
    if (p.contains(QStringLiteral("pageSize"))
        && !Protocol::readInt(p, QStringLiteral("pageSize"), 1, 100, pageSize))
        return fail(2001, QStringLiteral("invalid pageSize"));
    QSqlQuery count(db);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE userId = ?"));
    count.addBindValue(s.userId);
    if (!exec(count) || !count.next())
        return fail(5000, QStringLiteral("internal error"));
    const qint64 total = count.value(0).toLongLong();
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kOrderSelect)
              + QStringLiteral(" WHERE o.userId = ?"
                               " ORDER BY o.reservedAt DESC, o.orderId DESC LIMIT ? OFFSET ?"));
    q.addBindValue(s.userId);
    q.addBindValue(pageSize);
    q.addBindValue((page - 1) * pageSize);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray orders;
    while (q.next())
        orders.append(Protocol::orderJson(q));
    QJsonObject data;
    data.insert(QStringLiteral("page"), page);
    data.insert(QStringLiteral("pageSize"), pageSize);
    data.insert(QStringLiteral("total"), total);
    data.insert(QStringLiteral("orders"), orders);
    return ok(data);
}

Response hRevenueSummary(const QJsonObject &, Session &, QSqlDatabase db)
{
    return ok(Stats::revenueSummary(db));
}

Response hRevenueTrend(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 range = 0;
    if (!Protocol::readInt(p, QStringLiteral("range"), 1, kMaxId, range)
        || (range != 7 && range != 30))
        return fail(2001, QStringLiteral("range must be 7 or 30"));
    return ok(Stats::revenueTrend(db, static_cast<int>(range)));
}

Response hPileStatusOverview(const QJsonObject &, Session &, QSqlDatabase db)
{
    return ok(Stats::pileStatusOverview(db));
}

Response hPileList(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 stationId = 0;
    if (p.contains(QStringLiteral("stationId"))
        && !p.value(QStringLiteral("stationId")).isNull()
        && !Protocol::readInt(p, QStringLiteral("stationId"), 0, kMaxId, stationId))
        return fail(2001, QStringLiteral("invalid stationId"));
    QString status;
    if (p.contains(QStringLiteral("status"))
        && !p.value(QStringLiteral("status")).isNull()) {
        if (!p.value(QStringLiteral("status")).isString())
            return fail(2001, QStringLiteral("invalid status"));
        status = p.value(QStringLiteral("status")).toString();
        if (status != QLatin1String("idle") && status != QLatin1String("in_use")
            && status != QLatin1String("fault"))
            return fail(2001, QStringLiteral("invalid status"));
    }
    QString sql = QString::fromLatin1(Protocol::kPileSelect)
        + QStringLiteral(" WHERE 1 = 1");
    if (stationId > 0)
        sql += QStringLiteral(" AND p.stationId = ?");
    if (!status.isEmpty())
        sql += QStringLiteral(" AND p.status = ?");
    sql += QStringLiteral(" ORDER BY p.stationId, p.code");
    QSqlQuery q(db);
    q.prepare(sql);
    if (stationId > 0)
        q.addBindValue(stationId);
    if (!status.isEmpty())
        q.addBindValue(status);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray piles;
    while (q.next())
        piles.append(Protocol::pileJson(q));
    QJsonObject data;
    data.insert(QStringLiteral("piles"), piles);
    return ok(data);
}

Response hPileRestart(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 pileId = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileId"), 1, kMaxId, pileId))
        return fail(2001, QStringLiteral("invalid pileId"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT status FROM piles WHERE pileId = ?"));
    q.addBindValue(pileId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("pile not found"));
    if (q.value(0).toString() != QLatin1String("fault"))
        return fail(3002, QStringLiteral("pile is not faulty"));
    QSqlQuery active(db);
    active.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE pileId = ?"
                                  " AND status IN ('reserved', 'charging', 'pending_payment')"));
    active.addBindValue(pileId);
    if (!exec(active) || !active.next())
        return fail(5000, QStringLiteral("internal error"));
    if (active.value(0).toLongLong() > 0)
        return fail(3002, QStringLiteral("pile has unfinished order"));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE piles SET status = 'idle' WHERE pileId = ?"));
    upd.addBindValue(pileId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("pileId"), pileId);
    data.insert(QStringLiteral("status"), QStringLiteral("idle"));
    return ok(data);
}

Response hStationList(const QJsonObject &, Session &, QSqlDatabase db)
{
    QJsonObject data;
    data.insert(QStringLiteral("stations"), Stats::stationSummaries(db));
    return ok(data);
}

Response hStationAdd(const QJsonObject &p, Session &, QSqlDatabase db)
{
    if (!p.value(QStringLiteral("name")).isString()
        || !p.value(QStringLiteral("address")).isString())
        return fail(2001, QStringLiteral("invalid params"));
    const QString name = p.value(QStringLiteral("name")).toString().trimmed();
    const QString address = p.value(QStringLiteral("address")).toString().trimmed();
    if (name.isEmpty() || address.isEmpty())
        return fail(2001, QStringLiteral("invalid params"));
    double lng = 0.0, lat = 0.0;
    if (!Protocol::readLngLat(p, lng, lat))
        return fail(2001, QStringLiteral("invalid coordinates"));
    qint64 priceFen = 0;
    if (!Protocol::readMoneyFen(p, QStringLiteral("pricePerKwh"), kMaxPriceFen, priceFen))
        return fail(2001, QStringLiteral("invalid pricePerKwh"));
    qint64 pileCount = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileCount"), 1, 100, pileCount))
        return fail(2001, QStringLiteral("invalid pileCount"));

    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO stations (name, address, lng, lat, priceFenPerKwh)"
                               " VALUES (?, ?, ?, ?, ?)"));
    ins.addBindValue(name);
    ins.addBindValue(address);
    ins.addBindValue(lng);
    ins.addBindValue(lat);
    ins.addBindValue(priceFen);
    if (!exec(ins)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    const qint64 stationId = ins.lastInsertId().toLongLong();
    QSqlQuery counter(db);
    if (!counter.exec(QStringLiteral("INSERT OR IGNORE INTO counters (key, value)"
                                     " VALUES ('pileSeq', 0)"))
        || !counter.exec(QStringLiteral("SELECT value FROM counters WHERE key = 'pileSeq'"))
        || !counter.next()) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    const qint64 base = counter.value(0).toLongLong();
    QSqlQuery counterUpd(db);
    counterUpd.prepare(QStringLiteral("UPDATE counters SET value = ? WHERE key = 'pileSeq'"));
    counterUpd.addBindValue(base + pileCount);
    if (!exec(counterUpd)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    for (qint64 i = 1; i <= pileCount; ++i) {
        const qint64 n = base + i;
        const QString code = QStringLiteral("P-%1").arg(n, 4, 10, QLatin1Char('0'));
        const bool slow = (n % 2 == 1);
        QSqlQuery pileIns(db);
        pileIns.prepare(QStringLiteral("INSERT INTO piles (code, stationId, type, powerKw, status)"
                                       " VALUES (?, ?, ?, ?, 'idle')"));
        pileIns.addBindValue(code);
        pileIns.addBindValue(stationId);
        pileIns.addBindValue(slow ? QStringLiteral("slow") : QStringLiteral("fast"));
        pileIns.addBindValue(slow ? 7.0 : 60.0);
        if (!exec(pileIns)) {
            db.rollback();
            return fail(5000, QStringLiteral("internal error"));
        }
    }
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));

    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kStationAggregateSelect)
              + QStringLiteral(" WHERE s.stationId = ? GROUP BY s.stationId"));
    q.addBindValue(stationId);
    if (!exec(q) || !q.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("station"), Protocol::stationSummaryJson(q));
    data.insert(QStringLiteral("createdPileCount"), pileCount);
    return ok(data);
}

Response hUserList(const QJsonObject &p, Session &, QSqlDatabase db)
{
    QString keyword;
    if (p.contains(QStringLiteral("phoneKeyword"))) {
        if (!p.value(QStringLiteral("phoneKeyword")).isString())
            return fail(2001, QStringLiteral("invalid phoneKeyword"));
        keyword = p.value(QStringLiteral("phoneKeyword")).toString();
        static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
        if (!keyword.isEmpty() && !digits.match(keyword).hasMatch())
            return fail(2001, QStringLiteral("invalid phoneKeyword"));
    }
    QString sql = QString::fromLatin1(Protocol::kUserSelect);
    if (!keyword.isEmpty())
        sql += QStringLiteral(" WHERE phone LIKE ?");
    sql += QStringLiteral(" ORDER BY userId");
    QSqlQuery q(db);
    q.prepare(sql);
    if (!keyword.isEmpty())
        q.addBindValue(QLatin1Char('%') + keyword + QLatin1Char('%'));
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray users;
    while (q.next())
        users.append(Protocol::userJson(q, false));
    QJsonObject data;
    data.insert(QStringLiteral("users"), users);
    return ok(data);
}

Response hUserSetStatus(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 userId = 0;
    if (!Protocol::readInt(p, QStringLiteral("userId"), 1, kMaxId, userId))
        return fail(2001, QStringLiteral("invalid userId"));
    if (!p.value(QStringLiteral("status")).isString())
        return fail(2001, QStringLiteral("invalid status"));
    const QString status = p.value(QStringLiteral("status")).toString();
    if (status != QLatin1String("frozen") && status != QLatin1String("normal"))
        return fail(2001, QStringLiteral("invalid status"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT userId FROM users WHERE userId = ?"));
    q.addBindValue(userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("user not found"));
    if (status == QLatin1String("frozen")) {
        QSqlQuery active(db);
        active.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE userId = ?"
                                      " AND status IN ('reserved', 'charging', 'pending_payment')"));
        active.addBindValue(userId);
        if (!exec(active) || !active.next())
            return fail(5000, QStringLiteral("internal error"));
        if (active.value(0).toLongLong() > 0)
            return fail(3002, QStringLiteral("user has unfinished orders"));
    }
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE users SET status = ? WHERE userId = ?"));
    upd.addBindValue(status);
    upd.addBindValue(userId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("userId"), userId);
    data.insert(QStringLiteral("status"), status);
    return ok(data);
}

using Handler = Response (*)(const QJsonObject &, Session &, QSqlDatabase );

struct MessageDef {
    int roles; // bit 1 = user, bit 2 = admin, 0 = no login required
    Handler handler;
};

const QHash<QString, MessageDef> &messageTable()
{
    static const QHash<QString, MessageDef> table = {
        {QStringLiteral("ping"), {0, hPing}},
        {QStringLiteral("user_login"), {0, hUserLogin}},
        {QStringLiteral("admin_login"), {0, hAdminLogin}},
        {QStringLiteral("user_profile_get"), {1, hProfileGet}},
        {QStringLiteral("user_profile_update"), {1, hProfileUpdate}},
        {QStringLiteral("wallet_recharge"), {1, hRecharge}},
        {QStringLiteral("nearby_station_list"), {1, hNearby}},
        {QStringLiteral("station_detail"), {3, hStationDetail}},
        {QStringLiteral("active_order_get"), {1, hActiveOrder}},
        {QStringLiteral("charge_reserve"), {1, hReserve}},
        {QStringLiteral("charge_start"), {1, hStart}},
        {QStringLiteral("charge_stop"), {1, hStop}},
        {QStringLiteral("charge_settle"), {1, hSettle}},
        {QStringLiteral("charge_cancel"), {1, hCancel}},
        {QStringLiteral("user_order_list"), {1, hOrderList}},
        {QStringLiteral("revenue_summary"), {2, hRevenueSummary}},
        {QStringLiteral("revenue_trend"), {2, hRevenueTrend}},
        {QStringLiteral("pile_status_overview"), {2, hPileStatusOverview}},
        {QStringLiteral("pile_list"), {2, hPileList}},
        {QStringLiteral("pile_restart"), {2, hPileRestart}},
        {QStringLiteral("station_list"), {2, hStationList}},
        {QStringLiteral("station_add"), {2, hStationAdd}},
        {QStringLiteral("user_list"), {2, hUserList}},
        {QStringLiteral("user_set_status"), {2, hUserSetStatus}},
    };
    return table;
}

} // namespace

Response Handlers::dispatch(const QString &type, const QJsonObject &payload, Session &session,
                            QSqlDatabase db, bool &closeConnection)
{
    closeConnection = false;
    const auto it = messageTable().constFind(type);
    if (it == messageTable().constEnd())
        return fail(3001, QStringLiteral("unknown type"));
    const MessageDef def = it.value();
    const bool isLogin = type == QLatin1String("user_login")
        || type == QLatin1String("admin_login");
    if (isLogin) {
        if (session.role != Session::None)
            return fail(3002, QStringLiteral("already logged in"));
        return def.handler(payload, session, db);
    }
    if (def.roles != 0) {
        if (session.role == Session::None)
            return fail(1003, QStringLiteral("not logged in"));
        if (session.role == Session::User && !(def.roles & 1))
            return fail(1004, QStringLiteral("permission denied"));
        if (session.role == Session::Admin && !(def.roles & 2))
            return fail(1004, QStringLiteral("permission denied"));
        if (session.role == Session::User) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT status FROM users WHERE userId = ?"));
            q.addBindValue(session.userId);
            if (!exec(q) || !q.next()
                || q.value(0).toString() == QLatin1String("frozen")) {
                closeConnection = true;
                return fail(1002, QStringLiteral("account frozen"));
            }
        }
    }
    return def.handler(payload, session, db);
}
