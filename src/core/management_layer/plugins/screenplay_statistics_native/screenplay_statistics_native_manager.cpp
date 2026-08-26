#include "screenplay_statistics_native_manager.h"

#include "screenplay_statistics_native_view.h"

#include <QMetaObject>
#include <QModelIndex>
#include <QPointer>


namespace ManagementLayer {

class ScreenplayStatisticsNativeManager::Implementation
{
public:
    Ui::ScreenplayStatisticsNativeView* createView();

    Ui::ScreenplayStatisticsNativeView* view = nullptr;
    Ui::ScreenplayStatisticsNativeView* secondaryView = nullptr;
    QVector<QPointer<Ui::ScreenplayStatisticsNativeView>> allViews;
};

Ui::ScreenplayStatisticsNativeView* ScreenplayStatisticsNativeManager::Implementation::createView()
{
    auto* newView = new Ui::ScreenplayStatisticsNativeView;
    allViews.append(newView);
    return newView;
}


// ****


ScreenplayStatisticsNativeManager::ScreenplayStatisticsNativeManager(QObject* _parent)
    : QObject(_parent)
    , d(new Implementation)
{
}

ScreenplayStatisticsNativeManager::~ScreenplayStatisticsNativeManager() = default;

QObject* ScreenplayStatisticsNativeManager::asQObject()
{
    return this;
}

Ui::IDocumentView* ScreenplayStatisticsNativeManager::view()
{
    return d->view;
}

Ui::IDocumentView* ScreenplayStatisticsNativeManager::view(BusinessLayer::AbstractModel* _model)
{
    if (d->view == nullptr) {
        d->view = d->createView();
    }
    d->view->setStatisticsModel(_model);
    return d->view;
}

Ui::IDocumentView* ScreenplayStatisticsNativeManager::secondaryView()
{
    return d->secondaryView;
}

Ui::IDocumentView* ScreenplayStatisticsNativeManager::secondaryView(
    BusinessLayer::AbstractModel* _model)
{
    if (d->secondaryView == nullptr) {
        d->secondaryView = d->createView();
    }
    d->secondaryView->setStatisticsModel(_model);
    return d->secondaryView;
}

Ui::IDocumentView* ScreenplayStatisticsNativeManager::createView(
    BusinessLayer::AbstractModel* _model)
{
    auto* newView = d->createView();
    newView->setStatisticsModel(_model);
    return newView;
}

void ScreenplayStatisticsNativeManager::resetModels()
{
    for (auto& v : d->allViews) {
        if (!v.isNull()) {
            v->setStatisticsModel(nullptr);
        }
    }
}

void ScreenplayStatisticsNativeManager::bind(IDocumentManager* _manager)
{
    if (_manager == nullptr || _manager == this) {
        return;
    }

    //
    // El único par con el que este manager se enlaza es el navegador lateral
    // `screenplay_statistics_structure` — lo hace PluginsBuilder::bind() al
    // asociar el MIME de vista con el de navegador. Conectamos por firma de
    // texto (sin conocer su tipo concreto, es un plugin aparte) y solo si de
    // verdad emite esas señales, para no ensuciar el log con warnings de
    // connect si algún día se enlaza con otra cosa.
    //
    auto* other = _manager->asQObject();
    if (other == nullptr) {
        return;
    }

    if (other->metaObject()->indexOfSignal(
            QMetaObject::normalizedSignature("currentReportIndexChanged(QModelIndex)"))
        >= 0) {
        connect(other, SIGNAL(currentReportIndexChanged(QModelIndex)), this,
                SLOT(setCurrentReportIndex(QModelIndex)), Qt::UniqueConnection);
    }
    if (other->metaObject()->indexOfSignal(
            QMetaObject::normalizedSignature("currentPlotIndexChanged(QModelIndex)"))
        >= 0) {
        connect(other, SIGNAL(currentPlotIndexChanged(QModelIndex)), this,
                SLOT(setCurrentPlotIndex(QModelIndex)), Qt::UniqueConnection);
    }
}

void ScreenplayStatisticsNativeManager::setCurrentReportIndex(const QModelIndex& _index)
{
    if (d->view != nullptr) {
        d->view->setCurrentReport(_index.row());
    }
}

void ScreenplayStatisticsNativeManager::setCurrentPlotIndex(const QModelIndex& _index)
{
    if (d->view != nullptr) {
        d->view->setCurrentPlot(_index.row());
    }
}

} // namespace ManagementLayer
