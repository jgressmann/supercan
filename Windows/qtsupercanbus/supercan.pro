TARGET = qtsupercanbus

QT = core serialbus

HEADERS += \
    supercanbackend.h



SOURCES += \
    main.cpp \
    supercanbackend.cpp \
    ../../src/can_bit_timing.c

INCLUDEPATH += ../inc ../../src



DISTFILES = plugin.json


PLUGIN_TYPE = canbus
PLUGIN_EXTENDS = serialbus
PLUGIN_CLASS_NAME = SuperCanBusPlugin
load(qt_plugin)
