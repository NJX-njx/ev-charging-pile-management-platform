#pragma once

#include <QJsonObject>
#include <QString>

struct UserInfo {
    qint64 userId = 0;
    QString phone;
    QString nickname;
    double balance = 0.0;
    QString regTime;
    QString status;
    bool hasPassword = false;
    bool hasAvatar = false;
    QString avatarMime;
    QByteArray avatarBytes;

    static UserInfo fromJson(const QJsonObject &o);
};

struct Station {
    qint64 stationId = 0;
    QString name;
    QString address;
    double lng = 0.0;
    double lat = 0.0;
    double pricePerKwh = 0.0;
    int pileTotal = 0;
    int pileIdle = 0;
    double onlineRate = 0.0;
    double distanceKm = -1.0;

    static Station fromJson(const QJsonObject &o);
};

struct Pile {
    qint64 pileId = 0;
    QString code;
    qint64 stationId = 0;
    QString stationName;
    QString type;
    double powerKw = 0.0;
    QString status;
    int chargeCount = 0;
    int chargeMinutes = 0;

    static Pile fromJson(const QJsonObject &o);
};

struct Order {
    qint64 orderId = 0;
    qint64 stationId = 0;
    QString stationName;
    qint64 pileId = 0;
    QString pileCode;
    QString status;
    QString reservedAt;
    QString startTime;
    QString endTime;
    QString settledAt;
    bool hasEnergy = false;
    double energyKwh = 0.0;
    double unitPrice = 0.0;
    bool hasAmount = false;
    double amount = 0.0;

    static Order fromJson(const QJsonObject &o);
    QDateTime startDateTime() const;
};

QString formatDateTime(const QString &iso);
