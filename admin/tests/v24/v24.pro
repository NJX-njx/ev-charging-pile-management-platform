# admin 模块 v2.4 协议验证 harness（offscreen）：链接真实 admin 源码，
# 直连 v2.4 真实服务端（默认 127.0.0.1:8888），每场景自建站点/用户/订单数据后
# 驱动 UI 断言并截图：电桩占用区分显示与占用桩重启/禁用、用户资料编辑（头像/余额）、
# 订单干预（取消预约/停止充电）。
# 不属于交付模块，仅用于开发验证；用法见 run_scenarios.sh。

QT += core gui widgets network testlib

qtHaveModule(charts): QT += charts

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = v24_harness
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
