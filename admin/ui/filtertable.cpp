// Excel 式表格排序与筛选助手。
//
// 用法（页面表格 setColumnCount + setHorizontalHeaderLabels 之后）：
//     m_ft = new FilterTable(m_table, this);
//     m_ft->setExcludedColumns({9});              // 操作列不参与排序/筛选
//     m_ft->setScopeNote(QStringLiteral("..."));   // 可选，附加到表头 tooltip
// 之后每次向表格填完数据调用一次 m_ft->apply()（恢复排序/筛选状态），
// 选中行用 m_ft->rowToSelect(previousId) 计算（跳过被筛选隐藏的行）。
//
// 交互：点击表头列名在 升序→降序→不排序 间切换（不排序还原数据装入顺序）；
// 点击表头右侧漏斗图标弹出该列去重值多选清单，取消勾选的值对应行被隐藏，
// 筛选与排序可叠加。服务端分页的页面筛选/排序只作用于当前页已装入的数据，
// 应通过 setScopeNote 在表头 tooltip 注明。

#include "filtertable.h"

#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace {

// 色板（docs/visual-design.md）：主色 #00A870、辅助 #9CA3AF
const QColor kPrimary(0x00, 0xA8, 0x70);
const QColor kFunnelIdle(0x9C, 0xA3, 0xAF);

// 装入顺序标记（「不排序」状态据此还原），页面只用 UserRole/UserRole+1，避开
const int kOrderRole = Qt::UserRole + 100;

// 数值感知比较：支持可选「%」后缀（如在线率），其余按本地化文本比较
bool valueLess(const QString &sa, const QString &sb)
{
    auto asNumber = [](const QString &text, double &value) {
        QString t = text.trimmed();
        if (t.endsWith(QLatin1Char('%')))
            t.chop(1);
        bool ok = false;
        value = t.toDouble(&ok);
        return ok;
    };
    double da = 0.0, db = 0.0;
    if (asNumber(sa, da) && asNumber(sb, db))
        return da < db;
    return QString::localeAwareCompare(sa, sb) < 0;
}

bool itemLess(const QTableWidgetItem *a, const QTableWidgetItem *b)
{
    return valueLess(a ? a->text() : QString(), b ? b->text() : QString());
}

} // namespace

// ---------------- FilterHeaderView ----------------

FilterHeaderView::FilterHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setMouseTracking(true);
}

void FilterHeaderView::setSortState(int column, int order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    viewport()->update();
}

void FilterHeaderView::setColumnFiltered(int column, bool filtered)
{
    if (filtered)
        m_filteredColumns.insert(column);
    else
        m_filteredColumns.remove(column);
    viewport()->update();
}

void FilterHeaderView::setColumnExcluded(int column, bool excluded)
{
    if (excluded)
        m_excludedColumns.insert(column);
    else
        m_excludedColumns.remove(column);
    viewport()->update();
}

QRect FilterHeaderView::filterIconRect(const QRect &sectionRect) const
{
    return QRect(sectionRect.right() - 18, sectionRect.center().y() - 8, 16, 16);
}

QRect FilterHeaderView::sortArrowRect(const QRect &sectionRect) const
{
    return QRect(sectionRect.right() - 30, sectionRect.center().y() - 4, 9, 9);
}

bool FilterHeaderView::nearSectionBoundary(const QPoint &pos) const
{
    // 节区分界线附近是列宽拖拽热区，不触发排序
    for (int i = 0; i < count(); ++i) {
        const int x = sectionViewportPosition(i);
        if (qAbs(pos.x() - x) <= 4 || qAbs(pos.x() - (x + sectionSize(i))) <= 4)
            return true;
    }
    return false;
}

void FilterHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    QHeaderView::paintSection(painter, rect, logicalIndex);
    if (logicalIndex < 0 || m_excludedColumns.contains(logicalIndex))
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);

    // 排序箭头（升序▲/降序▼，主色）
    if (logicalIndex == m_sortColumn && m_sortOrder != 0) {
        const QRect r = sortArrowRect(rect);
        QPolygonF tri;
        const qreal cx = r.center().x();
        if (m_sortOrder == 1)
            tri << QPointF(cx, r.top()) << QPointF(r.right(), r.bottom())
                << QPointF(r.left(), r.bottom());
        else
            tri << QPointF(cx, r.bottom()) << QPointF(r.right(), r.top())
                << QPointF(r.left(), r.top());
        painter->setBrush(kPrimary);
        painter->drawPolygon(tri);
    }

    // 筛选漏斗：生效列主色填充，未生效列灰色填充
    const QRect fr = filterIconRect(rect);
    painter->setBrush(m_filteredColumns.contains(logicalIndex) ? kPrimary : kFunnelIdle);
    const qreal l = fr.left() + 2, r = fr.right() - 2, t = fr.top() + 3, b = fr.bottom() - 3;
    const qreal midY = t + (b - t) * 0.52;
    const qreal cx = fr.center().x();
    QPainterPath path;
    path.moveTo(l, t);
    path.lineTo(r, t);
    path.lineTo(cx + 2.2, midY);
    path.lineTo(cx + 2.2, b);
    path.lineTo(cx - 2.2, b - 2.5);
    path.lineTo(cx - 2.2, midY);
    path.closeSubpath();
    painter->drawPath(path);

    painter->restore();
}

void FilterHeaderView::mousePressEvent(QMouseEvent *event)
{
    m_pressedSection = -1;
    m_pressedOnIcon = false;
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        const int section = logicalIndexAt(event->pos());
        if (section >= 0 && !m_excludedColumns.contains(section)) {
            const QRect sr(sectionViewportPosition(section), 0, sectionSize(section), height());
            m_pressedSection = section;
            m_pressedOnIcon = filterIconRect(sr).contains(event->pos());
            if (m_pressedOnIcon)
                return; // 不交给基类，避免被当作列宽拖拽或节区按下
        }
    }
    QHeaderView::mousePressEvent(event);
}

void FilterHeaderView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressedSection >= 0) {
        const int section = m_pressedSection;
        const bool onIcon = m_pressedOnIcon;
        m_pressedSection = -1;
        m_pressedOnIcon = false;
        const QRect sr(sectionViewportPosition(section), 0, sectionSize(section), height());
        const bool moved = (event->pos() - m_pressPos).manhattanLength() > 6;
        if (!moved && sr.contains(event->pos())) {
            if (onIcon && filterIconRect(sr).contains(event->pos()))
                emit filterRequested(section, mapToGlobal(filterIconRect(sr).bottomLeft()));
            else if (!onIcon && !nearSectionBoundary(m_pressPos))
                emit sortRequested(section);
        }
    }
    QHeaderView::mouseReleaseEvent(event);
}

void FilterHeaderView::mouseMoveEvent(QMouseEvent *event)
{
    bool onIcon = false;
    const int section = logicalIndexAt(event->pos());
    if (section >= 0 && !m_excludedColumns.contains(section)) {
        const QRect sr(sectionViewportPosition(section), 0, sectionSize(section), height());
        onIcon = filterIconRect(sr).contains(event->pos());
    }
    setCursor(onIcon ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QHeaderView::mouseMoveEvent(event);
}

// ---------------- FilterTable ----------------

FilterTable::FilterTable(QTableWidget *table, QObject *parent)
    : QObject(parent), m_table(table)
{
    m_header = new FilterHeaderView(Qt::Horizontal, m_table);
    m_table->setHorizontalHeader(m_header);
    connect(m_header, &FilterHeaderView::sortRequested, this, &FilterTable::onSortRequested);
    connect(m_header, &FilterHeaderView::filterRequested, this, &FilterTable::onFilterRequested);
}

void FilterTable::setExcludedColumns(const QList<int> &columns)
{
    m_excludedColumns.clear();
    for (const int c : columns)
        m_excludedColumns.insert(c);
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_header->setColumnExcluded(c, m_excludedColumns.contains(c));
}

void FilterTable::setCellWidgetFactory(int column, const std::function<QWidget *(int row)> &factory)
{
    m_cellWidgetFactories[column] = factory;
}

void FilterTable::setScopeNote(const QString &note)
{
    m_scopeNote = note;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        QTableWidgetItem *h = m_table->horizontalHeaderItem(c);
        if (!h || m_excludedColumns.contains(c))
            continue;
        QString tip = QStringLiteral("「%1」点击列名切换升/降/不排序；点击右侧漏斗按值筛选").arg(h->text());
        if (!m_scopeNote.isEmpty())
            tip += QLatin1Char('\n') + m_scopeNote;
        h->setToolTip(tip);
    }
}

bool FilterTable::hasActiveFilters() const
{
    for (auto it = m_deniedValues.constBegin(); it != m_deniedValues.constEnd(); ++it)
        if (!it.value().isEmpty())
            return true;
    return false;
}

int FilterTable::visibleRowCount() const
{
    int n = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (!m_table->isRowHidden(r))
            ++n;
    return n;
}

void FilterTable::clearFilters()
{
    m_deniedValues.clear();
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_header->setColumnFiltered(c, false);
    const int id = selectedId();
    apply();
    const int row = rowToSelect(id);
    if (row >= 0)
        m_table->selectRow(row);
}

void FilterTable::apply()
{
    m_table->clearSelection();
    m_table->setUpdatesEnabled(false);
    // 首次排序前记录装入顺序，供「不排序」状态还原；数据重载后新行按装入顺序补标
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *it = m_table->item(r, 0);
        if (it && it->data(kOrderRole).isNull())
            it->setData(kOrderRole, r);
    }
    if (m_sortColumn >= 0 && m_sortOrder != 0)
        sortRows();
    applyRowVisibility();
    m_table->setUpdatesEnabled(true);
    m_header->viewport()->update();
}

int FilterTable::rowToSelect(int previousId) const
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *it = m_table->item(r, 0);
        if (it && !m_table->isRowHidden(r) && it->data(Qt::UserRole).toInt() == previousId)
            return r;
    }
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (!m_table->isRowHidden(r))
            return r;
    return -1;
}

int FilterTable::selectedId() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return -1;
    QTableWidgetItem *it = m_table->item(items.first()->row(), 0);
    return it ? it->data(Qt::UserRole).toInt() : -1;
}

void FilterTable::onSortRequested(int column)
{
    if (m_sortColumn != column) {
        m_sortColumn = column;
        m_sortOrder = 1;
    } else {
        m_sortOrder = (m_sortOrder + 1) % 3;
        if (m_sortOrder == 0)
            m_sortColumn = -1;
    }
    m_header->setSortState(m_sortColumn, m_sortOrder);

    // 切回「不排序」：按装入顺序还原
    if (m_sortColumn < 0) {
        const int id = selectedId();
        m_table->clearSelection();
        m_table->setUpdatesEnabled(false);
        reorderRows([](const QTableWidgetItem *a, const QTableWidgetItem *b) {
            const QVariant va = a ? a->data(kOrderRole) : QVariant();
            const QVariant vb = b ? b->data(kOrderRole) : QVariant();
            if (va.isNull() || vb.isNull())
                return va.isNull() && !vb.isNull();
            return va.toInt() < vb.toInt();
        });
        applyRowVisibility();
        m_table->setUpdatesEnabled(true);
        m_header->viewport()->update();
        const int row = rowToSelect(id);
        if (row >= 0)
            m_table->selectRow(row);
        return;
    }

    const int id = selectedId();
    apply();
    const int row = rowToSelect(id);
    if (row >= 0)
        m_table->selectRow(row);
}

void FilterTable::sortRows()
{
    reorderRows([this](const QTableWidgetItem *a, const QTableWidgetItem *b) {
        if (m_sortOrder == 1)
            return itemLess(a, b);
        return itemLess(b, a);
    });
}

void FilterTable::reorderRows(const std::function<bool(const QTableWidgetItem *, const QTableWidgetItem *)> &less)
{
    const int rows = m_table->rowCount();
    const int cols = m_table->columnCount();
    if (rows <= 1)
        return;

    // 含单元格控件的列先移除控件：Qt 的 setIndexWidget 对旧控件是 deleteLater
    // 语义，控件对象无法跨行搬运，只能移除（随之销毁）后由工厂重建
    const QList<int> factoryColumns = m_cellWidgetFactories.keys();
    for (const int c : factoryColumns)
        for (int r = 0; r < rows; ++r)
            if (m_table->cellWidget(r, c))
                m_table->removeCellWidget(r, c);

    std::vector<QList<QTableWidgetItem *>> data(rows);
    for (auto &items : data)
        items.resize(cols);
    // 列主序、自底向上取 item：某行最后一个 item 被取走时该行塌缩，
    // 只会影响下方已取过的行号，上方未取的行号不受影响
    for (int c = 0; c < cols; ++c)
        for (int r = rows - 1; r >= 0; --r)
            data[r][c] = m_table->takeItem(r, c);

    const int keyColumn = m_sortColumn >= 0 ? m_sortColumn : 0;
    std::stable_sort(data.begin(), data.end(), [&](const QList<QTableWidgetItem *> &a,
                                                   const QList<QTableWidgetItem *> &b) {
        return less(a[keyColumn], b[keyColumn]);
    });

    // 行可能已全部塌缩，恢复行数后回插
    m_table->setRowCount(rows);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (data[r][c])
                m_table->setItem(r, c, data[r][c]);

    // 重建单元格控件（工厂按当前行的 item data 生成新控件与连接）
    for (const int c : factoryColumns) {
        const auto &factory = m_cellWidgetFactories[c];
        for (int r = 0; r < rows; ++r)
            if (QWidget *w = factory(r))
                m_table->setCellWidget(r, c, w);
    }
}

void FilterTable::applyRowVisibility()
{
    const int rows = m_table->rowCount();
    const int cols = m_table->columnCount();
    for (int r = 0; r < rows; ++r) {
        bool visible = true;
        for (auto it = m_deniedValues.constBegin(); it != m_deniedValues.constEnd() && visible; ++it) {
            if (it.value().isEmpty() || it.key() >= cols)
                continue;
            QTableWidgetItem *item = m_table->item(r, it.key());
            if (it.value().contains(item ? item->text() : QString()))
                visible = false;
        }
        m_table->setRowHidden(r, !visible);
    }
}

QStringList FilterTable::distinctValues(int column) const
{
    QSet<QString> seen;
    QStringList values;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *item = m_table->item(r, column);
        const QString text = item ? item->text() : QString();
        if (!seen.contains(text)) {
            seen.insert(text);
            values << text;
        }
    }
    std::sort(values.begin(), values.end(), valueLess);
    return values;
}

void FilterTable::onFilterRequested(int column, const QPoint &globalPos)
{
    // 非模态弹出（Qt::Popup：点击弹窗外自动关闭），堆分配 + WA_DeleteOnClose
    QDialog *dialog = new QDialog(m_table, Qt::Popup | Qt::FramelessWindowHint);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("filterPopup"));
    QVBoxLayout *layout = new QVBoxLayout(dialog);

    QCheckBox *allCheck = new QCheckBox(QStringLiteral("（全选）"), dialog);
    layout->addWidget(allCheck);

    QListWidget *list = new QListWidget(dialog);
    list->setObjectName(QStringLiteral("filterValueList"));
    const QStringList values = distinctValues(column);
    const QSet<QString> denied = m_deniedValues.value(column);
    for (const QString &v : values) {
        QListWidgetItem *item = new QListWidgetItem(v.isEmpty() ? QStringLiteral("（空）") : v, list);
        item->setData(Qt::UserRole, v);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(denied.contains(v) ? Qt::Unchecked : Qt::Checked);
        list->addItem(item);
    }
    list->setMinimumWidth(180);
    layout->addWidget(list);

    QHBoxLayout *buttons = new QHBoxLayout;
    QPushButton *okBtn = new QPushButton(QStringLiteral("确定"), dialog);
    okBtn->setObjectName(QStringLiteral("filterOk"));
    okBtn->setProperty("primary", true);
    QPushButton *clearBtn = new QPushButton(QStringLiteral("清除筛选"), dialog);
    clearBtn->setObjectName(QStringLiteral("filterClear"));
    buttons->addWidget(okBtn);
    buttons->addWidget(clearBtn);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto syncAllCheck = [allCheck, list]() {
        int checked = 0;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked)
                ++checked;
        QSignalBlocker blocker(allCheck);
        allCheck->setCheckState(checked == 0 ? Qt::Unchecked
                                : checked == list->count() ? Qt::Checked
                                                           : Qt::PartiallyChecked);
    };
    allCheck->setTristate(false);
    syncAllCheck();
    connect(allCheck, &QCheckBox::clicked, dialog, [list](bool checked) {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    });
    connect(list, &QListWidget::itemChanged, dialog, [syncAllCheck]() { syncAllCheck(); });

    auto applyChecks = [this, column, list]() {
        QSet<QString> deniedNow;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            if (item->checkState() != Qt::Checked)
                deniedNow.insert(item->data(Qt::UserRole).toString());
        }
        if (deniedNow.isEmpty())
            m_deniedValues.remove(column);
        else
            m_deniedValues[column] = deniedNow;
        m_header->setColumnFiltered(column, !deniedNow.isEmpty());
        const int id = selectedId();
        apply();
        const int row = rowToSelect(id);
        if (row >= 0)
            m_table->selectRow(row);
    };
    connect(okBtn, &QPushButton::clicked, dialog, [dialog, applyChecks]() {
        applyChecks();
        dialog->accept();
    });
    connect(clearBtn, &QPushButton::clicked, dialog, [this, column, dialog]() {
        m_deniedValues.remove(column);
        m_header->setColumnFiltered(column, false);
        const int id = selectedId();
        apply();
        const int row = rowToSelect(id);
        if (row >= 0)
            m_table->selectRow(row);
        dialog->accept();
    });

    dialog->move(globalPos);
    dialog->show();
}
