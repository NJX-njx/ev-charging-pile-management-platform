#include "database.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

QString g_dbPath;

// 终态结构（协议 v2.2）：只在空库上一次性建表，不做任何旧结构的就地迁移。
// 已存在的库文件必须先通过 schemaCompatible() 自检才会继续使用。
const char *const kSchema[] = {
    "CREATE TABLE IF NOT EXISTS admins ("
    " adminId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " username TEXT NOT NULL UNIQUE,"
    " passwordHash TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS users ("
    " userId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " phone TEXT NOT NULL,"
    " nickname TEXT NOT NULL,"
    " balanceFen INTEGER NOT NULL DEFAULT 0,"
    " status TEXT NOT NULL DEFAULT 'normal',"
    " avatarMime TEXT,"
    " avatarBase64 TEXT,"
    " passwordHash TEXT,"
    " deleted INTEGER NOT NULL DEFAULT 0,"
    " regTime INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS stations ("
    " stationId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " name TEXT NOT NULL,"
    " address TEXT NOT NULL,"
    " lng REAL NOT NULL,"
    " lat REAL NOT NULL,"
    " priceFenPerKwh INTEGER NOT NULL,"
    " deleted INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS piles ("
    " pileId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " code TEXT NOT NULL UNIQUE,"
    " stationId INTEGER NOT NULL REFERENCES stations(stationId),"
    " type TEXT NOT NULL,"
    " powerKw REAL NOT NULL,"
    " status TEXT NOT NULL DEFAULT 'idle',"
    " chargeCount INTEGER NOT NULL DEFAULT 0,"
    " chargeMinutes INTEGER NOT NULL DEFAULT 0,"
    " deleted INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS orders ("
    " orderId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " userId INTEGER NOT NULL REFERENCES users(userId),"
    " stationId INTEGER NOT NULL REFERENCES stations(stationId),"
    " pileId INTEGER NOT NULL REFERENCES piles(pileId),"
    " status TEXT NOT NULL,"
    " unitPriceFen INTEGER NOT NULL,"
    " reservedAt INTEGER NOT NULL,"
    " startTime INTEGER,"
    " endTime INTEGER,"
    " settledAt INTEGER,"
    " energyWh INTEGER,"
    " amountFen INTEGER)",
    "CREATE TABLE IF NOT EXISTS counters ("
    " key TEXT PRIMARY KEY,"
    " value INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS codes ("
    " phone TEXT PRIMARY KEY,"
    " code TEXT NOT NULL,"
    " expiresAtEpoch INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_orders_user_status ON orders(userId, status)",
    "CREATE INDEX IF NOT EXISTS idx_orders_settled ON orders(status, settledAt)",
    "CREATE INDEX IF NOT EXISTS idx_orders_reserved ON orders(reservedAt)",
    "CREATE INDEX IF NOT EXISTS idx_piles_station ON piles(stationId)",
    // 手机号唯一性只在未删除用户间成立，已删除用户的手机号可以重新注册。
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_users_phone_active"
    " ON users(phone) WHERE deleted = 0",
};

bool applyPragmas(QSqlDatabase &db, QString *errorMessage)
{
    QSqlQuery q(db);
    static const char *const pragmas[] = {
        "PRAGMA journal_mode=WAL",
        "PRAGMA busy_timeout=5000",
        "PRAGMA foreign_keys=ON",
    };
    for (const char *pragma : pragmas) {
        if (!q.exec(QLatin1String(pragma))) {
            if (errorMessage)
                *errorMessage = q.lastError().text();
            return false;
        }
    }
    return true;
}

bool databaseIsEmpty(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master"
                               " WHERE type = 'table' AND name NOT LIKE 'sqlite_%'")))
        return false;
    return q.next() && q.value(0).toLongLong() == 0;
}

bool tableHasColumns(QSqlDatabase &db, const char *table,
                     std::initializer_list<const char *> required)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(QLatin1String(table))))
        return false;
    QSet<QString> columns;
    while (q.next())
        columns.insert(q.value(1).toString());
    if (columns.isEmpty())
        return false;
    for (const char *column : required) {
        if (!columns.contains(QLatin1String(column)))
            return false;
    }
    return true;
}

// 只接受终态结构：关键表/列齐全；users.phone 上没有旧的列级 UNIQUE（会表现为
// sqlite_autoindex，并挡住已删除手机号的重新注册）；部分唯一索引已就位。
bool schemaCompatible(QSqlDatabase &db)
{
    if (!tableHasColumns(db, "admins", {"adminId", "username", "passwordHash"})
        || !tableHasColumns(db, "users", {"userId", "phone", "nickname", "balanceFen",
                                          "status", "avatarMime", "avatarBase64",
                                          "passwordHash", "deleted", "regTime"})
        || !tableHasColumns(db, "stations", {"stationId", "name", "address", "lng", "lat",
                                             "priceFenPerKwh", "deleted"})
        || !tableHasColumns(db, "piles", {"pileId", "code", "stationId", "type", "powerKw",
                                          "status", "chargeCount", "chargeMinutes", "deleted"})
        || !tableHasColumns(db, "orders", {"orderId", "userId", "stationId", "pileId",
                                           "status", "unitPriceFen", "reservedAt",
                                           "startTime", "endTime", "settledAt",
                                           "energyWh", "amountFen"})
        || !tableHasColumns(db, "counters", {"key", "value"})
        || !tableHasColumns(db, "codes", {"phone", "code", "expiresAtEpoch"}))
        return false;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'index'"
                               " AND tbl_name = 'users' AND name LIKE 'sqlite_autoindex%'")))
        return false;
    if (q.next())
        return false;
    if (!q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'index'"
                               " AND name = 'idx_users_phone_active'")))
        return false;
    return q.next();
}

} // namespace

void Database::configure(const QString &path)
{
    g_dbPath = path;
}

QSqlDatabase Database::connection(const QString &name)
{
    if (QSqlDatabase::contains(name)) {
        QSqlDatabase db = QSqlDatabase::database(name);
        if (db.isOpen() || db.open())
            return db;
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(g_dbPath);
    if (!db.open()) {
        qWarning() << "failed to open database:" << db.lastError().text();
        return db;
    }
    applyPragmas(db, nullptr);
    return db;
}

void Database::remove(const QString &name)
{
    if (QSqlDatabase::contains(name))
        QSqlDatabase::removeDatabase(name);
}

bool Database::initialize(QString *errorMessage)
{
    // 先以裸连接打开：自检只做只读查询，被拒绝的旧库文件保持原样
    //（journal_mode=WAL 本身就会改写文件头，不能先上 pragma）。
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("main"));
    db.setDatabaseName(g_dbPath);
    if (!db.open()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }
    const bool empty = databaseIsEmpty(db);
    if (!empty && !schemaCompatible(db)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("数据库结构与当前版本不兼容，"
                                           "请删除旧测试数据库文件后重启: %1").arg(g_dbPath);
        }
        return false;
    }
    if (!applyPragmas(db, errorMessage))
        return false;
    if (empty) {
        QSqlQuery q(db);
        for (const char *stmt : kSchema) {
            if (!q.exec(QLatin1String(stmt))) {
                if (errorMessage)
                    *errorMessage = q.lastError().text();
                return false;
            }
        }
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM admins")) || !q.next()) {
        if (errorMessage)
            *errorMessage = q.lastError().text();
        return false;
    }
    if (q.value(0).toLongLong() > 0)
        return true;
    QByteArray salt(16, Qt::Uninitialized);
    for (int i = 0; i < salt.size(); ++i)
        salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    const QByteArray saltHex = salt.toHex();
    const QByteArray hash = QCryptographicHash::hash(saltHex + QByteArrayLiteral("123456"),
                                                     QCryptographicHash::Sha256).toHex();
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO admins (username, passwordHash) VALUES (?, ?)"));
    ins.addBindValue(QStringLiteral("admin"));
    ins.addBindValue(QString::fromLatin1(saltHex) + QLatin1Char(':') + QString::fromLatin1(hash));
    if (!ins.exec()) {
        if (errorMessage)
            *errorMessage = ins.lastError().text();
        return false;
    }
    return true;
}
