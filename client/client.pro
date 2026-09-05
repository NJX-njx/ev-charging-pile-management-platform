QT += core gui widgets network

CONFIG += c++17
CONFIG -= app_bundle

TARGET = client
TEMPLATE = app

INCLUDEPATH += src

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += EVCP_HAVE_WEBENGINE
}

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/model/appconfig.cpp \
    src/model/models.cpp \
    src/net/socketclient.cpp \
    src/map/mapbridge.cpp \
    src/ui/smscodebutton.cpp \
    src/pages/loginpage.cpp \
    src/pages/resetpassworddialog.cpp \
    src/pages/passworddialog.cpp \
    src/pages/findstationpage.cpp \
    src/pages/chargingpage.cpp \
    src/pages/mypage.cpp

HEADERS += \
    src/mainwindow.h \
    src/model/appconfig.h \
    src/model/models.h \
    src/net/socketclient.h \
    src/map/mapbridge.h \
    src/map/tencentmapkey.h \
    src/ui/uienums.h \
    src/ui/smscodebutton.h \
    src/pages/loginpage.h \
    src/pages/resetpassworddialog.h \
    src/pages/passworddialog.h \
    src/pages/findstationpage.h \
    src/pages/chargingpage.h \
    src/pages/mypage.h

RESOURCES += resources/resources.qrc
