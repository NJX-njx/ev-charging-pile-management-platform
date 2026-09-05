#pragma once

#include <QJsonObject>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QJsonArray;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class SocketClient;
class FilterTable;

// 站点与电桩管理合并页：顶部电桩状态总览条（空闲/在用/故障/总计徽标），
// 左侧站点列表（搜索/分页/行内修改删除/显示已删除/导入/新增），
// 右侧电桩列表（新增/修改/删除/重启/禁用/占用详情/显示已删除）：
// 默认联动左侧选中站点，可切「全部站点」查看全部电桩（显示所属站列）。
// 选中站点变化时右侧联动刷新。由原 StationPage 与 PileManagePage 合并而来，
// 电桩状态页撤除后其状态总览并入本页顶部。
class StationPilePage : public QWidget
{
    Q_OBJECT

public:
    explicit StationPilePage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onAddStation();
    void onImportStations();
    // 从 JSON 文件批量导入站点（格式同 tools/seed_stations.json），供导入按钮与自动化测试直接调用
    void importStationsFromFile(const QString &path);
    void onAddPile();
    void onEditPile();
    void onDeletePile();
    void onRestartClicked();
    void onDisableClicked();
    void onShowActiveOrder();

private:
    void loadStations();
    // 刷新右侧电桩列表；force=true 时即使站点范围未变化也重新拉取
    void loadPiles(bool force = false);
    // 顶部状态总览条：并发请求 pile_status_overview，刷新空闲/在用/故障/总计徽标
    void loadStatusOverview();
    // 左侧选中站点变化时同步右栏站点范围下拉（index 1 = 当前联动站点）
    void syncStationScopeCombo();
    void updatePagination();
    int totalPages() const;
    int selectedStationId() const;
    bool selectedStationDeleted() const;
    // 站点行「修改」「删除」操作列控件：loadStations 与 FilterTable 排序后重建共用
    QWidget *createStationOps(int row);
    void onEditStation(const QJsonObject &station, QPushButton *button);
    void onDeleteStation(const QJsonObject &station, QPushButton *button);
    void updatePileActionButtons();
    int selectedPileRow() const;
    void loadAllStations(const std::function<void(bool, const QJsonArray &)> &done);

    SocketClient *m_client;

    // 顶部：电桩状态总览徽标
    QLabel *m_badgeIdle;
    QLabel *m_badgeInUse;
    QLabel *m_badgeFault;
    QLabel *m_badgeTotal;

    // 左侧：站点
    QLineEdit *m_searchEdit;
    QCheckBox *m_showDeletedStationsCheck;
    QTableWidget *m_stationTable;
    FilterTable *m_stationFt;
    QPushButton *m_addStationBtn;
    QPushButton *m_importBtn;
    QPushButton *m_prevBtn;
    QPushButton *m_nextBtn;
    QLabel *m_pageLabel;
    int m_page = 1;
    int m_total = 0;
    const int m_pageSize = 20;
    bool m_importing = false;

    // 右侧：站点范围（全部站点 / 当前联动站点）内的电桩
    QLabel *m_currentStationLabel;
    QComboBox *m_stationScopeCombo;
    QCheckBox *m_showDeletedPilesCheck;
    QTableWidget *m_pileTable;
    FilterTable *m_pileFt;
    QPushButton *m_addPileBtn;
    QPushButton *m_editPileBtn;
    QPushButton *m_deletePileBtn;
    QPushButton *m_restartBtn;
    QPushButton *m_disableBtn;
    QPushButton *m_activeOrderBtn;
    // 已加载的站点范围：0=全部站点，>0=站点ID，-1=无选中站点
    int m_loadedScopeId = -2;
    bool m_stationDeleted = false;
    // loadStations 期间屏蔽选中变化触发的 loadPiles，由 loadStations 统一触发一次
    bool m_loadingStations = false;
};
