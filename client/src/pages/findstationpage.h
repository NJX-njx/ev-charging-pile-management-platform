#pragma once

#include <QWidget>

#include "model/models.h"

class QDialog;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QLabel;
class SocketClient;
class MapBridge;

class FindStationPage : public QWidget
{
    Q_OBJECT
public:
    explicit FindStationPage(SocketClient *client, QWidget *parent = nullptr);

signals:
    void orderStateDirty();

private:
    void onGeocodeClicked();
    void onSearchClicked();
    void onRefreshClicked();
    void searchNearby(double lng, double lat);
    void showStationDetail(const Station &station);
    void reservePile(qint64 pileId, const QString &pileCode, QDialog *dialog);
    void renderStations(const QList<Station> &stations);
    void setBusy(bool busy);

    SocketClient *m_client;
    MapBridge *m_map = nullptr;
    QLineEdit *m_addressEdit;
    QLineEdit *m_lngEdit;
    QLineEdit *m_latEdit;
    QPushButton *m_geocodeButton;
    QPushButton *m_searchButton;
    QPushButton *m_refreshButton;
    QWidget *m_listContainer;
    QVBoxLayout *m_listLayout;
    QLabel *m_emptyHint;
    bool m_hasLastCoord = false;
    double m_lastLng = 0.0;
    double m_lastLat = 0.0;
};
