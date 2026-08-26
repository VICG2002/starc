#pragma once

#include <interfaces/ui/i_document_view.h>
#include <ui/widgets/widget/widget.h>


namespace BusinessLayer {
class AbstractModel;
class ScreenplayStatisticsModel;
}


namespace Ui {

/**
 * @brief Vista de Estadísticas de guion nativa de Aula 122.
 *
 * Reemplaza al plugin closed source `screenplay_statistics`. Todo el cálculo
 * (los 6 reportes + 2 gráficas de `BusinessLayer::ScreenplayStatisticsModel`)
 * ya vivía en corelib como código abierto; esta vista solo lo dibuja.
 *
 * Selección de qué mostrar: la controla el navegador lateral
 * `screenplay_statistics_structure`, ya existente, vía
 * `setCurrentReport`/`setCurrentPlot` (el manager las conecta en `bind()`).
 * Los índices son los que ya fija ese navegador:
 *   Reportes — 0 Resumen, 1 Escenas, 2 Locaciones, 3 Personajes,
 *              4 Diálogos, 5 Género
 *   Gráficas — 0 Análisis de estructura, 1 Actividad de personajes
 */
class ScreenplayStatisticsNativeView : public Widget, public IDocumentView
{
    Q_OBJECT

public:
    explicit ScreenplayStatisticsNativeView(QWidget* _parent = nullptr);
    ~ScreenplayStatisticsNativeView() override;

    QWidget* asQWidget() override;
    void setEditingMode(ManagementLayer::DocumentEditingMode _mode) override;

    /**
     * @brief Cargar (o recargar) la vista a partir del modelo de estadísticas
     */
    void setStatisticsModel(BusinessLayer::AbstractModel* _model);

    /**
     * @brief Mostrar el reporte o la gráfica con el índice dado (ver arriba)
     */
    void setCurrentReport(int _index);
    void setCurrentPlot(int _index);

protected:
    void updateTranslations() override;
    void designSystemChangeEvent(DesignSystemChangeEvent* _event) override;

private:
    void rebuildAll();
    void refreshCurrentPage();
    void onExportClicked();

    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace Ui
