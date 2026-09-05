#pragma once

#include <QWidget>

#include "model/models.h"

class QComboBox;
class QDialog;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QVBoxLayout;
class QLabel;
class SocketClient;
class MapBridge;

class FindStationPage : public QWidget
{
    Q_OBJECT
public:
    explicit FindStationPage(SocketClient *client, QWidget *parent = nullptr);

    // 本地缓存的用户余额（元）；<0 表示未知（未拉取过资料），此时交由服务端 3004 兜底
    void setKnownBalance(double balance);

signals:
    void orderStateDirty();
    void requestRecharge();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void onRegionSelected(int index);
    void onGeocodeClicked();
    void onSearchClicked();
    void onRefreshClicked();
    void onNavigateToStation(const Station &station);
    void searchNearby(double lng, double lat);
    void showStationDetail(const Station &station);
    void reservePile(qint64 pileId, const QString &pileCode, QDialog *dialog);
    void renderStations(const QList<Station> &stations);
    void setBusy(bool busy);

    SocketClient *m_client;
    MapBridge *m_map = nullptr;
    QComboBox *m_regionCombo;
    QLineEdit *m_addressEdit;
    QLineEdit *m_lngEdit;
    QLineEdit *m_latEdit;
    QPushButton *m_geocodeButton;
    QPushButton *m_searchButton;
    QPushButton *m_refreshButton;
    QWidget *m_listContainer;
    QVBoxLayout *m_listLayout;
    QLabel *m_emptyHint;
    bool m_defaultSearched = false; // 「全部区域（默认）」首次进入时已自动查询
    bool m_hasLastCoord = false;
    double m_lastLng = 0.0;
    double m_lastLat = 0.0;
    QString m_lastLocationDesc; // 当前定位来源（区域/地址/手动坐标），导航起点展示用
    double m_knownBalance = -1.0;
};
