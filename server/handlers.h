#ifndef HANDLERS_H
#define HANDLERS_H

#include "protocol.h"

#include <QSqlDatabase>

struct Session {
    enum Role { None, User, Admin };
    Role role = None;
    qint64 userId = 0;
    qint64 adminId = 0;
};

namespace Handlers {

Response dispatch(const QString &type, const QJsonObject &payload, Session &session,
                  QSqlDatabase db, bool &closeConnection);

} // namespace Handlers

#endif // HANDLERS_H
