QT += core gui widgets network

# QtCharts 在 Ubuntu 22.04 仅 Qt5 可用；Qt6 环境下自动跳过（营收趋势页开发时需要）
qtHaveModule(charts): QT += charts

CONFIG += c++17

TARGET = AdminClient
TEMPLATE = app

SOURCES += \
    main.cpp \
    net/socketclient.cpp \
    ui/loginwindow.cpp \
    ui/mainwindow.cpp \
    ui/salespage.cpp

HEADERS += \
    net/socketclient.h \
    ui/loginwindow.h \
    ui/mainwindow.h \
    ui/salespage.h

FORMS += \
    ui/loginwindow.ui \
    ui/mainwindow.ui
