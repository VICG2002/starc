TEMPLATE = lib

CONFIG += plugin c++1z
CONFIG += force_debug_info
CONFIG += separate_debug_info
QT += widgets network

TARGET = writingassistantplugin

DEFINES += MANAGER_PLUGIN
DEFINES += QT_DEPRECATED_WARNINGS

mac {
    DESTDIR = ../../../../_build/Aula_122.app/Contents/PlugIns
    CORELIBDIR = ../../../../_build/Aula_122.app/Contents/Frameworks
} else {
    DESTDIR = ../../../../_build/plugins
    CORELIBDIR = ../../../../_build
}

INCLUDEPATH += $$PWD/../../../..

LIBS += -L$$CORELIBDIR/ -lcorelib
INCLUDEPATH += $$PWD/../../../../corelib
DEPENDPATH += $$PWD/../../../../corelib

HEADERS += \
    claude_client.h \
    odysseus_client.h \
    structures_loader.h \
    writing_assistant_manager.h \
    writing_assistant_view.h

SOURCES += \
    claude_client.cpp \
    odysseus_client.cpp \
    structures_loader.cpp \
    writing_assistant_manager.cpp \
    writing_assistant_view.cpp

mac {
    load(resolve_target)
    QMAKE_POST_LINK += install_name_tool -change libcorelib.1.dylib @executable_path/../Frameworks/libcorelib.dylib $$QMAKE_RESOLVED_TARGET
}
