# admin 模块 v2.5 协议验证 harness（offscreen）：链接真实 admin 源码。
# validate 场景离线断言 StationImport 预校验；addstation/importflow 场景连
# ../mock_server_v25.py 假服务端，驱动新增站点对话框与导入流程并截图。
# 不属于交付模块，仅用于开发验证；用法见 run_scenarios.sh。

QT += core gui widgets network testlib

qtHaveModule(charts): QT += charts

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = v25_harness
TEMPLATE = app

INCLUDEPATH += $$PWD/../..

SRC = $$PWD/../..

SOURCES += \
    main.cpp \
    $$SRC/net/socketclient.cpp \
    $$SRC/ui/filtertable.cpp \
    $$SRC/ui/loginwindow.cpp \
    $$SRC/ui/mainwindow.cpp \
    $$SRC/ui/orderpage.cpp \
    $$SRC/ui/salespage.cpp \
    $$SRC/ui/stationpilepage.cpp \
    $$SRC/ui/systempage.cpp \
    $$SRC/ui/usereditdialog.cpp \
    $$SRC/ui/userpage.cpp

HEADERS += \
    $$SRC/net/socketclient.h \
    $$SRC/ui/filtertable.h \
    $$SRC/ui/loginwindow.h \
    $$SRC/ui/mainwindow.h \
    $$SRC/ui/orderpage.h \
    $$SRC/ui/salespage.h \
    $$SRC/ui/stationimport.h \
    $$SRC/ui/stationpilepage.h \
    $$SRC/ui/systempage.h \
    $$SRC/ui/uienums.h \
    $$SRC/ui/usereditdialog.h \
    $$SRC/ui/userpage.h

FORMS += \
    $$SRC/ui/loginwindow.ui \
    $$SRC/ui/mainwindow.ui

RESOURCES += \
    $$SRC/resources/resources.qrc
