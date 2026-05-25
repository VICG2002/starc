#include "writing_assistant_manager.h"

#include "claude_client.h"
#include "writing_assistant_view.h"

#include <QPointer>


namespace ManagementLayer {

class WritingAssistantManager::Implementation
{
public:
    Implementation();

    /**
     * @brief Crear vista y conectarla al cliente de Claude
     */
    Ui::WritingAssistantView* createView();


    /**
     * @brief Cliente HTTP a la API de Anthropic (compartido entre vistas)
     */
    ClaudeClient* claudeClient = nullptr;

    /**
     * @brief Vistas activas (primary / secondary / multi-instance)
     */
    Ui::WritingAssistantView* view = nullptr;
    Ui::WritingAssistantView* secondaryView = nullptr;
    QVector<QPointer<Ui::WritingAssistantView>> allViews;
};

WritingAssistantManager::Implementation::Implementation()
    : claudeClient(new ClaudeClient)
{
}

Ui::WritingAssistantView* WritingAssistantManager::Implementation::createView()
{
    auto* newView = new Ui::WritingAssistantView;
    allViews.append(newView);

    //
    // Wire: usuario envía → ClaudeClient envía a la API
    //
    QObject::connect(newView, &Ui::WritingAssistantView::messageSubmitted,
                     claudeClient, [this, viewPtr = QPointer<Ui::WritingAssistantView>(newView)](
                                       const QString& _text) {
                         if (viewPtr.isNull()) {
                             return;
                         }
                         viewPtr->appendUserMessage(_text);
                         viewPtr->setInputEnabled(false);
                         viewPtr->setStatus(QObject::tr("Esperando respuesta de Claude..."));
                         claudeClient->sendMessage(_text);
                     });

    //
    // Wire: Claude responde → mostrar en TODAS las vistas activas (no sabemos cuál envió)
    // Para una sola vista esto funciona. Para multi-vista podría refinar después.
    //
    QObject::connect(claudeClient, &ClaudeClient::responseReceived,
                     newView, [this](const QString& _response) {
                         for (auto& v : allViews) {
                             if (!v.isNull()) {
                                 v->appendAssistantMessage(_response);
                                 v->setInputEnabled(true);
                                 v->setStatus(QString());
                             }
                         }
                     });

    QObject::connect(claudeClient, &ClaudeClient::errorOccurred,
                     newView, [this](const QString& _error) {
                         for (auto& v : allViews) {
                             if (!v.isNull()) {
                                 v->appendError(_error);
                                 v->setInputEnabled(true);
                                 v->setStatus(QObject::tr("Error — corregir y reintentar"));
                             }
                         }
                     });

    //
    // Status inicial según disponibilidad del CLI de Claude Code
    //
    if (!claudeClient->isAvailable()) {
        newView->setStatus(
            QObject::tr("⚠ Claude Code CLI no encontrado — instálalo y reinicia"));
    }

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
    // Sin modelos asociados en iteración 2c
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
