TARGET = qtsupercanbus

QT = core serialbus

HEADERS += \
    supercanbackend.h \
    supercan_srv.tlh.h

SOURCES += \
    main.cpp \
    supercanbackend.cpp \
    ../../src/can_bit_timing.c

INCLUDEPATH += ../inc ../../src
DEFINES += SC_STATIC
*-g++ {
    LIBS += -lole32 -loleaut32 -static
}

DISTFILES = plugin.json

PLUGIN_TYPE = canbus
PLUGIN_EXTENDS = serialbus
PLUGIN_CLASS_NAME = SuperCanBusPlugin
load(qt_plugin)
