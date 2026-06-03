QT += core gui widgets
CONFIG += c++17

TEMPLATE = app
TARGET = pc-dtu-config-tool

INCLUDEPATH += ../../src/pc_tool

SOURCES += \
    ../../src/pc_tool/main.cpp \
    ../../src/pc_tool/pcconfigwindow.cpp

HEADERS += \
    ../../src/pc_tool/pcconfigwindow.h

DISTFILES += \
    ../../config/rk3568-dtu-demo.json \
    ../../README.md
