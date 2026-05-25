#include "production_schedule_manager.h"

#include "production_schedule_view.h"

#include <QPointer>


namespace ManagementLayer {

class ProductionScheduleManager::Implementation
{
public:
    Ui::ProductionScheduleView* createView();

    Ui::ProductionScheduleView* view = nullptr;
    Ui::ProductionScheduleView* secondaryView = nullptr;
    QVector<QPointer<Ui::ProductionScheduleView>> allViews;
};

Ui::ProductionScheduleView* ProductionScheduleManager::Implementation::createView()
{
    auto* newView = new Ui::ProductionScheduleView;
    allViews.append(newView);
    return newView;
}


// ****


ProductionScheduleManager::ProductionScheduleManager(QObject* _parent)
    : QObject(_parent)
    , d(new Implementation)
{
}

ProductionScheduleManager::~ProductionScheduleManager() = default;

Ui::IDocumentView* ProductionScheduleManager::view()
{
    return d->view;
}

Ui::IDocumentView* ProductionScheduleManager::view(BusinessLayer::AbstractModel* _model)
{
    if (d->view == nullptr) {
        d->view = d->createView();
    }
    d->view->setScreenplayModel(_model);
    return d->view;
}

Ui::IDocumentView* ProductionScheduleManager::secondaryView()
{
    return d->secondaryView;
}

Ui::IDocumentView* ProductionScheduleManager::secondaryView(BusinessLayer::AbstractModel* _model)
{
    if (d->secondaryView == nullptr) {
        d->secondaryView = d->createView();
    }
    d->secondaryView->setScreenplayModel(_model);
    return d->secondaryView;
}

Ui::IDocumentView* ProductionScheduleManager::createView(BusinessLayer::AbstractModel* _model)
{
    auto* newView = d->createView();
    newView->setScreenplayModel(_model);
    return newView;
}

void ProductionScheduleManager::resetModels()
{
    for (auto& v : d->allViews) {
        if (!v.isNull()) {
            v->setScreenplayModel(nullptr);
        }
    }
}

void ProductionScheduleManager::setEditingMode(DocumentEditingMode _mode)
{
    for (auto& v : d->allViews) {
        if (v.isNull()) {
            continue;
        }
        v->setEditingMode(_mode);
    }
}

} // namespace ManagementLayer
