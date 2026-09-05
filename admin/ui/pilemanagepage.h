#pragma once

#include <QWidget>

#include <functional>

class QJsonArray;
class QPushButton;
class QTableWidget;
class SocketClient;

class PileManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit PileManagePage(SocketClient *client, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onRestartClicked();
    void onAddPile();
    void onEditPile();
    void onDeletePile();

private:
    int selectedRow() const;
    void updateActionButtons();
    void loadAllStations(const std::function<void(bool, const QJsonArray &)> &done);

    SocketClient *m_client;
    QTableWidget *m_table;
    QPushButton *m_addBtn;
    QPushButton *m_editBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_restartBtn;
};
