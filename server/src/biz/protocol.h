#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QJsonObject>
#include <QJsonValue>
#include <QSqlQuery>

// 业务处理结果由状态码、提示和数据组成；没有数据时返回空值。
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

// 密码为6至20位非空白字符，数据库保存盐值与摘要。
bool isValidPassword(const QString &password);
QString passwordRecord(const QString &password);
bool verifyPassword(const QString &record, const QString &password);

QJsonObject userJson(const QSqlQuery &q, bool withAvatar);
QJsonObject stationSummaryJson(const QSqlQuery &q);
QJsonObject pileJson(const QSqlQuery &q);
QJsonObject orderJson(const QSqlQuery &q);
// 订单基础字段后追加电桩功率和用户手机号。
QJsonObject adminOrderJson(const QSqlQuery &q);

extern const char *const kUserSelect;
extern const char *const kStationAggregateSelect;
extern const char *const kPileSelect;
extern const char *const kOrderSelect;
extern const char *const kAdminOrderSelect;

} // namespace Protocol

#endif // PROTOCOL_H
