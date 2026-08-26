#pragma once

#include "screenplay_statistics_native_view.h"

#include <interfaces/management_layer/i_document_manager.h>

#include <QObject>
#include <QScopedPointer>


namespace ManagementLayer {

/**
 * @brief Plugin nativo (open source) de Estadísticas de guion para Aula 122.
 *
 * Reemplaza al plugin closed source `screenplay_statistics`, cuyo submódulo
 * privado (story-apps) no está clonado en este checkout — solo quedaba un
 * `.dylib` heredado de un clon anterior, no reconstruible.
 *
 * Todo el cálculo (los 6 reportes + 2 gráficas) ya vivía en corelib, open
 * source: este plugin es solo la capa de vista que faltaba, siguiendo el
 * mismo patrón que `screenplay_breakdown_native`.
 *
 * El navegador lateral (`screenplay_statistics_structure`, ya en el repo)
 * emite `currentReportIndexChanged`/`currentPlotIndexChanged`; este manager
 * las escucha vía `bind()` y las reenvía a la vista.
 */
class ScreenplayStatisticsNativeManager : public QObject, public IDocumentManager
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "app.starc.ManagementLayer.IDocumentManager")
    Q_INTERFACES(ManagementLayer::IDocumentManager)

public:
    explicit ScreenplayStatisticsNativeManager(QObject* _parent = nullptr);
    ~ScreenplayStatisticsNativeManager() override;

    QObject* asQObject() override;
    Ui::IDocumentView* view() override;
    Ui::IDocumentView* view(BusinessLayer::AbstractModel* _model) override;
    Ui::IDocumentView* secondaryView() override;
    Ui::IDocumentView* secondaryView(BusinessLayer::AbstractModel* _model) override;
    Ui::IDocumentView* createView(BusinessLayer::AbstractModel* _model) override;
    void resetModels() override;
    void bind(IDocumentManager* _manager) override;

private slots:
    /**
     * @brief El navegador de estadísticas seleccionó un reporte/gráfica.
     *        Slots (no métodos normales): bind() los conecta por firma de
     *        texto vía SIGNAL/SLOT, ya que el navegador es un plugin aparte
     *        y no conviene enlazar contra su tipo concreto.
     */
    void setCurrentReportIndex(const QModelIndex& _index);
    void setCurrentPlotIndex(const QModelIndex& _index);

private:
    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace ManagementLayer
