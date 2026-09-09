#include "handlers.h"

#include "stats.h"
#include "timeutil.h"

#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <cmath>

namespace {

constexpr qint64 kMaxId = (1LL << 52);
constexpr qint64 kMaxRechargeFen = 10000 * 100;
constexpr qint64 kMaxPriceFen = 1000000 * 100;
constexpr int kMaxAvatarBytes = 512 * 1024;
const QString kInitialPassword = QStringLiteral("123456");
const char *const kUnfinishedOrders = "('reserved', 'charging', 'pending_payment')";
const char *const kOccupyingOrders = "('reserved', 'charging')";

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

bool validPhone(const QString &phone)
{
    static const QRegularExpression re(QStringLiteral("^1[0-9]{10}$"));
    return re.match(phone).hasMatch();
}

bool validPileType(const QString &type)
{
    return type == QLatin1String("fast") || type == QLatin1String("slow");
}

bool validOrderStatus(const QString &status)
{
    return status == QLatin1String("reserved") || status == QLatin1String("charging")
        || status == QLatin1String("pending_payment") || status == QLatin1String("completed")
        || status == QLatin1String("cancelled");
}

bool readPowerKw(const QJsonObject &p, double &out)
{
    const QJsonValue v = p.value(QStringLiteral("powerKw"));
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d <= 0.0)
        return false;
    out = d;
    return true;
}

// Returns the date for an optional "yyyy-MM-dd" payload field; present=false when absent/null.
bool readDate(const QJsonObject &p, const QString &key, QDate &out, bool &present)
{
    present = p.contains(key) && !p.value(key).isNull();
    if (!present)
        return true;
    if (!p.value(key).isString())
        return false;
    const QString text = p.value(key).toString();
    static const QRegularExpression re(QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}$"));
    if (!re.match(text).hasMatch())
        return false;
    const QDate date = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid())
        return false;
    out = date;
    return true;
}

// Optional includeDeleted flag (admin lists only): absent means false.
bool readIncludeDeleted(const QJsonObject &p, bool &out)
{
    out = false;
    if (!p.contains(QStringLiteral("includeDeleted")))
        return true;
    if (!p.value(QStringLiteral("includeDeleted")).isBool())
        return false;
    out = p.value(QStringLiteral("includeDeleted")).toBool();
    return true;
}

enum class CodeCheck { Ok, Mismatch, DbError };

// One-time SMS-style code: consumed on success; expired rows are removed lazily.
CodeCheck consumeSmsCode(QSqlDatabase db, const QString &phone, const QString &code)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT code, expiresAtEpoch FROM codes WHERE phone = ?"));
    q.addBindValue(phone);
    if (!exec(q))
        return CodeCheck::DbError;
    if (!q.next())
        return CodeCheck::Mismatch;
    const bool expired = q.value(1).toLongLong() < TimeUtil::nowSecs();
    const bool match = q.value(0).toString() == code;
    if (expired || match) {
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM codes WHERE phone = ?"));
        del.addBindValue(phone);
        if (!exec(del))
            return CodeCheck::DbError;
    }
    if (expired || !match)
        return CodeCheck::Mismatch;
    return CodeCheck::Ok;
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

Response loadAdminOrder(QSqlDatabase db, qint64 orderId, QSqlQuery &q)
{
    q = QSqlQuery(db);
    q.prepare(QString::fromLatin1(Protocol::kAdminOrderSelect)
              + QStringLiteral(" WHERE o.orderId = ?"));
    q.addBindValue(orderId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("order not found"));
    return ok(QJsonValue());
}

Response adminOrderDataResponse(const QSqlQuery &q)
{
    QJsonObject data;
    data.insert(QStringLiteral("order"), Protocol::adminOrderJson(q));
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
    if (!validPhone(phone))
        return fail(2001, QStringLiteral("invalid phone"));
    const bool byPassword = p.value(QStringLiteral("password")).isString();
    const bool byCode = p.value(QStringLiteral("code")).isString();
    if (byPassword == byCode)
        return fail(2001, QStringLiteral("provide either password or code"));

    // A phone held only by deleted users is treated as unknown: login auto-registers
    // a brand-new account (1005 is only for existing sessions of a deleted account).
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kUserSelect)
              + QStringLiteral(" WHERE phone = ? AND deleted = 0"));
    q.addBindValue(phone);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    const bool found = q.next();
    if (found && q.value(4).toString() == QLatin1String("frozen"))
        return fail(1002, QStringLiteral("account frozen"));
    if (byCode) {
        const CodeCheck check = consumeSmsCode(db, phone,
                                               p.value(QStringLiteral("code")).toString());
        if (check == CodeCheck::DbError)
            return fail(5000, QStringLiteral("internal error"));
        if (check == CodeCheck::Mismatch)
            return fail(1001, QStringLiteral("invalid or expired code"));
    } else if (found) {
        if (q.value(8).isNull()
            || !Protocol::verifyPassword(q.value(8).toString(),
                                         p.value(QStringLiteral("password")).toString()))
            return fail(1001, QStringLiteral("invalid credentials"));
    }
    if (!found) {
        // 密码方式自动注册直接保存该密码（hasPassword=true）；
        // 验证码方式注册不设置密码（null QString 绑定为 SQL NULL）。
        QString passwordHash;
        if (byPassword) {
            const QString password = p.value(QStringLiteral("password")).toString();
            if (!Protocol::isValidPassword(password))
                return fail(2001, QStringLiteral("invalid password"));
            passwordHash = Protocol::passwordRecord(password);
        }
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral("INSERT INTO users (phone, nickname, balanceFen, status,"
                                   " passwordHash, regTime) VALUES (?, ?, 0, 'normal', ?, ?)"));
        ins.addBindValue(phone);
        ins.addBindValue(QStringLiteral("用户") + phone.right(4));
        ins.addBindValue(passwordHash);
        ins.addBindValue(TimeUtil::nowSecs());
        if (!exec(ins))
            return fail(5000, QStringLiteral("internal error"));
        QSqlQuery q2(db);
        q2.prepare(QString::fromLatin1(Protocol::kUserSelect)
                   + QStringLiteral(" WHERE phone = ? AND deleted = 0"));
        q2.addBindValue(phone);
        if (!exec(q2) || !q2.next())
            return fail(5000, QStringLiteral("internal error"));
        return finishUserLogin(q2, s, true);
    }
    return finishUserLogin(q, s, false);
}

Response hCodeRequest(const QJsonObject &p, Session &, QSqlDatabase db)
{
    const QString phone = p.value(QStringLiteral("phone")).toString();
    if (!validPhone(phone))
        return fail(2001, QStringLiteral("invalid phone"));
    // Unknown phones (including ones held only by deleted users) also get a code:
    // it is what lets the auto-registration login verify them.
    const QString code = QStringLiteral("%1")
        .arg(QRandomGenerator::global()->bounded(1000000), 6, 10, QLatin1Char('0'));
    QSqlQuery up(db);
    up.prepare(QStringLiteral("INSERT OR REPLACE INTO codes (phone, code, expiresAtEpoch)"
                              " VALUES (?, ?, ?)"));
    up.addBindValue(phone);
    up.addBindValue(code);
    up.addBindValue(TimeUtil::nowSecs() + 300);
    if (!exec(up))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("code"), code);
    data.insert(QStringLiteral("validSec"), 300);
    return ok(data);
}

Response hPasswordReset(const QJsonObject &p, Session &, QSqlDatabase db)
{
    const QString phone = p.value(QStringLiteral("phone")).toString();
    if (!validPhone(phone))
        return fail(2001, QStringLiteral("invalid phone"));
    if (!p.value(QStringLiteral("code")).isString())
        return fail(2001, QStringLiteral("invalid code"));
    const QString code = p.value(QStringLiteral("code")).toString();
    if (!p.value(QStringLiteral("newPassword")).isString()
        || !Protocol::isValidPassword(p.value(QStringLiteral("newPassword")).toString()))
        return fail(2001, QStringLiteral("invalid newPassword"));
    const QString newPassword = p.value(QStringLiteral("newPassword")).toString();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT userId FROM users WHERE phone = ? AND deleted = 0"));
    q.addBindValue(phone);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("user not found"));
    const CodeCheck check = consumeSmsCode(db, phone, code);
    if (check == CodeCheck::DbError)
        return fail(5000, QStringLiteral("internal error"));
    if (check == CodeCheck::Mismatch)
        return fail(1001, QStringLiteral("invalid or expired code"));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE users SET passwordHash = ? WHERE userId = ?"));
    upd.addBindValue(Protocol::passwordRecord(newPassword));
    upd.addBindValue(q.value(0).toLongLong());
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("phone"), phone);
    return ok(data);
}

Response hUserPasswordUpdate(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    if (!p.value(QStringLiteral("newPassword")).isString()
        || !Protocol::isValidPassword(p.value(QStringLiteral("newPassword")).toString()))
        return fail(2001, QStringLiteral("invalid newPassword"));
    const QString newPassword = p.value(QStringLiteral("newPassword")).toString();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT passwordHash FROM users WHERE userId = ?"));
    q.addBindValue(s.userId);
    if (!exec(q) || !q.next())
        return fail(5000, QStringLiteral("internal error"));
    if (!q.value(0).isNull()) {
        const QString record = q.value(0).toString();
        const QString oldPassword = p.value(QStringLiteral("oldPassword")).toString();
        if (!p.value(QStringLiteral("oldPassword")).isString()
            || !Protocol::verifyPassword(record, oldPassword))
            return fail(1001, QStringLiteral("invalid old password"));
        if (Protocol::verifyPassword(record, newPassword))
            return fail(2001, QStringLiteral("new password must differ from old"));
    }
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE users SET passwordHash = ? WHERE userId = ?"));
    upd.addBindValue(Protocol::passwordRecord(newPassword));
    upd.addBindValue(s.userId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("hasPassword"), true);
    return ok(data);
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
    if (!q.next() || !Protocol::verifyPassword(q.value(1).toString(), password))
        return fail(1001, QStringLiteral("invalid credentials"));
    s.role = Session::Admin;
    s.adminId = q.value(0).toLongLong();
    QJsonObject data;
    data.insert(QStringLiteral("adminId"), s.adminId);
    data.insert(QStringLiteral("username"), username);
    return ok(data);
}

Response hAdminPasswordUpdate(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    if (!p.value(QStringLiteral("oldPassword")).isString()
        || !p.value(QStringLiteral("newPassword")).isString())
        return fail(2001, QStringLiteral("invalid params"));
    const QString oldPassword = p.value(QStringLiteral("oldPassword")).toString();
    const QString newPassword = p.value(QStringLiteral("newPassword")).toString();
    if (!Protocol::isValidPassword(newPassword))
        return fail(2001, QStringLiteral("invalid newPassword"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT passwordHash FROM admins WHERE adminId = ?"));
    q.addBindValue(s.adminId);
    if (!exec(q) || !q.next())
        return fail(5000, QStringLiteral("internal error"));
    if (!Protocol::verifyPassword(q.value(0).toString(), oldPassword))
        return fail(1001, QStringLiteral("invalid old password"));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE admins SET passwordHash = ? WHERE adminId = ?"));
    upd.addBindValue(Protocol::passwordRecord(newPassword));
    upd.addBindValue(s.adminId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("adminId"), s.adminId);
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
              + QStringLiteral(" WHERE s.stationId = ? AND s.deleted = 0 GROUP BY s.stationId"));
    q.addBindValue(stationId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("station not found"));
    const QJsonObject station = Protocol::stationSummaryJson(q);
    QSqlQuery piles(db);
    piles.prepare(QString::fromLatin1(Protocol::kPileSelect)
                  + QStringLiteral(" WHERE p.stationId = ? AND p.deleted = 0 ORDER BY p.code"));
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
                               " ORDER BY o.reservedAt DESC, o.orderId DESC"));
    q.addBindValue(s.userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray orders;
    while (q.next())
        orders.append(Protocol::orderJson(q));
    QJsonObject data;
    data.insert(QStringLiteral("orders"), orders);
    return ok(data);
}

Response hReserve(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 pileId = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileId"), 1, kMaxId, pileId))
        return fail(2001, QStringLiteral("invalid pileId"));
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery bal(db);
    bal.prepare(QStringLiteral("SELECT balanceFen FROM users WHERE userId = ?"));
    bal.addBindValue(s.userId);
    if (!exec(bal) || !bal.next()) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (bal.value(0).toLongLong() <= 0) {
        db.rollback();
        return fail(3004, QStringLiteral("balance too low, please recharge first"));
    }
    // 同一用户可以拥有多个未完成订单。
    QSqlQuery pq(db);
    pq.prepare(QStringLiteral("SELECT p.status, p.stationId, s.priceFenPerKwh"
                              " FROM piles p JOIN stations s ON s.stationId = p.stationId"
                              " WHERE p.pileId = ? AND p.deleted = 0"));
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

// charging 订单计费停止并释放电桩（累计充电次数与时长），调用方须已开启事务。
bool stopChargingInTx(QSqlDatabase db, qint64 orderId, qint64 pileId, qint64 startTime,
                      qint64 unitPriceFen, double powerKw)
{
    const qint64 now = TimeUtil::nowSecs();
    const qint64 durationSecs = qMax<qint64>(0, now - startTime);
    const qint64 energyWh = static_cast<qint64>(
        std::llround(powerKw * static_cast<double>(durationSecs) * 1000.0 / 3600.0));
    const qint64 amountFen = (energyWh * unitPriceFen + 500) / 1000;
    const qint64 minutes = static_cast<qint64>(
        std::llround(static_cast<double>(now - startTime) / 60.0));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE orders SET status = 'pending_payment', endTime = ?,"
                               " energyWh = ?, amountFen = ? WHERE orderId = ?"));
    upd.addBindValue(now);
    upd.addBindValue(energyWh);
    upd.addBindValue(amountFen);
    upd.addBindValue(orderId);
    QSqlQuery release(db);
    release.prepare(QStringLiteral("UPDATE piles SET status = 'idle',"
                                   " chargeCount = chargeCount + 1,"
                                   " chargeMinutes = chargeMinutes + ? WHERE pileId = ?"));
    release.addBindValue(minutes);
    release.addBindValue(pileId);
    return exec(upd) && exec(release);
}

// 强制终结电桩的占用订单（reserved→cancelled，charging→计费停止），调用方须已开启事务。
// 无占用订单时 affectedOrderId 保持 0、affectedStatus 保持空。
bool terminateOccupyingOrderInTx(QSqlDatabase db, qint64 pileId, qint64 &affectedOrderId,
                                 QString &affectedStatus)
{
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kOrderSelect)
              + QStringLiteral(" WHERE o.pileId = ? AND o.status IN ")
              + QLatin1String(kOccupyingOrders)
              + QStringLiteral(" ORDER BY o.reservedAt DESC, o.orderId DESC LIMIT 1"));
    q.addBindValue(pileId);
    if (!exec(q))
        return false;
    if (!q.next())
        return true;
    const qint64 orderId = q.value(0).toLongLong();
    if (q.value(3).toString() == QLatin1String("reserved")) {
        QSqlQuery cancel(db);
        cancel.prepare(QStringLiteral("UPDATE orders SET status = 'cancelled' WHERE orderId = ?"));
        cancel.addBindValue(orderId);
        if (!exec(cancel))
            return false;
        affectedStatus = QStringLiteral("cancelled");
    } else {
        if (!stopChargingInTx(db, orderId, pileId, q.value(6).toLongLong(),
                              q.value(4).toLongLong(), q.value(14).toDouble()))
            return false;
        affectedStatus = QStringLiteral("pending_payment");
    }
    affectedOrderId = orderId;
    return true;
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
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    if (!stopChargingInTx(db, orderId, q.value(2).toLongLong(), q.value(6).toLongLong(),
                          q.value(4).toLongLong(), q.value(14).toDouble())) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!db.commit())
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
    if (!exec(deduct) || !exec(done)) {
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
    bool includeDeleted = false;
    if (!readIncludeDeleted(p, includeDeleted))
        return fail(2001, QStringLiteral("invalid includeDeleted"));
    QStringList conditions;
    if (!includeDeleted)
        conditions.append(QStringLiteral("p.deleted = 0"));
    if (stationId > 0)
        conditions.append(QStringLiteral("p.stationId = ?"));
    if (!status.isEmpty())
        conditions.append(QStringLiteral("p.status = ?"));
    QString sql = QString::fromLatin1(Protocol::kPileSelect);
    if (!conditions.isEmpty())
        sql += QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
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
    while (q.next()) {
        QJsonObject pile = Protocol::pileJson(q);
        if (q.value(10).isNull())
            pile.insert(QStringLiteral("occupancy"), QJsonValue(QJsonValue::Null));
        else
            pile.insert(QStringLiteral("occupancy"), q.value(10).toString());
        if (includeDeleted)
            pile.insert(QStringLiteral("deleted"), q.value(9).toInt() != 0);
        piles.append(pile);
    }
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
    q.prepare(QStringLiteral("SELECT status FROM piles WHERE pileId = ? AND deleted = 0"));
    q.addBindValue(pileId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("pile not found"));
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    qint64 affectedOrderId = 0;
    QString affectedStatus;
    if (!terminateOccupyingOrderInTx(db, pileId, affectedOrderId, affectedStatus)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE piles SET status = 'idle' WHERE pileId = ?"));
    upd.addBindValue(pileId);
    if (!exec(upd)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("pileId"), pileId);
    data.insert(QStringLiteral("status"), QStringLiteral("idle"));
    if (affectedOrderId > 0) {
        data.insert(QStringLiteral("affectedOrderId"), affectedOrderId);
        data.insert(QStringLiteral("affectedOrderStatus"), affectedStatus);
    } else {
        data.insert(QStringLiteral("affectedOrderId"), QJsonValue(QJsonValue::Null));
        data.insert(QStringLiteral("affectedOrderStatus"), QJsonValue(QJsonValue::Null));
    }
    return ok(data);
}

Response hPileDisable(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 pileId = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileId"), 1, kMaxId, pileId))
        return fail(2001, QStringLiteral("invalid pileId"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT status FROM piles WHERE pileId = ? AND deleted = 0"));
    q.addBindValue(pileId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("pile not found"));
    if (q.value(0).toString() == QLatin1String("fault"))
        return fail(3002, QStringLiteral("pile already fault"));
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    qint64 affectedOrderId = 0;
    QString affectedStatus;
    if (!terminateOccupyingOrderInTx(db, pileId, affectedOrderId, affectedStatus)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE piles SET status = 'fault' WHERE pileId = ?"));
    upd.addBindValue(pileId);
    if (!exec(upd)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("pileId"), pileId);
    data.insert(QStringLiteral("status"), QStringLiteral("fault"));
    if (affectedOrderId > 0) {
        data.insert(QStringLiteral("affectedOrderId"), affectedOrderId);
        data.insert(QStringLiteral("affectedOrderStatus"), affectedStatus);
    } else {
        data.insert(QStringLiteral("affectedOrderId"), QJsonValue(QJsonValue::Null));
        data.insert(QStringLiteral("affectedOrderStatus"), QJsonValue(QJsonValue::Null));
    }
    return ok(data);
}

Response hPileActiveOrder(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 pileId = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileId"), 1, kMaxId, pileId))
        return fail(2001, QStringLiteral("invalid pileId"));
    QSqlQuery pile(db);
    pile.prepare(QStringLiteral("SELECT 1 FROM piles WHERE pileId = ? AND deleted = 0"));
    pile.addBindValue(pileId);
    if (!exec(pile))
        return fail(5000, QStringLiteral("internal error"));
    if (!pile.next())
        return fail(2002, QStringLiteral("pile not found"));
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kAdminOrderSelect)
              + QStringLiteral(" WHERE o.pileId = ? AND o.status IN ")
              + QLatin1String(kOccupyingOrders)
              + QStringLiteral(" ORDER BY o.reservedAt DESC, o.orderId DESC LIMIT 1"));
    q.addBindValue(pileId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    if (q.next())
        data.insert(QStringLiteral("order"), Protocol::adminOrderJson(q));
    else
        data.insert(QStringLiteral("order"), QJsonValue(QJsonValue::Null));
    return ok(data);
}

Response hPileAdd(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 stationId = 0;
    if (!Protocol::readInt(p, QStringLiteral("stationId"), 1, kMaxId, stationId))
        return fail(2001, QStringLiteral("invalid stationId"));
    if (!p.value(QStringLiteral("code")).isString())
        return fail(2001, QStringLiteral("invalid code"));
    const QString code = p.value(QStringLiteral("code")).toString().trimmed();
    if (code.isEmpty() || code.length() > 20)
        return fail(2001, QStringLiteral("invalid code"));
    if (!p.value(QStringLiteral("type")).isString()
        || !validPileType(p.value(QStringLiteral("type")).toString()))
        return fail(2001, QStringLiteral("invalid type"));
    const QString type = p.value(QStringLiteral("type")).toString();
    double powerKw = 0.0;
    if (!readPowerKw(p, powerKw))
        return fail(2001, QStringLiteral("invalid powerKw"));
    QSqlQuery station(db);
    station.prepare(QStringLiteral("SELECT stationId FROM stations WHERE stationId = ? AND deleted = 0"));
    station.addBindValue(stationId);
    if (!exec(station))
        return fail(5000, QStringLiteral("internal error"));
    if (!station.next())
        return fail(2002, QStringLiteral("station not found"));
    QSqlQuery dup(db);
    dup.prepare(QStringLiteral("SELECT COUNT(*) FROM piles WHERE code = ?"));
    dup.addBindValue(code);
    if (!exec(dup) || !dup.next())
        return fail(5000, QStringLiteral("internal error"));
    if (dup.value(0).toLongLong() > 0)
        return fail(2001, QStringLiteral("code already exists"));
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO piles (code, stationId, type, powerKw, status)"
                               " VALUES (?, ?, ?, ?, 'idle')"));
    ins.addBindValue(code);
    ins.addBindValue(stationId);
    ins.addBindValue(type);
    ins.addBindValue(powerKw);
    if (!exec(ins))
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kPileSelect)
              + QStringLiteral(" WHERE p.pileId = ?"));
    q.addBindValue(ins.lastInsertId().toLongLong());
    if (!exec(q) || !q.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("pile"), Protocol::pileJson(q));
    return ok(data);
}

Response hPileUpdate(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 pileId = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileId"), 1, kMaxId, pileId))
        return fail(2001, QStringLiteral("invalid pileId"));
    const bool hasType = p.contains(QStringLiteral("type"));
    const bool hasPower = p.contains(QStringLiteral("powerKw"));
    if (!hasType && !hasPower)
        return fail(2001, QStringLiteral("nothing to update"));
    QString type;
    double powerKw = 0.0;
    if (hasType) {
        if (!p.value(QStringLiteral("type")).isString()
            || !validPileType(p.value(QStringLiteral("type")).toString()))
            return fail(2001, QStringLiteral("invalid type"));
        type = p.value(QStringLiteral("type")).toString();
    }
    if (hasPower && !readPowerKw(p, powerKw))
        return fail(2001, QStringLiteral("invalid powerKw"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT status, deleted FROM piles WHERE pileId = ?"));
    q.addBindValue(pileId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next() || q.value(1).toInt() != 0)
        return fail(2002, QStringLiteral("pile not found"));
    QSqlQuery active(db);
    active.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE pileId = ? AND status IN ")
                   + QLatin1String(kOccupyingOrders));
    active.addBindValue(pileId);
    if (!exec(active) || !active.next())
        return fail(5000, QStringLiteral("internal error"));
    if (active.value(0).toLongLong() > 0)
        return fail(3002, QStringLiteral("pile has occupying order"));
    QSqlQuery upd(db);
    if (hasType && hasPower) {
        upd.prepare(QStringLiteral("UPDATE piles SET type = ?, powerKw = ? WHERE pileId = ?"));
        upd.addBindValue(type);
        upd.addBindValue(powerKw);
    } else if (hasType) {
        upd.prepare(QStringLiteral("UPDATE piles SET type = ? WHERE pileId = ?"));
        upd.addBindValue(type);
    } else {
        upd.prepare(QStringLiteral("UPDATE piles SET powerKw = ? WHERE pileId = ?"));
        upd.addBindValue(powerKw);
    }
    upd.addBindValue(pileId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery fresh(db);
    fresh.prepare(QString::fromLatin1(Protocol::kPileSelect)
                  + QStringLiteral(" WHERE p.pileId = ?"));
    fresh.addBindValue(pileId);
    if (!exec(fresh) || !fresh.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("pile"), Protocol::pileJson(fresh));
    return ok(data);
}

Response hPileDelete(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 pileId = 0;
    if (!Protocol::readInt(p, QStringLiteral("pileId"), 1, kMaxId, pileId))
        return fail(2001, QStringLiteral("invalid pileId"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT status, deleted FROM piles WHERE pileId = ?"));
    q.addBindValue(pileId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next() || q.value(1).toInt() != 0)
        return fail(2002, QStringLiteral("pile not found"));
    if (q.value(0).toString() != QLatin1String("idle"))
        return fail(3002, QStringLiteral("pile is not idle"));
    QSqlQuery active(db);
    active.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE pileId = ? AND status IN ")
                   + QLatin1String(kOccupyingOrders));
    active.addBindValue(pileId);
    if (!exec(active) || !active.next())
        return fail(5000, QStringLiteral("internal error"));
    if (active.value(0).toLongLong() > 0)
        return fail(3002, QStringLiteral("pile has occupying order"));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE piles SET deleted = 1 WHERE pileId = ?"));
    upd.addBindValue(pileId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("pileId"), pileId);
    data.insert(QStringLiteral("deleted"), true);
    return ok(data);
}

Response hStationList(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 page = 1, pageSize = 20;
    if (p.contains(QStringLiteral("page"))
        && !Protocol::readInt(p, QStringLiteral("page"), 1, kMaxId, page))
        return fail(2001, QStringLiteral("invalid page"));
    if (p.contains(QStringLiteral("pageSize"))
        && !Protocol::readInt(p, QStringLiteral("pageSize"), 1, 100, pageSize))
        return fail(2001, QStringLiteral("invalid pageSize"));
    QString keyword;
    if (p.contains(QStringLiteral("nameKeyword"))) {
        if (!p.value(QStringLiteral("nameKeyword")).isString())
            return fail(2001, QStringLiteral("invalid nameKeyword"));
        keyword = p.value(QStringLiteral("nameKeyword")).toString();
    }
    bool includeDeleted = false;
    if (!readIncludeDeleted(p, includeDeleted))
        return fail(2001, QStringLiteral("invalid includeDeleted"));
    QStringList conditions;
    if (!includeDeleted)
        conditions.append(QStringLiteral("s.deleted = 0"));
    if (!keyword.isEmpty())
        conditions.append(QStringLiteral("s.name LIKE ?"));
    QString where;
    if (!conditions.isEmpty())
        where = QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
    QSqlQuery count(db);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM stations s") + where);
    if (!keyword.isEmpty())
        count.addBindValue(QLatin1Char('%') + keyword + QLatin1Char('%'));
    if (!exec(count) || !count.next())
        return fail(5000, QStringLiteral("internal error"));
    const qint64 total = count.value(0).toLongLong();
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kStationAggregateSelect) + where
              + QStringLiteral(" GROUP BY s.stationId ORDER BY s.stationId LIMIT ? OFFSET ?"));
    if (!keyword.isEmpty())
        q.addBindValue(QLatin1Char('%') + keyword + QLatin1Char('%'));
    q.addBindValue(pageSize);
    q.addBindValue((page - 1) * pageSize);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray stations;
    while (q.next()) {
        QJsonObject station = Protocol::stationSummaryJson(q);
        if (includeDeleted)
            station.insert(QStringLiteral("deleted"), q.value(9).toInt() != 0);
        stations.append(station);
    }
    QJsonObject data;
    data.insert(QStringLiteral("page"), page);
    data.insert(QStringLiteral("pageSize"), pageSize);
    data.insert(QStringLiteral("total"), total);
    data.insert(QStringLiteral("stations"), stations);
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
    if (!p.value(QStringLiteral("piles")).isArray())
        return fail(2001, QStringLiteral("invalid piles"));
    const QJsonArray piles = p.value(QStringLiteral("piles")).toArray();
    if (piles.isEmpty() || piles.size() > 100)
        return fail(2001, QStringLiteral("invalid piles"));
    QStringList codes;
    QStringList types;
    QList<double> powers;
    for (const QJsonValue &v : piles) {
        if (!v.isObject())
            return fail(2001, QStringLiteral("invalid pile entry"));
        const QJsonObject pile = v.toObject();
        if (!pile.value(QStringLiteral("code")).isString())
            return fail(2001, QStringLiteral("invalid code"));
        const QString code = pile.value(QStringLiteral("code")).toString().trimmed();
        if (code.isEmpty() || code.length() > 20)
            return fail(2001, QStringLiteral("invalid code"));
        if (codes.contains(code))
            return fail(2001, QStringLiteral("duplicate code"));
        if (!pile.value(QStringLiteral("type")).isString()
            || !validPileType(pile.value(QStringLiteral("type")).toString()))
            return fail(2001, QStringLiteral("invalid type"));
        double powerKw = 0.0;
        if (!readPowerKw(pile, powerKw))
            return fail(2001, QStringLiteral("invalid powerKw"));
        codes.append(code);
        types.append(pile.value(QStringLiteral("type")).toString());
        powers.append(powerKw);
    }
    QStringList placeholders;
    for (int i = 0; i < codes.size(); ++i)
        placeholders.append(QStringLiteral("?"));
    QSqlQuery dup(db);
    dup.prepare(QStringLiteral("SELECT COUNT(*) FROM piles WHERE code IN (")
                + placeholders.join(QLatin1Char(',')) + QStringLiteral(")"));
    for (const QString &code : codes)
        dup.addBindValue(code);
    if (!exec(dup) || !dup.next())
        return fail(5000, QStringLiteral("internal error"));
    if (dup.value(0).toLongLong() > 0)
        return fail(2001, QStringLiteral("code already exists"));

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
    QSqlQuery pileIns(db);
    pileIns.prepare(QStringLiteral("INSERT INTO piles (code, stationId, type, powerKw, status)"
                                   " VALUES (?, ?, ?, ?, 'idle')"));
    for (int i = 0; i < codes.size(); ++i) {
        pileIns.bindValue(0, codes.at(i));
        pileIns.bindValue(1, stationId);
        pileIns.bindValue(2, types.at(i));
        pileIns.bindValue(3, powers.at(i));
        if (!pileIns.exec()) {
            db.rollback();
            // 并发插入撞上 code 的列级 UNIQUE 约束同样按编号冲突处理。
            if (pileIns.lastError().text().contains(QLatin1String("UNIQUE")))
                return fail(2001, QStringLiteral("code already exists"));
            qWarning() << "SQL error:" << pileIns.lastError().text();
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
    data.insert(QStringLiteral("createdPileCount"), static_cast<int>(codes.size()));
    return ok(data);
}

Response hStationUpdate(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 stationId = 0;
    if (!Protocol::readInt(p, QStringLiteral("stationId"), 1, kMaxId, stationId))
        return fail(2001, QStringLiteral("invalid stationId"));
    const bool hasName = p.contains(QStringLiteral("name"));
    const bool hasAddress = p.contains(QStringLiteral("address"));
    const bool hasPrice = p.contains(QStringLiteral("pricePerKwh"));
    if (!hasName && !hasAddress && !hasPrice)
        return fail(2001, QStringLiteral("nothing to update"));
    QString name, address;
    qint64 priceFen = 0;
    if (hasName) {
        if (!p.value(QStringLiteral("name")).isString())
            return fail(2001, QStringLiteral("invalid name"));
        name = p.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            return fail(2001, QStringLiteral("invalid name"));
    }
    if (hasAddress) {
        if (!p.value(QStringLiteral("address")).isString())
            return fail(2001, QStringLiteral("invalid address"));
        address = p.value(QStringLiteral("address")).toString().trimmed();
        if (address.isEmpty())
            return fail(2001, QStringLiteral("invalid address"));
    }
    if (hasPrice
        && !Protocol::readMoneyFen(p, QStringLiteral("pricePerKwh"), kMaxPriceFen, priceFen))
        return fail(2001, QStringLiteral("invalid pricePerKwh"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT deleted FROM stations WHERE stationId = ?"));
    q.addBindValue(stationId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next() || q.value(0).toInt() != 0)
        return fail(2002, QStringLiteral("station not found"));
    QStringList sets;
    QVariantList binds;
    if (hasName) {
        sets.append(QStringLiteral("name = ?"));
        binds.append(name);
    }
    if (hasAddress) {
        sets.append(QStringLiteral("address = ?"));
        binds.append(address);
    }
    if (hasPrice) {
        sets.append(QStringLiteral("priceFenPerKwh = ?"));
        binds.append(priceFen);
    }
    // lng/lat are intentionally not updatable; submitted values are ignored.
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE stations SET ") + sets.join(QStringLiteral(", "))
                + QStringLiteral(" WHERE stationId = ?"));
    for (const QVariant &bind : binds)
        upd.addBindValue(bind);
    upd.addBindValue(stationId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery fresh(db);
    fresh.prepare(QString::fromLatin1(Protocol::kStationAggregateSelect)
                  + QStringLiteral(" WHERE s.stationId = ? GROUP BY s.stationId"));
    fresh.addBindValue(stationId);
    if (!exec(fresh) || !fresh.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("station"), Protocol::stationSummaryJson(fresh));
    return ok(data);
}

Response hStationDelete(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 stationId = 0;
    if (!Protocol::readInt(p, QStringLiteral("stationId"), 1, kMaxId, stationId))
        return fail(2001, QStringLiteral("invalid stationId"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT deleted FROM stations WHERE stationId = ?"));
    q.addBindValue(stationId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next() || q.value(0).toInt() != 0)
        return fail(2002, QStringLiteral("station not found"));
    QSqlQuery active(db);
    active.prepare(QStringLiteral("SELECT COUNT(*) FROM orders o"
                                  " JOIN piles p ON p.pileId = o.pileId"
                                  " WHERE p.stationId = ? AND o.status IN ")
                   + QLatin1String(kOccupyingOrders));
    active.addBindValue(stationId);
    if (!exec(active) || !active.next())
        return fail(5000, QStringLiteral("internal error"));
    if (active.value(0).toLongLong() > 0)
        return fail(3002, QStringLiteral("station has piles with unfinished orders"));
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery delStation(db);
    delStation.prepare(QStringLiteral("UPDATE stations SET deleted = 1 WHERE stationId = ?"));
    delStation.addBindValue(stationId);
    QSqlQuery delPiles(db);
    delPiles.prepare(QStringLiteral("UPDATE piles SET deleted = 1"
                                    " WHERE stationId = ? AND deleted = 0"));
    delPiles.addBindValue(stationId);
    if (!exec(delStation) || !exec(delPiles)) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    const qint64 removedPileCount = delPiles.numRowsAffected();
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("stationId"), stationId);
    data.insert(QStringLiteral("deleted"), true);
    data.insert(QStringLiteral("removedPileCount"), removedPileCount);
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
    bool includeDeleted = false;
    if (!readIncludeDeleted(p, includeDeleted))
        return fail(2001, QStringLiteral("invalid includeDeleted"));
    QStringList conditions;
    if (!includeDeleted)
        conditions.append(QStringLiteral("deleted = 0"));
    if (!keyword.isEmpty())
        conditions.append(QStringLiteral("phone LIKE ?"));
    QString sql = QString::fromLatin1(Protocol::kUserSelect);
    if (!conditions.isEmpty())
        sql += QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY userId");
    QSqlQuery q(db);
    q.prepare(sql);
    if (!keyword.isEmpty())
        q.addBindValue(QLatin1Char('%') + keyword + QLatin1Char('%'));
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray users;
    while (q.next()) {
        QJsonObject user = Protocol::userJson(q, false);
        if (includeDeleted)
            user.insert(QStringLiteral("deleted"), q.value(9).toInt() != 0);
        users.append(user);
    }
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
    q.prepare(QStringLiteral("SELECT userId FROM users WHERE userId = ? AND deleted = 0"));
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

Response userSummaryResponse(QSqlDatabase db, qint64 userId)
{
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kUserSelect)
              + QStringLiteral(" WHERE userId = ?"));
    q.addBindValue(userId);
    if (!exec(q) || !q.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("user"), Protocol::userJson(q, false));
    return ok(data);
}

Response userProfileResponse(QSqlDatabase db, qint64 userId)
{
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kUserSelect)
              + QStringLiteral(" WHERE userId = ?"));
    q.addBindValue(userId);
    if (!exec(q) || !q.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("user"), Protocol::userJson(q, true));
    return ok(data);
}

Response hUserAdd(const QJsonObject &p, Session &, QSqlDatabase db)
{
    if (!p.value(QStringLiteral("phone")).isString())
        return fail(2001, QStringLiteral("invalid phone"));
    const QString phone = p.value(QStringLiteral("phone")).toString();
    if (!validPhone(phone))
        return fail(2001, QStringLiteral("invalid phone"));
    QString nickname = QStringLiteral("用户") + phone.right(4);
    if (p.contains(QStringLiteral("nickname"))) {
        if (!p.value(QStringLiteral("nickname")).isString())
            return fail(2001, QStringLiteral("invalid nickname"));
        nickname = p.value(QStringLiteral("nickname")).toString().trimmed();
        if (nickname.isEmpty() || nickname.length() > 20)
            return fail(2001, QStringLiteral("invalid nickname"));
    }
    QString password = kInitialPassword;
    if (p.contains(QStringLiteral("password"))) {
        if (!p.value(QStringLiteral("password")).isString()
            || !Protocol::isValidPassword(p.value(QStringLiteral("password")).toString()))
            return fail(2001, QStringLiteral("invalid password"));
        password = p.value(QStringLiteral("password")).toString();
    }
    QSqlQuery dup(db);
    dup.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE phone = ? AND deleted = 0"));
    dup.addBindValue(phone);
    if (!exec(dup) || !dup.next())
        return fail(5000, QStringLiteral("internal error"));
    if (dup.value(0).toLongLong() > 0)
        return fail(2001, QStringLiteral("phone already exists"));
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO users (phone, nickname, balanceFen, status,"
                               " passwordHash, regTime) VALUES (?, ?, 0, 'normal', ?, ?)"));
    ins.addBindValue(phone);
    ins.addBindValue(nickname);
    ins.addBindValue(Protocol::passwordRecord(password));
    ins.addBindValue(TimeUtil::nowSecs());
    if (!exec(ins))
        return fail(5000, QStringLiteral("internal error"));
    return userSummaryResponse(db, ins.lastInsertId().toLongLong());
}

Response hUserUpdate(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 userId = 0;
    if (!Protocol::readInt(p, QStringLiteral("userId"), 1, kMaxId, userId))
        return fail(2001, QStringLiteral("invalid userId"));
    const bool hasPhone = p.contains(QStringLiteral("phone"));
    const bool hasNick = p.contains(QStringLiteral("nickname"));
    const bool hasAvatar = p.contains(QStringLiteral("avatar"));
    const bool hasBalance = p.contains(QStringLiteral("balance"));
    if (!hasPhone && !hasNick && !hasAvatar && !hasBalance)
        return fail(2001, QStringLiteral("nothing to update"));
    QString phone, nickname;
    if (hasPhone) {
        if (!p.value(QStringLiteral("phone")).isString())
            return fail(2001, QStringLiteral("invalid phone"));
        phone = p.value(QStringLiteral("phone")).toString();
        if (!validPhone(phone))
            return fail(2001, QStringLiteral("invalid phone"));
    }
    if (hasNick) {
        if (!p.value(QStringLiteral("nickname")).isString())
            return fail(2001, QStringLiteral("invalid nickname"));
        nickname = p.value(QStringLiteral("nickname")).toString().trimmed();
        if (nickname.isEmpty() || nickname.length() > 20)
            return fail(2001, QStringLiteral("invalid nickname"));
    }
    // 显式 null 表示清除头像；省略表示不修改。规则与 hProfileUpdate 相同。
    QString avatarMime, avatarB64;
    if (hasAvatar && !p.value(QStringLiteral("avatar")).isNull()) {
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
    // balance 允许 0，不能用 readMoneyFen（要求 >0）。
    qint64 balanceFen = 0;
    if (hasBalance) {
        const QJsonValue v = p.value(QStringLiteral("balance"));
        if (!v.isDouble())
            return fail(2001, QStringLiteral("invalid balance"));
        const double fen = v.toDouble() * 100.0;
        if (std::fabs(fen - std::round(fen)) > 1e-6)
            return fail(2001, QStringLiteral("invalid balance"));
        balanceFen = static_cast<qint64>(std::round(fen));
        if (balanceFen < 0 || balanceFen > kMaxPriceFen)
            return fail(2001, QStringLiteral("invalid balance"));
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT deleted FROM users WHERE userId = ?"));
    q.addBindValue(userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next() || q.value(0).toInt() != 0)
        return fail(2002, QStringLiteral("user not found"));
    if (hasPhone) {
        QSqlQuery dup(db);
        dup.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE phone = ? AND userId != ?"
                                   " AND deleted = 0"));
        dup.addBindValue(phone);
        dup.addBindValue(userId);
        if (!exec(dup) || !dup.next())
            return fail(5000, QStringLiteral("internal error"));
        if (dup.value(0).toLongLong() > 0)
            return fail(2001, QStringLiteral("phone already exists"));
    }
    QStringList sets;
    QVariantList binds;
    if (hasPhone) {
        sets.append(QStringLiteral("phone = ?"));
        binds.append(phone);
    }
    if (hasNick) {
        sets.append(QStringLiteral("nickname = ?"));
        binds.append(nickname);
    }
    if (hasAvatar) {
        sets.append(QStringLiteral("avatarMime = ?"));
        binds.append(avatarMime);
        sets.append(QStringLiteral("avatarBase64 = ?"));
        binds.append(avatarB64);
    }
    if (hasBalance) {
        sets.append(QStringLiteral("balanceFen = ?"));
        binds.append(balanceFen);
    }
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE users SET ") + sets.join(QStringLiteral(", "))
                + QStringLiteral(" WHERE userId = ?"));
    for (const QVariant &bind : binds)
        upd.addBindValue(bind);
    upd.addBindValue(userId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    return userProfileResponse(db, userId);
}

Response hUserDetail(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 userId = 0;
    if (!Protocol::readInt(p, QStringLiteral("userId"), 1, kMaxId, userId))
        return fail(2001, QStringLiteral("invalid userId"));
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kUserSelect)
              + QStringLiteral(" WHERE userId = ? AND deleted = 0"));
    q.addBindValue(userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("user not found"));
    QJsonObject data;
    data.insert(QStringLiteral("user"), Protocol::userJson(q, true));
    return ok(data);
}

Response hUserResetPassword(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 userId = 0;
    if (!Protocol::readInt(p, QStringLiteral("userId"), 1, kMaxId, userId))
        return fail(2001, QStringLiteral("invalid userId"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT deleted FROM users WHERE userId = ?"));
    q.addBindValue(userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next() || q.value(0).toInt() != 0)
        return fail(2002, QStringLiteral("user not found"));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE users SET passwordHash = ? WHERE userId = ?"));
    upd.addBindValue(Protocol::passwordRecord(kInitialPassword));
    upd.addBindValue(userId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("userId"), userId);
    data.insert(QStringLiteral("password"), kInitialPassword);
    return ok(data);
}

Response hUserDelete(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 userId = 0;
    if (!Protocol::readInt(p, QStringLiteral("userId"), 1, kMaxId, userId))
        return fail(2001, QStringLiteral("invalid userId"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT deleted FROM users WHERE userId = ?"));
    q.addBindValue(userId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next() || q.value(0).toInt() != 0)
        return fail(2002, QStringLiteral("user not found"));
    QSqlQuery active(db);
    active.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE userId = ? AND status IN ")
                   + QLatin1String(kUnfinishedOrders));
    active.addBindValue(userId);
    if (!exec(active) || !active.next())
        return fail(5000, QStringLiteral("internal error"));
    if (active.value(0).toLongLong() > 0)
        return fail(3002, QStringLiteral("user has unfinished orders"));
    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE users SET deleted = 1 WHERE userId = ?"));
    upd.addBindValue(userId);
    if (!exec(upd))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("userId"), userId);
    data.insert(QStringLiteral("deleted"), true);
    return ok(data);
}

Response hAdminOrderList(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 page = 1, pageSize = 20;
    if (p.contains(QStringLiteral("page"))
        && !Protocol::readInt(p, QStringLiteral("page"), 1, kMaxId, page))
        return fail(2001, QStringLiteral("invalid page"));
    if (p.contains(QStringLiteral("pageSize"))
        && !Protocol::readInt(p, QStringLiteral("pageSize"), 1, 100, pageSize))
        return fail(2001, QStringLiteral("invalid pageSize"));
    QString keyword;
    if (p.contains(QStringLiteral("phoneKeyword"))
        && !p.value(QStringLiteral("phoneKeyword")).isNull()) {
        if (!p.value(QStringLiteral("phoneKeyword")).isString())
            return fail(2001, QStringLiteral("invalid phoneKeyword"));
        keyword = p.value(QStringLiteral("phoneKeyword")).toString();
        static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
        if (!keyword.isEmpty() && !digits.match(keyword).hasMatch())
            return fail(2001, QStringLiteral("invalid phoneKeyword"));
    }
    QString status;
    if (p.contains(QStringLiteral("status")) && !p.value(QStringLiteral("status")).isNull()) {
        if (!p.value(QStringLiteral("status")).isString()
            || !validOrderStatus(p.value(QStringLiteral("status")).toString()))
            return fail(2001, QStringLiteral("invalid status"));
        status = p.value(QStringLiteral("status")).toString();
    }
    QDate dateFrom, dateTo;
    bool hasFrom = false, hasTo = false;
    if (!readDate(p, QStringLiteral("dateFrom"), dateFrom, hasFrom)
        || !readDate(p, QStringLiteral("dateTo"), dateTo, hasTo))
        return fail(2001, QStringLiteral("invalid date"));
    if (hasFrom && hasTo && dateFrom > dateTo)
        return fail(2001, QStringLiteral("dateFrom must not be after dateTo"));

    QStringList conditions;
    QVariantList binds;
    if (!keyword.isEmpty()) {
        conditions.append(QStringLiteral("u.phone LIKE ?"));
        binds.append(QLatin1Char('%') + keyword + QLatin1Char('%'));
    }
    if (!status.isEmpty()) {
        conditions.append(QStringLiteral("o.status = ?"));
        binds.append(status);
    }
    if (hasFrom) {
        conditions.append(QStringLiteral("o.reservedAt >= ?"));
        binds.append(TimeUtil::dayStartSecs(dateFrom));
    }
    if (hasTo) {
        conditions.append(QStringLiteral("o.reservedAt < ?"));
        binds.append(TimeUtil::dayStartSecs(dateTo.addDays(1)));
    }
    QString where;
    if (!conditions.isEmpty())
        where = QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));

    QSqlQuery count(db);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM orders o"
                                 " JOIN users u ON u.userId = o.userId") + where);
    for (const QVariant &bind : binds)
        count.addBindValue(bind);
    if (!exec(count) || !count.next())
        return fail(5000, QStringLiteral("internal error"));
    const qint64 total = count.value(0).toLongLong();
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kAdminOrderSelect) + where
              + QStringLiteral(" ORDER BY o.reservedAt DESC, o.orderId DESC LIMIT ? OFFSET ?"));
    for (const QVariant &bind : binds)
        q.addBindValue(bind);
    q.addBindValue(pageSize);
    q.addBindValue((page - 1) * pageSize);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray orders;
    while (q.next())
        orders.append(Protocol::adminOrderJson(q));
    QJsonObject data;
    data.insert(QStringLiteral("page"), page);
    data.insert(QStringLiteral("pageSize"), pageSize);
    data.insert(QStringLiteral("total"), total);
    data.insert(QStringLiteral("orders"), orders);
    return ok(data);
}

Response hAdminOrderDetail(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 orderId = 0;
    if (!Protocol::readInt(p, QStringLiteral("orderId"), 1, kMaxId, orderId))
        return fail(2001, QStringLiteral("invalid orderId"));
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kAdminOrderSelect)
              + QStringLiteral(" WHERE o.orderId = ?"));
    q.addBindValue(orderId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("order not found"));
    const QJsonObject order = Protocol::adminOrderJson(q);
    const qint64 userId = q.value(13).toLongLong();
    const qint64 stationId = q.value(1).toLongLong();
    const qint64 pileId = q.value(2).toLongLong();
    QSqlQuery uq(db);
    uq.prepare(QString::fromLatin1(Protocol::kUserSelect)
               + QStringLiteral(" WHERE userId = ?"));
    uq.addBindValue(userId);
    if (!exec(uq) || !uq.next())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery sq(db);
    sq.prepare(QString::fromLatin1(Protocol::kStationAggregateSelect)
               + QStringLiteral(" WHERE s.stationId = ? GROUP BY s.stationId"));
    sq.addBindValue(stationId);
    if (!exec(sq) || !sq.next())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery pq(db);
    pq.prepare(QString::fromLatin1(Protocol::kPileSelect)
               + QStringLiteral(" WHERE p.pileId = ?"));
    pq.addBindValue(pileId);
    if (!exec(pq) || !pq.next())
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("order"), order);
    data.insert(QStringLiteral("user"), Protocol::userJson(uq, false));
    data.insert(QStringLiteral("station"), Protocol::stationSummaryJson(sq));
    data.insert(QStringLiteral("pile"), Protocol::pileJson(pq));
    return ok(data);
}

Response hAdminOrderCancel(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 orderId = 0;
    if (!Protocol::readInt(p, QStringLiteral("orderId"), 1, kMaxId, orderId))
        return fail(2001, QStringLiteral("invalid orderId"));
    QSqlQuery q(db);
    const Response loaded = loadAdminOrder(db, orderId, q);
    if (loaded.code != 0)
        return loaded;
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
    QSqlQuery fresh;
    const Response reloaded = loadAdminOrder(db, orderId, fresh);
    if (reloaded.code != 0)
        return reloaded;
    return adminOrderDataResponse(fresh);
}

Response hAdminOrderStop(const QJsonObject &p, Session &, QSqlDatabase db)
{
    qint64 orderId = 0;
    if (!Protocol::readInt(p, QStringLiteral("orderId"), 1, kMaxId, orderId))
        return fail(2001, QStringLiteral("invalid orderId"));
    QSqlQuery q(db);
    const Response loaded = loadAdminOrder(db, orderId, q);
    if (loaded.code != 0)
        return loaded;
    if (q.value(3).toString() != QLatin1String("charging"))
        return fail(3002, QStringLiteral("order status must be charging"));
    if (!db.transaction())
        return fail(5000, QStringLiteral("internal error"));
    if (!stopChargingInTx(db, orderId, q.value(2).toLongLong(), q.value(6).toLongLong(),
                          q.value(4).toLongLong(), q.value(14).toDouble())) {
        db.rollback();
        return fail(5000, QStringLiteral("internal error"));
    }
    if (!db.commit())
        return fail(5000, QStringLiteral("internal error"));
    QSqlQuery fresh;
    const Response reloaded = loadAdminOrder(db, orderId, fresh);
    if (reloaded.code != 0)
        return reloaded;
    return adminOrderDataResponse(fresh);
}

Response hAdminList(const QJsonObject &, Session &, QSqlDatabase db)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT adminId, username FROM admins ORDER BY adminId"));
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    QJsonArray admins;
    while (q.next()) {
        QJsonObject admin;
        admin.insert(QStringLiteral("adminId"), q.value(0).toLongLong());
        admin.insert(QStringLiteral("username"), q.value(1).toString());
        admins.append(admin);
    }
    QJsonObject data;
    data.insert(QStringLiteral("admins"), admins);
    return ok(data);
}

Response hAdminAdd(const QJsonObject &p, Session &, QSqlDatabase db)
{
    if (!p.value(QStringLiteral("username")).isString())
        return fail(2001, QStringLiteral("invalid username"));
    const QString username = p.value(QStringLiteral("username")).toString().trimmed();
    if (username.isEmpty() || username.length() > 20)
        return fail(2001, QStringLiteral("invalid username"));
    if (!p.value(QStringLiteral("password")).isString()
        || !Protocol::isValidPassword(p.value(QStringLiteral("password")).toString()))
        return fail(2001, QStringLiteral("invalid password"));
    const QString password = p.value(QStringLiteral("password")).toString();
    QSqlQuery dup(db);
    dup.prepare(QStringLiteral("SELECT COUNT(*) FROM admins WHERE username = ?"));
    dup.addBindValue(username);
    if (!exec(dup) || !dup.next())
        return fail(5000, QStringLiteral("internal error"));
    if (dup.value(0).toLongLong() > 0)
        return fail(2001, QStringLiteral("username already exists"));
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO admins (username, passwordHash) VALUES (?, ?)"));
    ins.addBindValue(username);
    ins.addBindValue(Protocol::passwordRecord(password));
    if (!ins.exec()) {
        // 并发插入撞上 username 的 UNIQUE 约束同样按冲突处理。
        if (ins.lastError().text().contains(QLatin1String("UNIQUE")))
            return fail(2001, QStringLiteral("username already exists"));
        qWarning() << "SQL error:" << ins.lastError().text();
        return fail(5000, QStringLiteral("internal error"));
    }
    QJsonObject data;
    data.insert(QStringLiteral("adminId"), ins.lastInsertId().toLongLong());
    data.insert(QStringLiteral("username"), username);
    return ok(data);
}

Response hAdminDelete(const QJsonObject &p, Session &s, QSqlDatabase db)
{
    qint64 adminId = 0;
    if (!Protocol::readInt(p, QStringLiteral("adminId"), 1, kMaxId, adminId))
        return fail(2001, QStringLiteral("invalid adminId"));
    if (adminId == s.adminId)
        return fail(3002, QStringLiteral("cannot delete the logged-in account"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT adminId FROM admins WHERE adminId = ?"));
    q.addBindValue(adminId);
    if (!exec(q))
        return fail(5000, QStringLiteral("internal error"));
    if (!q.next())
        return fail(2002, QStringLiteral("admin not found"));
    QSqlQuery count(db);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM admins"));
    if (!exec(count) || !count.next())
        return fail(5000, QStringLiteral("internal error"));
    if (count.value(0).toLongLong() <= 1)
        return fail(3002, QStringLiteral("cannot delete the last admin"));
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM admins WHERE adminId = ?"));
    del.addBindValue(adminId);
    if (!exec(del))
        return fail(5000, QStringLiteral("internal error"));
    QJsonObject data;
    data.insert(QStringLiteral("adminId"), adminId);
    data.insert(QStringLiteral("deleted"), true);
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
        {QStringLiteral("user_code_request"), {0, hCodeRequest}},
        {QStringLiteral("user_password_reset"), {0, hPasswordReset}},
        {QStringLiteral("admin_login"), {0, hAdminLogin}},
        {QStringLiteral("user_profile_get"), {1, hProfileGet}},
        {QStringLiteral("user_profile_update"), {1, hProfileUpdate}},
        {QStringLiteral("user_password_update"), {1, hUserPasswordUpdate}},
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
        {QStringLiteral("admin_password_update"), {2, hAdminPasswordUpdate}},
        {QStringLiteral("revenue_summary"), {2, hRevenueSummary}},
        {QStringLiteral("revenue_trend"), {2, hRevenueTrend}},
        {QStringLiteral("pile_status_overview"), {2, hPileStatusOverview}},
        {QStringLiteral("pile_list"), {2, hPileList}},
        {QStringLiteral("pile_restart"), {2, hPileRestart}},
        {QStringLiteral("pile_disable"), {2, hPileDisable}},
        {QStringLiteral("pile_active_order"), {2, hPileActiveOrder}},
        {QStringLiteral("pile_add"), {2, hPileAdd}},
        {QStringLiteral("pile_update"), {2, hPileUpdate}},
        {QStringLiteral("pile_delete"), {2, hPileDelete}},
        {QStringLiteral("station_list"), {2, hStationList}},
        {QStringLiteral("station_add"), {2, hStationAdd}},
        {QStringLiteral("station_update"), {2, hStationUpdate}},
        {QStringLiteral("station_delete"), {2, hStationDelete}},
        {QStringLiteral("user_list"), {2, hUserList}},
        {QStringLiteral("user_set_status"), {2, hUserSetStatus}},
        {QStringLiteral("user_add"), {2, hUserAdd}},
        {QStringLiteral("user_update"), {2, hUserUpdate}},
        {QStringLiteral("user_reset_password"), {2, hUserResetPassword}},
        {QStringLiteral("user_delete"), {2, hUserDelete}},
        {QStringLiteral("user_detail"), {2, hUserDetail}},
        {QStringLiteral("admin_order_list"), {2, hAdminOrderList}},
        {QStringLiteral("admin_order_detail"), {2, hAdminOrderDetail}},
        {QStringLiteral("admin_order_cancel"), {2, hAdminOrderCancel}},
        {QStringLiteral("admin_order_stop"), {2, hAdminOrderStop}},
        {QStringLiteral("admin_list"), {2, hAdminList}},
        {QStringLiteral("admin_add"), {2, hAdminAdd}},
        {QStringLiteral("admin_delete"), {2, hAdminDelete}},
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
    const bool isPreLogin = type == QLatin1String("user_login")
        || type == QLatin1String("admin_login")
        || type == QLatin1String("user_code_request")
        || type == QLatin1String("user_password_reset");
    if (isPreLogin) {
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
            q.prepare(QStringLiteral("SELECT status, deleted FROM users WHERE userId = ?"));
            q.addBindValue(session.userId);
            if (!exec(q) || !q.next() || q.value(1).toInt() != 0) {
                closeConnection = true;
                return fail(1005, QStringLiteral("account deleted"));
            }
            if (q.value(0).toString() == QLatin1String("frozen")) {
                closeConnection = true;
                return fail(1002, QStringLiteral("account frozen"));
            }
        }
    }
    return def.handler(payload, session, db);
}
