#pragma once

#include <interfaces/ui/i_document_view.h>
#include <ui/widgets/widget/widget.h>


namespace Ui {

/**
 * @brief Vista del asistente de escritura — panel con input + respuestas
 *        de Claude.
 *
 * Iteración 1 (mínima): muestra un texto estático para validar que el
 * plugin carga y aparece en la UI de Diez50.
 *
 * Iteración 2 (futuro): añadir QLineEdit + QPushButton + QTextEdit con
 * conexión a la API de Anthropic vía QNetworkAccessManager.
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

protected:
    /**
     * @brief Actualizar traducciones (texto del UI)
     */
    void updateTranslations() override;

    /**
     * @brief Reaccionar a cambios del design system de STARC
     */
    void designSystemChangeEvent(DesignSystemChangeEvent* _event) override;

private:
    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace Ui
