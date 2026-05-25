#pragma once

#include <interfaces/ui/i_document_view.h>
#include <ui/widgets/widget/widget.h>


namespace Ui {

/**
 * @brief Vista del asistente de escritura — panel de chat con Claude.
 *
 * Layout vertical:
 *   - Título "Asistente de escritura"
 *   - Área de respuestas (QTextEdit read-only, scrollable)
 *   - Input del usuario (QLineEdit)
 *   - Botón "Enviar"
 *   - Status label al pie
 */
class WritingAssistantView : public Widget, public IDocumentView
{
    Q_OBJECT

public:
    explicit WritingAssistantView(QWidget* _parent = nullptr);
    ~WritingAssistantView() override;

    /**
     * @brief Implementación de IDocumentView
     */
    /** @{ */
    QWidget* asQWidget() override;
    void setEditingMode(ManagementLayer::DocumentEditingMode _mode) override;
    /** @} */

    /**
     * @brief Añadir un mensaje del usuario al área de respuestas (prefijo "Tú:")
     */
    void appendUserMessage(const QString& _text);

    /**
     * @brief Añadir una respuesta de Claude al área de respuestas (prefijo "Claude:")
     */
    void appendAssistantMessage(const QString& _text);

    /**
     * @brief Mostrar mensaje de error en el área de respuestas
     */
    void appendError(const QString& _error);

    /**
     * @brief Cambiar texto del status label al pie
     */
    void setStatus(const QString& _status);

    /**
     * @brief Habilitar/deshabilitar input + botón (mientras se espera respuesta)
     */
    void setInputEnabled(bool _enabled);

    /**
     * @brief Limpiar visualmente el área de respuestas (al iniciar nueva conversación)
     */
    void clearConversation();

signals:
    /**
     * @brief El usuario envió un mensaje (click en botón o Enter en input)
     */
    void messageSubmitted(const QString& _text);

    /**
     * @brief El usuario pidió empezar una nueva conversación (olvidar contexto)
     */
    void newConversationRequested();

protected:
    void updateTranslations() override;
    void designSystemChangeEvent(DesignSystemChangeEvent* _event) override;

private:
    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace Ui
