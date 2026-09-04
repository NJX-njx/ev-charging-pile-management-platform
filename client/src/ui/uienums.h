#pragma once

#include <QLabel>
#include <QString>
#include <QStyle>
#include <QWidget>

namespace ui {

inline QString pileStatusText(const QString &status)
{
    if (status == QLatin1String("idle")) return QStringLiteral("空闲");
    if (status == QLatin1String("in_use")) return QStringLiteral("在用");
    if (status == QLatin1String("fault")) return QStringLiteral("故障");
    return QStringLiteral("未知");
}

inline QString pileTypeText(const QString &type)
{
    if (type == QLatin1String("fast")) return QStringLiteral("快充");
    if (type == QLatin1String("slow")) return QStringLiteral("慢充");
    return QStringLiteral("未知");
}

inline QString orderStatusText(const QString &status)
{
    if (status == QLatin1String("reserved")) return QStringLiteral("已预约");
    if (status == QLatin1String("charging")) return QStringLiteral("充电中");
    if (status == QLatin1String("pending_payment")) return QStringLiteral("待结算");
    if (status == QLatin1String("completed")) return QStringLiteral("已完成");
    if (status == QLatin1String("cancelled")) return QStringLiteral("已取消");
    return QStringLiteral("未知");
}

inline QString userStatusText(const QString &status)
{
    if (status == QLatin1String("normal")) return QStringLiteral("正常");
    if (status == QLatin1String("frozen")) return QStringLiteral("冻结");
    return QStringLiteral("未知");
}

inline void setState(QLabel *label, const QString &state)
{
    label->setProperty("state", state);
    label->setProperty("kind", QStringLiteral("status"));
    label->style()->unpolish(label);
    label->style()->polish(label);
}

inline void restyle(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace ui
