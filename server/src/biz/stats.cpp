#include "stats.h"

#include "protocol.h"
#include "timeutil.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

qint64 scalarSum(const QSqlDatabase &db, const QString &column,
                 qint64 fromInclusive, qint64 toExclusive, bool unbounded)
{
    QSqlQuery q(db);
    QString sql = QStringLiteral("SELECT COALESCE(SUM(%1), 0) FROM orders WHERE status = 'completed'").arg(column);
    if (!unbounded)
        sql += QStringLiteral(" AND settledAt >= ? AND settledAt < ?");
    q.prepare(sql);
    if (!unbounded) {
        q.addBindValue(fromInclusive);
        q.addBindValue(toExclusive);
    }
    if (!q.exec() || !q.next()) {
        qWarning() << "stats query failed:" << q.lastError().text();
        return 0;
    }
    return q.value(0).toLongLong();
}

qint64 scalarCount(const QSqlDatabase &db, qint64 fromInclusive, qint64 toExclusive, bool unbounded)
{
    QSqlQuery q(db);
    QString sql = QStringLiteral("SELECT COUNT(*) FROM orders WHERE status = 'completed'");
    if (!unbounded)
        sql += QStringLiteral(" AND settledAt >= ? AND settledAt < ?");
    q.prepare(sql);
    if (!unbounded) {
        q.addBindValue(fromInclusive);
        q.addBindValue(toExclusive);
    }
    if (!q.exec() || !q.next()) {
        qWarning() << "stats query failed:" << q.lastError().text();
        return 0;
    }
    return q.value(0).toLongLong();
}

} // namespace

QJsonObject Stats::revenueSummary(const QSqlDatabase &db)
{
    const QDate today = TimeUtil::todayChina();
    const qint64 dayStart = TimeUtil::dayStartSecs(today);
    const qint64 dayEnd = TimeUtil::dayStartSecs(today.addDays(1));
    const qint64 monthStart = TimeUtil::monthStartSecs(today);
    const qint64 monthEnd = TimeUtil::monthStartSecs(today.addMonths(1));
    QJsonObject obj;
    obj.insert(QStringLiteral("today"),
               scalarSum(db, QStringLiteral("amountFen"), dayStart, dayEnd, false) / 100.0);
    obj.insert(QStringLiteral("month"),
               scalarSum(db, QStringLiteral("amountFen"), monthStart, monthEnd, false) / 100.0);
    obj.insert(QStringLiteral("total"),
               scalarSum(db, QStringLiteral("amountFen"), 0, 0, true) / 100.0);
    return obj;
}

QJsonObject Stats::revenueTrend(const QSqlDatabase &db, int range)
{
    const QDate today = TimeUtil::todayChina();
    const QDate first = today.addDays(-(range - 1));
    const qint64 startSecs = TimeUtil::dayStartSecs(first);
    const qint64 endSecs = TimeUtil::dayStartSecs(today.addDays(1));
    QVector<qint64> sums(range, 0);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT settledAt, amountFen FROM orders"
                             " WHERE status = 'completed' AND settledAt >= ? AND settledAt < ?"));
    q.addBindValue(startSecs);
    q.addBindValue(endSecs);
    if (q.exec()) {
        while (q.next()) {
            const qint64 index = (q.value(0).toLongLong() - startSecs) / 86400;
            if (index >= 0 && index < range)
                sums[static_cast<int>(index)] += q.value(1).toLongLong();
        }
    } else {
        qWarning() << "stats query failed:" << q.lastError().text();
    }
    QJsonArray points;
    for (int i = 0; i < range; ++i) {
        QJsonObject point;
        point.insert(QStringLiteral("date"), first.addDays(i).toString(QStringLiteral("yyyy-MM-dd")));
        point.insert(QStringLiteral("amount"), sums[i] / 100.0);
        points.append(point);
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("range"), range);
    obj.insert(QStringLiteral("points"), points);
    return obj;
}

QJsonObject Stats::pileStatusOverview(const QSqlDatabase &db)
{
    qint64 idle = 0, inUse = 0, fault = 0;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT status, COUNT(*) FROM piles WHERE deleted = 0 GROUP BY status"));
    if (q.exec()) {
        while (q.next()) {
            const QString status = q.value(0).toString();
            const qint64 count = q.value(1).toLongLong();
            if (status == QLatin1String("idle"))
                idle = count;
            else if (status == QLatin1String("in_use"))
                inUse = count;
            else if (status == QLatin1String("fault"))
                fault = count;
        }
    } else {
        qWarning() << "stats query failed:" << q.lastError().text();
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("total"), idle + inUse + fault);
    obj.insert(QStringLiteral("idle"), idle);
    obj.insert(QStringLiteral("inUse"), inUse);
    obj.insert(QStringLiteral("fault"), fault);
    return obj;
}

QJsonArray Stats::stationSummaries(const QSqlDatabase &db)
{
    QJsonArray out;
    QSqlQuery q(db);
    q.prepare(QString::fromLatin1(Protocol::kStationAggregateSelect)
              + QStringLiteral(" WHERE s.deleted = 0 GROUP BY s.stationId ORDER BY s.stationId"));
    if (q.exec()) {
        while (q.next())
            out.append(Protocol::stationSummaryJson(q));
    } else {
        qWarning() << "stats query failed:" << q.lastError().text();
    }
    return out;
}

QJsonObject Stats::orderCounts(const QSqlDatabase &db)
{
    const QDate today = TimeUtil::todayChina();
    const qint64 dayStart = TimeUtil::dayStartSecs(today);
    const qint64 dayEnd = TimeUtil::dayStartSecs(today.addDays(1));
    QJsonObject obj;
    obj.insert(QStringLiteral("today"), scalarCount(db, dayStart, dayEnd, false));
    obj.insert(QStringLiteral("total"), scalarCount(db, 0, 0, true));
    return obj;
}

QJsonObject Stats::energySums(const QSqlDatabase &db)
{
    const QDate today = TimeUtil::todayChina();
    const qint64 dayStart = TimeUtil::dayStartSecs(today);
    const qint64 dayEnd = TimeUtil::dayStartSecs(today.addDays(1));
    QJsonObject obj;
    obj.insert(QStringLiteral("todayKwh"),
               scalarSum(db, QStringLiteral("energyWh"), dayStart, dayEnd, false) / 1000.0);
    obj.insert(QStringLiteral("totalKwh"),
               scalarSum(db, QStringLiteral("energyWh"), 0, 0, true) / 1000.0);
    return obj;
}
