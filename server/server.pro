!equals(QT_MAJOR_VERSION, 6): error("This project requires Qt 6.")

QT += core network sql
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = server

INCLUDEPATH += src/net src/db src/biz src/http src/util

# 构建产物集中放在构建目录下。
OBJECTS_DIR = .build/obj
MOC_DIR = .build/moc
RCC_DIR = .build/rcc
UI_DIR = .build/ui
DESTDIR = bin

HEADERS += \
    src/net/tcpserver.h \
    src/net/connection.h \
    src/db/database.h \
    src/biz/handlers.h \
    src/biz/protocol.h \
    src/biz/stats.h \
    src/http/httpserver.h \
    src/util/timeutil.h

SOURCES += \
    src/main.cpp \
    src/net/tcpserver.cpp \
    src/net/connection.cpp \
    src/db/database.cpp \
    src/biz/handlers.cpp \
    src/biz/protocol.cpp \
    src/biz/stats.cpp \
    src/http/httpserver.cpp \
    src/util/timeutil.cpp
