#pragma once

#include <interfaces/ui/i_document_view.h>
#include <ui/widgets/widget/widget.h>


namespace BusinessLayer {
class AbstractModel;
}


namespace Ui {

/**
 * @brief Vista del Script Breakdown nativo.
 *
 * Layout (Fase 5.A):
 *   - Título "Desglose del guion"
 *   - Tabla (QTableView) con columnas: # escena, Heading, Recursos
 *   - Mensaje de estado al pie
 *
 * Etapas siguientes añadirán: edición inline de recursos, categorías
 * color-coded, panel de filtros, botón "Auto-extract con Claude",
 * botón "Export PDF/CSV".
 */
class ScreenplayBreakdownNativeView : public Widget, public IDocumentView
{
    Q_OBJECT

public:
    explicit ScreenplayBreakdownNativeView(QWidget* _parent = nullptr);
    ~ScreenplayBreakdownNativeView() override;

    /**
     * @brief Implementación de IDocumentView
     */
    /** @{ */
    QWidget* asQWidget() override;
    void setEditingMode(ManagementLayer::DocumentEditingMode _mode) override;
    /** @} */

    /**
     * @brief Cargar (o recargar) la vista a partir del modelo de guion.
     *        Si _model es null o no es ScreenplayTextModel, muestra
     *        mensaje de "abre un proyecto de guion".
     */
    void setScreenplayModel(BusinessLayer::AbstractModel* _model);

protected:
    void updateTranslations() override;
    void designSystemChangeEvent(DesignSystemChangeEvent* _event) override;

private:
    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace Ui
