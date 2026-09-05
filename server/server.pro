QT += core network sql
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = server

INCLUDEPATH += src/net src/db src/biz src/http src/util

# 就地构建时把中间产物收进 .build/、可执行文件收进 bin/；
# 影子构建（推荐，Qt Creator 默认）下这些目录都在构建目录内，源码目录始终干净
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
