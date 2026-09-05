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

// 逻辑删除记录（协议 3.4）：deleted=true 的行仅用于历史数据查看
inline QString recordStatusText(bool deleted)
{
    return deleted ? QStringLiteral("已删除") : QStringLiteral("正常");
}

inline QColor recordStatusColor(bool deleted)
{
    // 已删除用色板「文本-次」#6B7280，正常用语义色「成功/正常」#2E7D32
    return deleted ? QColor(107, 114, 128) : QColor(46, 125, 50);
}

inline QString orderStatusText(const QString &status)
{
    if (status == QStringLiteral("reserved"))
        return QStringLiteral("已预约");
    if (status == QStringLiteral("charging"))
        return QStringLiteral("充电中");
    if (status == QStringLiteral("pending_payment"))
        return QStringLiteral("待结算");
    if (status == QStringLiteral("completed"))
        return QStringLiteral("已完成");
    if (status == QStringLiteral("cancelled"))
        return QStringLiteral("已取消");
    return status;
}

inline QColor orderStatusColor(const QString &status)
{
    if (status == QStringLiteral("reserved"))
        return QColor(21, 101, 192);
    if (status == QStringLiteral("charging"))
        return QColor(21, 101, 192);
    if (status == QStringLiteral("pending_payment"))
        return QColor(237, 108, 2);
    if (status == QStringLiteral("completed"))
        return QColor(46, 125, 50);
    if (status == QStringLiteral("cancelled"))
        return QColor(107, 114, 128);
    return QColor(0, 0, 0);
}

} // namespace UiEnums
