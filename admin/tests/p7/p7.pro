# admin 模块 p7 改版验证 harness（offscreen）：链接真实 admin 源码 +
# tools/mock_server_v23.py 状态化假服务端，脚本化驱动 UI 断言：
# 导航 6 项可见/窗口尺寸、站点与电桩合并页左右联动与全部操作、
# 全列表 Excel 式筛选排序、系统页管理员操作。
# 不属于交付模块，仅用于开发验证；用法见 run_scenarios.sh。

QT += core gui widgets network testlib

qtHaveModule(charts): QT += charts

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = p7_harness
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
    $$SRC/ui/pilestatuspage.cpp \
    $$SRC/ui/salespage.cpp \
    $$SRC/ui/stationpilepage.cpp \
    $$SRC/ui/systempage.cpp \
    $$SRC/ui/userpage.cpp

HEADERS += \
    $$SRC/net/socketclient.h \
    $$SRC/ui/filtertable.h \
    $$SRC/ui/loginwindow.h \
    $$SRC/ui/mainwindow.h \
    $$SRC/ui/orderpage.h \
    $$SRC/ui/pilestatuspage.h \
    $$SRC/ui/salespage.h \
    $$SRC/ui/stationpilepage.h \
    $$SRC/ui/systempage.h \
    $$SRC/ui/uienums.h \
    $$SRC/ui/userpage.h

FORMS += \
    $$SRC/ui/loginwindow.ui \
    $$SRC/ui/mainwindow.ui

RESOURCES += \
    $$SRC/resources/resources.qrc
