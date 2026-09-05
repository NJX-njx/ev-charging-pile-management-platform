#ifndef STATS_H
#define STATS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>

// Aggregations shared by the admin TCP messages and the read-only HTTP API.
namespace Stats {

QJsonObject revenueSummary(const QSqlDatabase &db);
QJsonObject revenueTrend(const QSqlDatabase &db, int range);
QJsonObject pileStatusOverview(const QSqlDatabase &db);
QJsonArray stationSummaries(const QSqlDatabase &db);
QJsonObject orderCounts(const QSqlDatabase &db);
QJsonObject energySums(const QSqlDatabase &db);

} // namespace Stats

#endif // STATS_H
