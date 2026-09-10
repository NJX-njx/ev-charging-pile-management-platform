QT += core gui widgets network

QT += charts

CONFIG += c++17

TARGET = AdminClient
TEMPLATE = app

# 构建产物集中放在构建目录下。
OBJECTS_DIR = .build/obj
MOC_DIR = .build/moc
RCC_DIR = .build/rcc
UI_DIR = .build/ui
DESTDIR = bin

SOURCES += \
    main.cpp \
    net/socketclient.cpp \
    ui/filtertable.cpp \
    ui/loginwindow.cpp \
    ui/mainwindow.cpp \
    ui/orderpage.cpp \
    ui/salespage.cpp \
    ui/stationpilepage.cpp \
    ui/systempage.cpp \
    ui/usereditdialog.cpp \
    ui/userpage.cpp

HEADERS += \
    net/socketclient.h \
    ui/filtertable.h \
    ui/loginwindow.h \
    ui/mainwindow.h \
    ui/orderpage.h \
    ui/salespage.h \
    ui/stationimport.h \
    ui/stationpilepage.h \
    ui/systempage.h \
    ui/uienums.h \
    ui/usereditdialog.h \
    ui/userpage.h

FORMS += \
    ui/loginwindow.ui \
    ui/mainwindow.ui

RESOURCES += \
    resources/resources.qrc
