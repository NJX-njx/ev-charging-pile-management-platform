#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>

// 协议 v2.5（7.8 节）站点条目预校验：导入文件与新增站点对话框共用。
// 合法数据规整后（code 去空白、type 中文映射为英文枚举）原样透传 station_add；
// 非法时 error 给出中文原因。type 接受英文枚举 fast/slow 或中文 快充/慢充。
namespace StationImport {

// 校验电桩清单：1 至 100 条，每条 {code, type, powerKw}
inline bool validatePiles(const QJsonValue &pilesValue, QJsonArray *pilesOut, QString *error)
{
    const QJsonArray piles = pilesValue.isArray() ? pilesValue.toArray() : QJsonArray();
    if (!pilesValue.isArray() || piles.isEmpty() || piles.size() > 100) {
        *error = QStringLiteral("piles 必须为 1 至 100 条的数组");
        return false;
    }
    QJsonArray out;
    QStringList codes;
    for (int i = 0; i < piles.size(); ++i) {
        const QString label = QStringLiteral("第 %1 个电桩").arg(i + 1);
        if (!piles.at(i).isObject()) {
            *error = QStringLiteral("%1：不是 JSON 对象").arg(label);
            return false;
        }
        const QJsonObject pile = piles.at(i).toObject();
        const QJsonValue codeValue = pile[QStringLiteral("code")];
        const QString code = codeValue.isString() ? codeValue.toString().trimmed() : QString();
        if (code.isEmpty() || code.size() > 20) {
            *error = QStringLiteral("%1：code 必须为 1 至 20 字符的字符串").arg(label);
            return false;
        }
        if (codes.contains(code)) {
            *error = QStringLiteral("电桩编号 %1 重复").arg(code);
            return false;
        }
        codes << code;

        const QJsonValue typeValue = pile[QStringLiteral("type")];
        const QString typeText = typeValue.isString() ? typeValue.toString().trimmed() : QString();
        QString type;
        if (typeText == QStringLiteral("fast") || typeText == QStringLiteral("快充"))
            type = QStringLiteral("fast");
        else if (typeText == QStringLiteral("slow") || typeText == QStringLiteral("慢充"))
            type = QStringLiteral("slow");
        else {
            *error = QStringLiteral("%1：type 必须为 fast/slow（或 快充/慢充）").arg(label);
            return false;
        }

        const QJsonValue powerValue = pile[QStringLiteral("powerKw")];
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double power = powerValue.toDouble(nan);
        if (!powerValue.isDouble() || std::isnan(power) || power <= 0.0) {
            *error = QStringLiteral("%1：powerKw 必须为大于 0 的数字").arg(label);
            return false;
        }

        out.append(QJsonObject{{QStringLiteral("code"), code},
                               {QStringLiteral("type"), type},
                               {QStringLiteral("powerKw"), power}});
    }
    *pilesOut = out;
    return true;
}

// 校验单个站点条目（导入文件数组元素），成功时 payload 为可直接发送的 station_add 请求体
inline bool validateStationEntry(const QJsonObject &obj, QJsonObject *payload, QString *error)
{
    const QString name = obj[QStringLiteral("name")].toString().trimmed();
    const QString address = obj[QStringLiteral("address")].toString().trimmed();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double lng = obj[QStringLiteral("lng")].toDouble(nan);
    const double lat = obj[QStringLiteral("lat")].toDouble(nan);
    const double price = obj[QStringLiteral("pricePerKwh")].toDouble(nan);

    if (name.isEmpty() || address.isEmpty()) {
        *error = QStringLiteral("name/address 不能为空");
        return false;
    }
    if (std::isnan(lng) || lng < -180.0 || lng > 180.0 || std::isnan(lat) || lat < -90.0
        || lat > 90.0) {
        *error = QStringLiteral("lng/lat 非法或超出范围");
        return false;
    }
    if (std::isnan(price) || price <= 0.0) {
        *error = QStringLiteral("pricePerKwh 必须大于 0");
        return false;
    }

    QJsonArray piles;
    if (!validatePiles(obj[QStringLiteral("piles")], &piles, error))
        return false;

    *payload = QJsonObject{{QStringLiteral("name"), name},
                           {QStringLiteral("address"), address},
                           {QStringLiteral("lng"), lng},
                           {QStringLiteral("lat"), lat},
                           {QStringLiteral("pricePerKwh"), price},
                           {QStringLiteral("piles"), piles}};
    return true;
}

} // namespace StationImport
