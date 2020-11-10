TARGET = qtsupercanbus

QT = core serialbus

HEADERS += \
    supercanbackend.h



SOURCES += \
    main.cpp \
    supercanbackend.cpp

INCLUDEPATH += ../inc ../../src

#sc_static:{
    SOURCES += ../dll/supercan_dll.c
    SOURCES += ../../src/can_bit_timing.c
    DEFINES += SC_STATIC=1
    LIBS += -lCfgmgr32 -lwinusb
#} else {
#    HEADERS += supercan_symbols_p.h
#}



DISTFILES = plugin.json


PLUGIN_TYPE = canbus
PLUGIN_EXTENDS = serialbus
PLUGIN_CLASS_NAME = SuperCanBusPlugin
load(qt_plugin)
