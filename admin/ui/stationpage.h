#pragma once

#include <QWidget>

class QTableWidget;
class SocketClient;

class StationPage : public QWidget
{
    Q_OBJECT

public:
    explicit StationPage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onAddStation();
    void onShowPiles();

private:
    int selectedStationId() const;

    SocketClient *m_client;
    QTableWidget *m_table;
};
