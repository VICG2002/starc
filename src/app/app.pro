TEMPLATE = app
TARGET = Aula_122

CONFIG += c++1z
CONFIG += force_debug_info
CONFIG += separate_debug_info
QT += core gui widgets

DEFINES += QT_DEPRECATED_WARNINGS

DESTDIR = ../_build/

INCLUDEPATH += ..

LIBS += -L$$DESTDIR

include(../3rd_party/qbreakpad/qBreakpad.pri)

SOURCES += \
        application.cpp \
        main.cpp

HEADERS += \
    application.h

win32:RC_FILE = app.rc
macx {
    ICON = icon.icns
    QMAKE_INFO_PLIST = Info.plist

    #
    # Fase D del empaquetado del cerebro: con `qmake CONFIG+=bundle_brain`, tras
    # enlazar la app se ensambla el cerebro (python + llama + odysseus) dentro del
    # .app (ver ai/PACKAGING.md). Por defecto NO se ejecuta — los builds de
    # desarrollo apuntan a ai/brain vía $AULA122_BRAIN_DIR (symlinks, instantáneo).
    #
    bundle_brain {
        QMAKE_POST_LINK += $$shell_quote($$PWD/../../ai/build-brain-bundle.sh) $$shell_quote($$PWD/../_build/Aula_122.app)$$escape_expand(\\n\\t)
    }
}
