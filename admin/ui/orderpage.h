#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class FilterTable;
class SocketClient;

class OrderPage : public QWidget
{
    Q_OBJECT

public:
    explicit OrderPage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private:
    void loadOrders();
    void updatePagination();
    int totalPages() const;
    // 订单行「详情」操作列控件：loadOrders 与 FilterTable 排序后重建共用
    QWidget *createOrderOps(int row);
    void showDetail(int orderId);

    SocketClient *m_client;
    QLineEdit *m_phoneEdit;
    QComboBox *m_statusBox;
    QCheckBox *m_dateCheck;
    QDateEdit *m_dateFrom;
    QDateEdit *m_dateTo;
    QTableWidget *m_table;
    FilterTable *m_ft;
    QPushButton *m_prevBtn;
    QPushButton *m_nextBtn;
    QLabel *m_pageLabel;
    int m_page = 1;
    int m_total = 0;
    const int m_pageSize = 20;
};
