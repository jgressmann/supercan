TARGET = qtsupercanbus

QT = core serialbus

HEADERS += \
    supercanbackend.h \
    ../inc/supercan.h \
    ../inc/supercan_dll.h \
    ../inc/supercan_winapi.h \



SOURCES += \
    main.cpp \
    supercanbackend.cpp

INC += ../inc

sc_static:{
    SOURCES += ../dll/supercan_dll.c
    DEFINES += SC_STATIC=1
    LIBS += -lCfgmgr32 -lwinusb
} else {
    HEADERS += supercan_symbols_p.h
}



DISTFILES = plugin.json


PLUGIN_TYPE = canbus
PLUGIN_EXTENDS = serialbus
PLUGIN_CLASS_NAME = SuperCanBusPlugin
load(qt_plugin)
