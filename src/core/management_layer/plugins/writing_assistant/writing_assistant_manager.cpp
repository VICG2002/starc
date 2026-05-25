#include "writing_assistant_manager.h"

#include "writing_assistant_view.h"

#include <QPointer>


namespace ManagementLayer {

class WritingAssistantManager::Implementation
{
public:
    /**
     * @brief Crear vista (la del asistente)
     */
    Ui::WritingAssistantView* createView();


    /**
     * @brief Vista principal y secundaria del asistente
     */
    Ui::WritingAssistantView* view = nullptr;
    Ui::WritingAssistantView* secondaryView = nullptr;

    /**
     * @brief Todas las vistas creadas (para multi-instancia)
     */
    QVector<QPointer<Ui::WritingAssistantView>> allViews;
};

Ui::WritingAssistantView* WritingAssistantManager::Implementation::createView()
{
    auto newView = new Ui::WritingAssistantView;
    allViews.append(newView);
    return newView;
}


// ****


WritingAssistantManager::WritingAssistantManager(QObject* _parent)
    : QObject(_parent)
    , d(new Implementation)
{
}

WritingAssistantManager::~WritingAssistantManager() = default;

Ui::IDocumentView* WritingAssistantManager::view()
{
    return d->view;
}

Ui::IDocumentView* WritingAssistantManager::view(BusinessLayer::AbstractModel* _model)
{
    Q_UNUSED(_model)
    //
    // Iteración 1: el asistente no consume el modelo del guion todavía.
    // En iteración 2 castearemos a ScreenplayTextModel* y conectaremos signals.
    //
    if (d->view == nullptr) {
        d->view = d->createView();
    }

    return d->view;
}

Ui::IDocumentView* WritingAssistantManager::secondaryView()
{
    return d->secondaryView;
}

Ui::IDocumentView* WritingAssistantManager::secondaryView(BusinessLayer::AbstractModel* _model)
{
    Q_UNUSED(_model)
    if (d->secondaryView == nullptr) {
        d->secondaryView = d->createView();
    }

    return d->secondaryView;
}

Ui::IDocumentView* WritingAssistantManager::createView(BusinessLayer::AbstractModel* _model)
{
    Q_UNUSED(_model)
    return d->createView();
}

void WritingAssistantManager::resetModels()
{
    //
    // Iteración 1: no hay modelos asociados. Nada que resetear.
    //
}

void WritingAssistantManager::setEditingMode(DocumentEditingMode _mode)
{
    for (auto& view : d->allViews) {
        if (view.isNull()) {
            continue;
        }
        view->setEditingMode(_mode);
    }
}

} // namespace ManagementLayer
