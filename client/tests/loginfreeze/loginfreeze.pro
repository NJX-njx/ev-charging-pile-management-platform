# 登录流程卡死回归 harness（offscreen）：链接真实 client 源码，脚本化点击按钮。
# 不属于交付模块，仅用于开发验证；用法见 run_scenarios.sh。
QT += core gui widgets network

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = loginfreeze_harness
TEMPLATE = app

INCLUDEPATH += ../../src

SRC = ../..

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
    $$SRC/src/ui/passwordtoggle.h \
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
