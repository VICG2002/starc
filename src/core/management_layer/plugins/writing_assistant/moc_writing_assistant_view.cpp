/****************************************************************************
** Meta object code from reading C++ file 'writing_assistant_view.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "writing_assistant_view.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'writing_assistant_view.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN2Ui20WritingAssistantViewE_t {};
} // unnamed namespace

template <> constexpr inline auto Ui::WritingAssistantView::qt_create_metaobjectdata<qt_meta_tag_ZN2Ui20WritingAssistantViewE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Ui::WritingAssistantView",
        "messageSubmitted",
        "",
        "_text",
        "newConversationRequested"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'messageSubmitted'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'newConversationRequested'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<WritingAssistantView, qt_meta_tag_ZN2Ui20WritingAssistantViewE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Ui::WritingAssistantView::staticMetaObject = { {
    QMetaObject::SuperData::link<Widget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2Ui20WritingAssistantViewE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2Ui20WritingAssistantViewE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN2Ui20WritingAssistantViewE_t>.metaTypes,
    nullptr
} };

void Ui::WritingAssistantView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<WritingAssistantView *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->messageSubmitted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->newConversationRequested(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (WritingAssistantView::*)(const QString & )>(_a, &WritingAssistantView::messageSubmitted, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (WritingAssistantView::*)()>(_a, &WritingAssistantView::newConversationRequested, 1))
            return;
    }
}

const QMetaObject *Ui::WritingAssistantView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Ui::WritingAssistantView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2Ui20WritingAssistantViewE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IDocumentView"))
        return static_cast< IDocumentView*>(this);
    return Widget::qt_metacast(_clname);
}

int Ui::WritingAssistantView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Widget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void Ui::WritingAssistantView::messageSubmitted(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void Ui::WritingAssistantView::newConversationRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
