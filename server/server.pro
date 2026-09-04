QT += core network sql
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = server

HEADERS += \
    connection.h \
    database.h \
    handlers.h \
    httpserver.h \
    protocol.h \
    stats.h \
    tcpserver.h \
    timeutil.h

SOURCES += \
    connection.cpp \
    database.cpp \
    handlers.cpp \
    httpserver.cpp \
    main.cpp \
    protocol.cpp \
    stats.cpp \
    tcpserver.cpp \
    timeutil.cpp
