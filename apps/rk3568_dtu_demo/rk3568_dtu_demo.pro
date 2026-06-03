QT += core network serialport serialbus
CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = rk3568-dtu-terminal-demo

INCLUDEPATH += ../../src ../../src/dtu_demo

SOURCES += \
    ../../src/dtu_demo/dtuclientservice.cpp \
    ../../src/dtu_demo/main.cpp

HEADERS += \
    ../../src/types.h \
    ../../src/dtu_demo/dtuclientservice.h

DISTFILES += \
    ../../config/rk3568-dtu-demo.json \
    ../../README.md \
    ../../docs/rk3568-deploy.md
