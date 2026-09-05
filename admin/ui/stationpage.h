#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
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
    void loadStations();
    void updatePagination();
    int totalPages() const;
    int selectedStationId() const;
    void onEditStation(const QJsonObject &station, QPushButton *button);
    void onDeleteStation(const QJsonObject &station, QPushButton *button);

    SocketClient *m_client;
    QLineEdit *m_searchEdit;
    QTableWidget *m_table;
    QPushButton *m_addBtn;
    QPushButton *m_prevBtn;
    QPushButton *m_nextBtn;
    QLabel *m_pageLabel;
    int m_page = 1;
    int m_total = 0;
    const int m_pageSize = 20;
};
