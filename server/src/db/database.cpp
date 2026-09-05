#include "database.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

QString g_dbPath;

const char *const kSchema[] = {
    "CREATE TABLE IF NOT EXISTS admins ("
    " adminId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " username TEXT NOT NULL UNIQUE,"
    " passwordHash TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS users ("
    " userId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " phone TEXT NOT NULL UNIQUE,"
    " nickname TEXT NOT NULL,"
    " balanceFen INTEGER NOT NULL DEFAULT 0,"
    " status TEXT NOT NULL DEFAULT 'normal',"
    " avatarMime TEXT,"
    " avatarBase64 TEXT,"
    " regTime INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS stations ("
    " stationId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " name TEXT NOT NULL,"
    " address TEXT NOT NULL,"
    " lng REAL NOT NULL,"
    " lat REAL NOT NULL,"
    " priceFenPerKwh INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS piles ("
    " pileId INTEGER PRIMARY KEY AUTOINCREMENT,"
    " code TEXT NOT NULL UNIQUE,"
    " stationId INTEGER NOT NULL REFERENCES stations(stationId),"
    " type TEXT NOT NULL,"
    " powerKw REAL NOT NULL,"
    " status TEXT NOT NULL DEFAULT 'idle',"
    " chargeCount INTEGER NOT NULL DEFAULT 0,"
    " chargeMinutes INTEGER NOT NULL DEFAULT 0)",
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
    "CREATE INDEX IF NOT EXISTS idx_orders_user_status ON orders(userId, status)",
    "CREATE INDEX IF NOT EXISTS idx_orders_settled ON orders(status, settledAt)",
    "CREATE INDEX IF NOT EXISTS idx_piles_station ON piles(stationId)",
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
    QSqlDatabase db = connection(QStringLiteral("main"));
    if (!db.isOpen()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    for (const char *stmt : kSchema) {
        if (!q.exec(QLatin1String(stmt))) {
            if (errorMessage)
                *errorMessage = q.lastError().text();
            return false;
        }
    }
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
