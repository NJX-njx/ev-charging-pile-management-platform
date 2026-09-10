#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QPushButton;
class QStackedWidget;
class QWebEngineView;

// 一键导航：在应用内展示腾讯路线规划页，支持驾车和步行。
class NavigationDialog : public QDialog
{
    Q_OBJECT
public:
    NavigationDialog(const QString &stationName, double fromLng, double fromLat,
                     const QString &fromDescription, double toLng, double toLat,
                     QWidget *parent = nullptr);

private:
    // 构造腾讯地图路线规划 URI（type=drive|walk）。
    static QUrl buildRouteUrl(const QString &type, double fromLng, double fromLat,
                              double toLng, double toLat, const QString &stationName,
                              const QString &fromDescription);

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
