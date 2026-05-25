#include "writing_assistant_manager.h"

#include "claude_client.h"
#include "writing_assistant_view.h"

#include <business_layer/model/screenplay/text/screenplay_text_model.h>
#include <business_layer/model/screenplay/screenplay_dictionaries_model.h>

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
    // Wire: usuario pidió nueva conversación → cliente olvida session_id,
    // todas las vistas limpian su área de respuestas.
    //
    QObject::connect(newView, &Ui::WritingAssistantView::newConversationRequested,
                     claudeClient, [this] {
                         claudeClient->resetConversation();
                         for (auto& v : allViews) {
                             if (!v.isNull()) {
                                 v->clearConversation();
                                 v->setStatus(QObject::tr("Nueva conversación iniciada"));
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

namespace {

/**
 * @brief Construir resumen del guion para inyectar a Claude como system prompt.
 *        Texto compacto en español que Claude lee al inicio de la sesión.
 */
QString buildScreenplayContext(BusinessLayer::AbstractModel* _model)
{
    auto* screenplay = qobject_cast<BusinessLayer::ScreenplayTextModel*>(_model);
    if (screenplay == nullptr) {
        return QString();
    }

    QStringList lines;
    lines << QStringLiteral("Eres un asistente para un guionista de cine que está "
                            "escribiendo el siguiente proyecto en el software Aula 122. "
                            "Aquí está el contexto del guion actual:");
    lines << QString();

    //
    // Personajes registrados en la lista del proyecto
    //
    if (auto* charactersList = screenplay->charactersList()) {
        QStringList names;
        for (int i = 0; i < charactersList->rowCount() && i < 30; ++i) {
            names << charactersList->index(i, 0).data().toString();
        }
        if (!names.isEmpty()) {
            lines << QStringLiteral("Personajes registrados (%1): %2")
                         .arg(charactersList->rowCount())
                         .arg(names.join(QStringLiteral(", ")));
        }
    }

    //
    // Locaciones registradas en la lista del proyecto
    //
    if (auto* locationsList = screenplay->locationsList()) {
        QStringList names;
        for (int i = 0; i < locationsList->rowCount() && i < 30; ++i) {
            names << locationsList->index(i, 0).data().toString();
        }
        if (!names.isEmpty()) {
            lines << QStringLiteral("Locaciones registradas (%1): %2")
                         .arg(locationsList->rowCount())
                         .arg(names.join(QStringLiteral(", ")));
        }
    }

    //
    // Métricas globales
    //
    lines << QStringLiteral("Escenas: %1 · Páginas: %2 · Palabras: %3")
                 .arg(screenplay->scenesCount())
                 .arg(screenplay->scriptPageCount())
                 .arg(screenplay->wordsCount());

    lines << QString();
    lines << QStringLiteral("Responde en español neutro. Sé conciso y específico al guión.");
    lines << QStringLiteral("Si el usuario pide algo que requiera leer el guion entero, "
                            "explica que solo tienes este resumen y pídele que pegue "
                            "el fragmento concreto que quiere discutir.");

    return lines.join(QStringLiteral("\n"));
}

} // anonymous namespace

Ui::IDocumentView* WritingAssistantManager::view(BusinessLayer::AbstractModel* _model)
{
    if (d->view == nullptr) {
        d->view = d->createView();
    }
    //
    // Aula 122: si nos pasan un ScreenplayTextModel, construimos contexto
    // y se lo damos al cliente Claude. Si _model es null o no es screenplay,
    // limpiamos el contexto (chat genérico).
    //
    d->claudeClient->setScreenplayContext(buildScreenplayContext(_model));
    return d->view;
}

Ui::IDocumentView* WritingAssistantManager::secondaryView()
{
    return d->secondaryView;
}

Ui::IDocumentView* WritingAssistantManager::secondaryView(BusinessLayer::AbstractModel* _model)
{
    if (d->secondaryView == nullptr) {
        d->secondaryView = d->createView();
    }
    d->claudeClient->setScreenplayContext(buildScreenplayContext(_model));
    return d->secondaryView;
}

Ui::IDocumentView* WritingAssistantManager::createView(BusinessLayer::AbstractModel* _model)
{
    d->claudeClient->setScreenplayContext(buildScreenplayContext(_model));
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
