#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
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
    void onImportStations();
    void onShowPiles();
    // 从 JSON 文件批量导入站点（格式同 tools/seed_stations.json），供导入按钮与自动化测试直接调用
    void importStationsFromFile(const QString &path);

private:
    void loadStations();
    void updatePagination();
    int totalPages() const;
    int selectedStationId() const;
    void onEditStation(const QJsonObject &station, QPushButton *button);
    void onDeleteStation(const QJsonObject &station, QPushButton *button);

    SocketClient *m_client;
    QLineEdit *m_searchEdit;
    QCheckBox *m_showDeletedCheck;
    QTableWidget *m_table;
    QPushButton *m_addBtn;
    QPushButton *m_importBtn;
    QPushButton *m_prevBtn;
    QPushButton *m_nextBtn;
    QLabel *m_pageLabel;
    int m_page = 1;
    int m_total = 0;
    const int m_pageSize = 20;
    bool m_importing = false;
};
