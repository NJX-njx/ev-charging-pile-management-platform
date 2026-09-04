#pragma once

#include <QColor>
#include <QString>

// 协议枚举（英文）与界面显示（中文）之间的唯一映射处，见 docs/protocol.md 第 2 节
namespace UiEnums {

inline QString pileStatusText(const QString &status)
{
    if (status == QStringLiteral("idle"))
        return QStringLiteral("空闲");
    if (status == QStringLiteral("in_use"))
        return QStringLiteral("在用");
    if (status == QStringLiteral("fault"))
        return QStringLiteral("故障");
    return status;
}

inline QColor pileStatusColor(const QString &status)
{
    if (status == QStringLiteral("idle"))
        return QColor(46, 125, 50);
    if (status == QStringLiteral("in_use"))
        return QColor(21, 101, 192);
    if (status == QStringLiteral("fault"))
        return QColor(198, 40, 40);
    return QColor(0, 0, 0);
}

inline QString pileTypeText(const QString &type)
{
    if (type == QStringLiteral("fast"))
        return QStringLiteral("快充");
    if (type == QStringLiteral("slow"))
        return QStringLiteral("慢充");
    return type;
}

inline QString userStatusText(const QString &status)
{
    if (status == QStringLiteral("frozen"))
        return QStringLiteral("冻结");
    if (status == QStringLiteral("normal"))
        return QStringLiteral("正常");
    return status;
}

inline QColor userStatusColor(const QString &status)
{
    if (status == QStringLiteral("frozen"))
        return QColor(198, 40, 40);
    return QColor(46, 125, 50);
}

} // namespace UiEnums
