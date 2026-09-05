#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QWebEngineView;

// 一键导航（需求矩阵 NO.11）：QWebEngineView 加载腾讯地图 URI API 路线规划页，
// 支持驾车/步行切换；起点为最近一次地址解析或手动输入的坐标。
// 未编译 WebEngine 模块时 isAvailable() 为 false，由调用方降级为提示。
class NavigationDialog : public QDialog
{
    Q_OBJECT
public:
    NavigationDialog(const QString &stationName, double fromLng, double fromLat,
                     double toLng, double toLat, QWidget *parent = nullptr);

    static bool isAvailable();

private:
    void loadRoute(const QString &type);

    QString m_stationName;
    double m_fromLng;
    double m_fromLat;
    double m_toLng;
    double m_toLat;

    QWebEngineView *m_view = nullptr;
    QPushButton *m_driveButton;
    QPushButton *m_walkButton;
};
