#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QPushButton;
class QStackedWidget;
class QWebEngineView;

// 一键导航（需求矩阵 NO.11，对照说明书「点击导航按钮跳转至地图路线规划页面」）：
// 对话框先展示起点（当前定位坐标，可附来源地址）与终点（站点名+坐标）及
// 驾车/步行方式选择；点击「导航」按钮后才创建 QWebEngineView 并加载腾讯地图
// URI API 路线规划页（fromcoord/tocoord 均为显式 lat,lng 坐标，referer 用已配置
// Key）。已加载后切换出行方式会立即重新规划。未编译 WebEngine 或未配置 Key 时
// isAvailable() 为 false，由调用方降级为提示。
class NavigationDialog : public QDialog
{
    Q_OBJECT
public:
    NavigationDialog(const QString &stationName, double fromLng, double fromLat,
                     const QString &fromDescription, double toLng, double toLat,
                     QWidget *parent = nullptr);

    static bool isAvailable();
    // 构造腾讯地图路线规划 URI（type=drive|walk）；独立出来供自动化验证断言参数
    static QUrl buildRouteUrl(const QString &type, double fromLng, double fromLat,
                              double toLng, double toLat, const QString &stationName,
                              const QString &fromDescription);

private:
    void loadRoute();

    QString m_stationName;
    QString m_fromDescription;
    double m_fromLng;
    double m_fromLat;
    double m_toLng;
    double m_toLat;
    QString m_mode;
    bool m_loaded = false;

    QStackedWidget *m_stack;
    QWebEngineView *m_view = nullptr;
    QPushButton *m_driveButton;
    QPushButton *m_walkButton;
    QPushButton *m_navButton;
};
