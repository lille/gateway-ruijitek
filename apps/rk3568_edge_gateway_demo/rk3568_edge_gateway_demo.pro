QT += core network serialport serialbus
CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = rk3568-edge-gateway-demo

INCLUDEPATH += ../../src

SOURCES += \
    ../../src/appconfig.cpp \
    ../../src/datastore.cpp \
    ../../src/dtupassthroughserver.cpp \
    ../../src/gatewayservice.cpp \
    ../../src/httpserver.cpp \
    ../../src/main.cpp \
    ../../src/protocolcollector.cpp

HEADERS += \
    ../../src/appconfig.h \
    ../../src/datastore.h \
    ../../src/dtupassthroughserver.h \
    ../../src/gatewayservice.h \
    ../../src/httpserver.h \
    ../../src/protocolcollector.h \
    ../../src/types.h

DISTFILES += \
    ../../config/gateway-demo.json \
    ../../tools/mock-dtu-send.ps1 \
    ../../README.md \
    ../../docs/rk3568-deploy.md
