#include "timeutil.h"

#include <QDateTime>

namespace TimeUtil {

const QTimeZone &chinaZone()
{
    static const QTimeZone zone("Asia/Shanghai");
    return zone;
}

qint64 nowSecs()
{
    return QDateTime::currentSecsSinceEpoch();
}

QString isoFromSecs(qint64 secs)
{
    return QDateTime::fromSecsSinceEpoch(secs, chinaZone()).toString(Qt::ISODate);
}

QDate todayChina()
{
    return QDateTime::currentDateTimeUtc().toTimeZone(chinaZone()).date();
}

qint64 dayStartSecs(const QDate &date)
{
    return QDateTime(date, QTime(0, 0, 0), chinaZone()).toSecsSinceEpoch();
}

qint64 monthStartSecs(const QDate &anyDay)
{
    return dayStartSecs(QDate(anyDay.year(), anyDay.month(), 1));
}

} // namespace TimeUtil
