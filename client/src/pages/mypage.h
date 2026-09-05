#pragma once

#include <QWidget>

#include "model/models.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class SocketClient;

class MyPage : public QWidget
{
    Q_OBJECT
public:
    explicit MyPage(SocketClient *client, QWidget *parent = nullptr);

    void setUser(const UserInfo &user);
    void refreshProfile();
    void reloadOrders();
    void openRechargeDialog();

signals:
    void logoutRequested();
    void balanceChanged(double balance);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void onChangeAvatar();
    void onSaveNickname();
    void onLoadMoreOrders();
    void renderProfile();
    void appendOrders(const QList<Order> &orders);
    void resetOrders();

    SocketClient *m_client;
    UserInfo m_user;
    bool m_hasUser = false;

    QLabel *m_avatarLabel;
    QLineEdit *m_nicknameEdit;
    QPushButton *m_saveNicknameButton;
    QPushButton *m_avatarButton;
    QPushButton *m_passwordButton;
    QLabel *m_phoneLabel;

    QLabel *m_balanceLabel;
    QPushButton *m_rechargeButton;

    QWidget *m_ordersContainer;
    QVBoxLayout *m_ordersLayout;
    QPushButton *m_loadMoreButton;
    QLabel *m_ordersHint;
    int m_nextPage = 1;
    bool m_loadingOrders = false;

    QPushButton *m_logoutButton;
};
