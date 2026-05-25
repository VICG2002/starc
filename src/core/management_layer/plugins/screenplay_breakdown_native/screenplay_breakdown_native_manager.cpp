#include "screenplay_breakdown_native_manager.h"

#include "screenplay_breakdown_native_view.h"

#include <QPointer>


namespace ManagementLayer {

class ScreenplayBreakdownNativeManager::Implementation
{
public:
    Ui::ScreenplayBreakdownNativeView* createView();

    Ui::ScreenplayBreakdownNativeView* view = nullptr;
    Ui::ScreenplayBreakdownNativeView* secondaryView = nullptr;
    QVector<QPointer<Ui::ScreenplayBreakdownNativeView>> allViews;
};

Ui::ScreenplayBreakdownNativeView* ScreenplayBreakdownNativeManager::Implementation::createView()
{
    auto* newView = new Ui::ScreenplayBreakdownNativeView;
    allViews.append(newView);
    return newView;
}


// ****


ScreenplayBreakdownNativeManager::ScreenplayBreakdownNativeManager(QObject* _parent)
    : QObject(_parent)
    , d(new Implementation)
{
}

ScreenplayBreakdownNativeManager::~ScreenplayBreakdownNativeManager() = default;

Ui::IDocumentView* ScreenplayBreakdownNativeManager::view()
{
    return d->view;
}

Ui::IDocumentView* ScreenplayBreakdownNativeManager::view(BusinessLayer::AbstractModel* _model)
{
    if (d->view == nullptr) {
        d->view = d->createView();
    }
    d->view->setScreenplayModel(_model);
    return d->view;
}

Ui::IDocumentView* ScreenplayBreakdownNativeManager::secondaryView()
{
    return d->secondaryView;
}

Ui::IDocumentView* ScreenplayBreakdownNativeManager::secondaryView(
    BusinessLayer::AbstractModel* _model)
{
    if (d->secondaryView == nullptr) {
        d->secondaryView = d->createView();
    }
    d->secondaryView->setScreenplayModel(_model);
    return d->secondaryView;
}

Ui::IDocumentView* ScreenplayBreakdownNativeManager::createView(
    BusinessLayer::AbstractModel* _model)
{
    auto* newView = d->createView();
    newView->setScreenplayModel(_model);
    return newView;
}

void ScreenplayBreakdownNativeManager::resetModels()
{
    for (auto& v : d->allViews) {
        if (!v.isNull()) {
            v->setScreenplayModel(nullptr);
        }
    }
}

void ScreenplayBreakdownNativeManager::setEditingMode(DocumentEditingMode _mode)
{
    for (auto& v : d->allViews) {
        if (v.isNull()) {
            continue;
        }
        v->setEditingMode(_mode);
    }
}

} // namespace ManagementLayer
