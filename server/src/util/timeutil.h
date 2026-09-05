#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <QDate>
#include <QString>
#include <QTimeZone>

namespace TimeUtil {

const QTimeZone &chinaZone();
qint64 nowSecs();
QString isoFromSecs(qint64 secs);
QDate todayChina();
qint64 dayStartSecs(const QDate &date);
qint64 monthStartSecs(const QDate &anyDay);

} // namespace TimeUtil

#endif // TIMEUTIL_H
