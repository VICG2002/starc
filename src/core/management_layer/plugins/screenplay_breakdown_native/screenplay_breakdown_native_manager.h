#pragma once

#include "screenplay_breakdown_native_view.h"

#include <interfaces/management_layer/i_document_manager.h>

#include <QObject>
#include <QScopedPointer>


namespace ManagementLayer {

/**
 * @brief Plugin nativo (open source) de Script Breakdown para Aula 122.
 *
 * Reemplaza/complementa al plugin closed source `screenplay_breakdown`
 * copiado del Story Architect oficial. Lee del `ScreenplayTextModel` la
 * lista de escenas y los `BreakdownSceneResource` ya registrados (modelo
 * abierto en `corelib/business_layer/model/screenplay/screenplay_dictionaries_model.h`).
 *
 * Fase 5.A: solo lista escenas con su heading. Etapas siguientes (5.B-5.F)
 * añadirán tagging, categorías color-coded, export PDF, auto-extract IA.
 */
class ScreenplayBreakdownNativeManager : public QObject, public IDocumentManager
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "app.starc.ManagementLayer.IDocumentManager")
    Q_INTERFACES(ManagementLayer::IDocumentManager)

public:
    explicit ScreenplayBreakdownNativeManager(QObject* _parent = nullptr);
    ~ScreenplayBreakdownNativeManager() override;

    Ui::IDocumentView* view() override;
    Ui::IDocumentView* view(BusinessLayer::AbstractModel* _model) override;
    Ui::IDocumentView* secondaryView() override;
    Ui::IDocumentView* secondaryView(BusinessLayer::AbstractModel* _model) override;
    Ui::IDocumentView* createView(BusinessLayer::AbstractModel* _model) override;
    void resetModels() override;
    void setEditingMode(DocumentEditingMode _mode) override;

private:
    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace ManagementLayer
