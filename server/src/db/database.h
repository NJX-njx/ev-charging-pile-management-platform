#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QString>

namespace Database {

void configure(const QString &path);
bool initialize(QString *errorMessage);
// Opens (once) and returns the named connection; each thread must use its own name.
QSqlDatabase connection(const QString &name);
// Closes and removes the named connection; call from its owning thread after last use.
void remove(const QString &name);

} // namespace Database

#endif // DATABASE_H
