#pragma once

#include <QList>
#include <QMainWindow>

#include "model/appconfig.h"
#include "model/models.h"

class QLabel;
class QPushButton;
class QStackedWidget;
class SocketClient;
class LoginPage;
class FindStationPage;
class ChargingPage;
class MyPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const AppConfig &config, QWidget *parent = nullptr);

private:
    void switchTab(int index);
    void showBanner(const QString &text);
    void hideBanner();

    AppConfig m_config;
    SocketClient *m_client;
    QStackedWidget *m_stack;
    LoginPage *m_loginPage;
    QWidget *m_mainPage;
    FindStationPage *m_findPage;
    ChargingPage *m_chargingPage;
    MyPage *m_myPage;
    QLabel *m_banner;
    QList<QPushButton *> m_tabButtons;
    QStackedWidget *m_tabStack;
};
