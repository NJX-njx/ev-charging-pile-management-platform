#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QJsonObject>
#include <QJsonValue>
#include <QSqlQuery>

// code/msg/data triplet returned by every business handler; invalid data means null.
struct Response {
    int code = 0;
    QString msg;
    QJsonValue data;
};

namespace Protocol {

struct Envelope {
    bool ok = false;
    qint64 seq = -1;
    QString type = QStringLiteral("error");
    QJsonObject payload;
};

Envelope parseEnvelope(const QByteArray &line);
QByteArray buildResponse(qint64 seq, const QString &type, const Response &response);

bool readInt(const QJsonObject &obj, const QString &key, qint64 min, qint64 max, qint64 &out);
bool readMoneyFen(const QJsonObject &obj, const QString &key, qint64 maxFen, qint64 &outFen);
bool readLngLat(const QJsonObject &obj, double &lng, double &lat);

QJsonObject userJson(const QSqlQuery &q, bool withAvatar);
QJsonObject stationSummaryJson(const QSqlQuery &q);
QJsonObject pileJson(const QSqlQuery &q);
QJsonObject orderJson(const QSqlQuery &q);

extern const char *const kUserSelect;
extern const char *const kStationAggregateSelect;
extern const char *const kPileSelect;
extern const char *const kOrderSelect;

} // namespace Protocol

#endif // PROTOCOL_H
