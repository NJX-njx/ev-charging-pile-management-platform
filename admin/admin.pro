QT += core gui widgets network charts

CONFIG += c++17

TARGET = AdminClient
TEMPLATE = app

SOURCES += \
    main.cpp \
    net/socketclient.cpp \
    ui/loginwindow.cpp \
    ui/mainwindow.cpp

HEADERS += \
    net/socketclient.h \
    ui/loginwindow.h \
    ui/mainwindow.h

FORMS += \
    ui/loginwindow.ui \
    ui/mainwindow.ui
