#pragma once

#include <interfaces/ui/i_document_view.h>
#include <ui/widgets/widget/widget.h>


namespace BusinessLayer {
class AbstractModel;
class ScreenplayTextModel;
class ScreenplayTextModelSceneItem;
}


namespace Ui {

/**
 * @brief Vista del Script Breakdown nativo de Aula 122 (Bloque 5).
 *
 * Layout split:
 *   - Izquierda: tabla con todas las escenas del guion (#, heading, # recursos)
 *   - Derecha: panel con los recursos de la escena seleccionada + botones
 *     para añadir/quitar
 *
 * Persistencia: las llamadas a ScreenplayTextModelSceneItem::storeResource /
 * removeResource modifican el modelo y se guardan en el .starc al hacer save.
 *
 * Etapas siguientes (5.C-5.F) añadirán categorías color-coded, export PDF/CSV,
 * y un botón "Auto-extract con Claude" que aproveche el Bloque 3.
 */
class ScreenplayBreakdownNativeView : public Widget, public IDocumentView
{
    Q_OBJECT

public:
    explicit ScreenplayBreakdownNativeView(QWidget* _parent = nullptr);
    ~ScreenplayBreakdownNativeView() override;

    QWidget* asQWidget() override;
    void setEditingMode(ManagementLayer::DocumentEditingMode _mode) override;

    /**
     * @brief Cargar (o recargar) la vista a partir del modelo de guion.
     */
    void setScreenplayModel(BusinessLayer::AbstractModel* _model);

protected:
    void updateTranslations() override;
    void designSystemChangeEvent(DesignSystemChangeEvent* _event) override;

private:
    void onSceneSelectionChanged(int _row);
    void onAddResourceClicked();
    void onRemoveResourceClicked();
    void onExportClicked();
    void onAutoExtractClicked();
    void applyAutoExtractedCsv(const QString& _csv);
    void exportToCsv(const QString& _filePath) const;
    void exportToPdf(const QString& _filePath) const;
    void refreshSceneTable();
    void refreshResourcesList();

    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace Ui
