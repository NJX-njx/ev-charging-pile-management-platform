#pragma once

#include <QHash>
#include <QHeaderView>
#include <QObject>
#include <QSet>

#include <functional>

class QTableWidget;
class QTableWidgetItem;

// 表头视图：在常规绘制基础上叠加排序箭头与筛选漏斗图标，
// 点击漏斗区域发出 filterRequested，点击节区其余位置发出 sortRequested
// （靠近节区分界线的按下交给基类处理，保证列宽拖拽不受影响）。
class FilterHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    explicit FilterHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

    // 排序状态（0=无 1=升序 2=降序）与各列是否有生效筛选，仅影响图标绘制
    void setSortState(int column, int order);
    void setColumnFiltered(int column, bool filtered);
    void setColumnExcluded(int column, bool excluded);

signals:
    void sortRequested(int logicalIndex);
    void filterRequested(int logicalIndex, const QPoint &globalPos);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QRect filterIconRect(const QRect &sectionRect) const;
    QRect sortArrowRect(const QRect &sectionRect) const;
    bool nearSectionBoundary(const QPoint &pos) const;

    int m_sortColumn = -1;
    int m_sortOrder = 0;
    QSet<int> m_filteredColumns;
    QSet<int> m_excludedColumns;
    int m_pressedSection = -1;
    bool m_pressedOnIcon = false;
    QPoint m_pressPos;
};

// Excel 式表格排序与筛选助手（用法见 filtertable.cpp 顶部注释）：
// 点击表头列名在 升序→降序→不排序 间切换；点击表头右侧漏斗图标弹出该列
// 去重值多选清单，勾选后仅显示匹配行，筛选与排序可叠加。
class FilterTable : public QObject
{
    Q_OBJECT

public:
    explicit FilterTable(QTableWidget *table, QObject *parent = nullptr);

    // 操作列等不参与排序/筛选的列
    void setExcludedColumns(const QList<int> &columns);
    // 含单元格控件的列必须注册工厂：Qt 的 setIndexWidget 对旧控件是 deleteLater
    // 语义，控件对象无法跨行搬运；排序时旧控件被移除销毁，排序完成后逐行调用
    // 工厂重建（工厂可从该行 item 的 data 取回行数据，重建按钮与信号连接）
    void setCellWidgetFactory(int column, const std::function<QWidget *(int row)> &factory);
    // 表头 tooltip 附加的作用范围说明（服务端分页页面应注明「仅作用于当前页数据」）
    void setScopeNote(const QString &note);

    // 数据重载后调用：对当前表内容重新应用排序与筛选（会清空选中，调用方用
    // rowToSelect 自行恢复；表头交互触发的重排由组件内部自动恢复选中）
    void apply();
    void clearFilters();

    // 数据重载后的选中恢复：返回应选中的行（优先 previousId 所在且可见的行，
    // 否则第一可见行，均不可见返回 -1）；id 取自第 0 列 UserRole
    int rowToSelect(int previousId) const;

    bool hasActiveFilters() const;
    int visibleRowCount() const;

private:
    void onSortRequested(int column);
    void onFilterRequested(int column, const QPoint &globalPos);
    void sortRows();
    // 整行搬运（含单元格控件），less 比较给定排序列的两个 item
    void reorderRows(const std::function<bool(const QTableWidgetItem *, const QTableWidgetItem *)> &less);
    void applyRowVisibility();
    QStringList distinctValues(int column) const;
    int selectedId() const;

    QTableWidget *m_table;
    FilterHeaderView *m_header;
    int m_sortColumn = -1;
    int m_sortOrder = 0; // 0=无 1=升序 2=降序
    // 每列被排除（未勾选）的显示值集合；空集合表示该列无筛选
    QHash<int, QSet<QString>> m_deniedValues;
    QSet<int> m_excludedColumns;
    QHash<int, std::function<QWidget *(int row)>> m_cellWidgetFactories;
    QString m_scopeNote;
};
