# 协议 v2.2 适配验证 harness（offscreen）：链接真实 client 源码 + 状态化假服务端，
# 脚本化驱动 UI 断言多订单/预计花费/资料编辑/导航 URL/区域下拉行为。
# 不属于交付模块，仅用于开发验证；用法见 run_scenarios.sh。
QT += core gui widgets network testlib

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = v22_harness
TEMPLATE = app

INCLUDEPATH += ../../src

SRC = ../..

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += EVCP_HAVE_WEBENGINE
}

SOURCES += \
    main.cpp \
    $$SRC/src/mainwindow.cpp \
    $$SRC/src/model/appconfig.cpp \
    $$SRC/src/model/models.cpp \
    $$SRC/src/net/socketclient.cpp \
    $$SRC/src/map/mapbridge.cpp \
    $$SRC/src/ui/smscodebutton.cpp \
    $$SRC/src/pages/loginpage.cpp \
    $$SRC/src/pages/resetpassworddialog.cpp \
    $$SRC/src/pages/passworddialog.cpp \
    $$SRC/src/pages/findstationpage.cpp \
    $$SRC/src/pages/chargingpage.cpp \
    $$SRC/src/pages/mypage.cpp \
    $$SRC/src/pages/profileeditdialog.cpp \
    $$SRC/src/pages/rechargedialog.cpp \
    $$SRC/src/pages/navigationdialog.cpp

HEADERS += \
    $$SRC/src/mainwindow.h \
    $$SRC/src/model/appconfig.h \
    $$SRC/src/model/models.h \
    $$SRC/src/net/socketclient.h \
    $$SRC/src/map/mapbridge.h \
    $$SRC/src/ui/uienums.h \
    $$SRC/src/ui/avatarutils.h \
    $$SRC/src/ui/smscodebutton.h \
    $$SRC/src/pages/loginpage.h \
    $$SRC/src/pages/resetpassworddialog.h \
    $$SRC/src/pages/passworddialog.h \
    $$SRC/src/pages/findstationpage.h \
    $$SRC/src/pages/chargingpage.h \
    $$SRC/src/pages/mypage.h \
    $$SRC/src/pages/profileeditdialog.h \
    $$SRC/src/pages/rechargedialog.h \
    $$SRC/src/pages/navigationdialog.h

RESOURCES += $$SRC/resources/resources.qrc
