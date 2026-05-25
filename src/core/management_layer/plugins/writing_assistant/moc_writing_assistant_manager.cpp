/****************************************************************************
** Meta object code from reading C++ file 'writing_assistant_manager.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "writing_assistant_manager.h"
#include <QtCore/qmetatype.h>
#include <QtCore/qplugin.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'writing_assistant_manager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN15ManagementLayer23WritingAssistantManagerE_t {};
} // unnamed namespace

template <> constexpr inline auto ManagementLayer::WritingAssistantManager::qt_create_metaobjectdata<qt_meta_tag_ZN15ManagementLayer23WritingAssistantManagerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ManagementLayer::WritingAssistantManager"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<WritingAssistantManager, qt_meta_tag_ZN15ManagementLayer23WritingAssistantManagerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ManagementLayer::WritingAssistantManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15ManagementLayer23WritingAssistantManagerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15ManagementLayer23WritingAssistantManagerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15ManagementLayer23WritingAssistantManagerE_t>.metaTypes,
    nullptr
} };

void ManagementLayer::WritingAssistantManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<WritingAssistantManager *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *ManagementLayer::WritingAssistantManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ManagementLayer::WritingAssistantManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15ManagementLayer23WritingAssistantManagerE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IDocumentManager"))
        return static_cast< IDocumentManager*>(this);
    if (!strcmp(_clname, "app.starc.ManagementLayer.IDocumentManager"))
        return static_cast< ManagementLayer::IDocumentManager*>(this);
    return QObject::qt_metacast(_clname);
}

int ManagementLayer::WritingAssistantManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
using namespace ManagementLayer;

#ifdef QT_MOC_EXPORT_PLUGIN_V2
static constexpr unsigned char qt_pluginMetaDataV2_WritingAssistantManager[] = {
    0xbf, 
    // "IID"
    0x02,  0x78,  0x2a,  'a',  'p',  'p',  '.',  's', 
    't',  'a',  'r',  'c',  '.',  'M',  'a',  'n', 
    'a',  'g',  'e',  'm',  'e',  'n',  't',  'L', 
    'a',  'y',  'e',  'r',  '.',  'I',  'D',  'o', 
    'c',  'u',  'm',  'e',  'n',  't',  'M',  'a', 
    'n',  'a',  'g',  'e',  'r', 
    // "className"
    0x03,  0x77,  'W',  'r',  'i',  't',  'i',  'n', 
    'g',  'A',  's',  's',  'i',  's',  't',  'a', 
    'n',  't',  'M',  'a',  'n',  'a',  'g',  'e', 
    'r', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN_V2(ManagementLayer::WritingAssistantManager, WritingAssistantManager, qt_pluginMetaDataV2_WritingAssistantManager)
#else
QT_PLUGIN_METADATA_SECTION
Q_CONSTINIT static constexpr unsigned char qt_pluginMetaData_WritingAssistantManager[] = {
    'Q', 'T', 'M', 'E', 'T', 'A', 'D', 'A', 'T', 'A', ' ', '!',
    // metadata version, Qt version, architectural requirements
    0, QT_VERSION_MAJOR, QT_VERSION_MINOR, qPluginArchRequirements(),
    0xbf, 
    // "IID"
    0x02,  0x78,  0x2a,  'a',  'p',  'p',  '.',  's', 
    't',  'a',  'r',  'c',  '.',  'M',  'a',  'n', 
    'a',  'g',  'e',  'm',  'e',  'n',  't',  'L', 
    'a',  'y',  'e',  'r',  '.',  'I',  'D',  'o', 
    'c',  'u',  'm',  'e',  'n',  't',  'M',  'a', 
    'n',  'a',  'g',  'e',  'r', 
    // "className"
    0x03,  0x77,  'W',  'r',  'i',  't',  'i',  'n', 
    'g',  'A',  's',  's',  'i',  's',  't',  'a', 
    'n',  't',  'M',  'a',  'n',  'a',  'g',  'e', 
    'r', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN(ManagementLayer::WritingAssistantManager, WritingAssistantManager)
#endif  // QT_MOC_EXPORT_PLUGIN_V2

QT_WARNING_POP
