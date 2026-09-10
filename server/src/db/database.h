#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QString>

namespace Database {

void configure(const QString &path);
bool initialize(QString *errorMessage);
// 按名称打开并复用数据库连接；每个线程使用独立名称。
QSqlDatabase connection(const QString &name);
// 最后一次使用后，由所属线程关闭并移除数据库连接。
void remove(const QString &name);

} // namespace Database

#endif // DATABASE_H
