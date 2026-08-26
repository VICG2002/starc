#pragma once

#include "production_schedule_view.h"

#include <interfaces/management_layer/i_document_manager.h>

#include <QObject>
#include <QScopedPointer>


namespace ManagementLayer {

/**
 * @brief Plugin de Plan de rodaje (Strip Board + Crew + Call Sheets).
 *
 * Bloque 7 del plan. Consume:
 *   - ScreenplayTextModel del proyecto (para escenas)
 *   - ProductionState desde ~/Documents/Aula 122/projects/<proj>/production.json
 *
 * Etapa 7.A: lista de shooting days + boneyard de escenas no asignadas,
 * crear día nuevo, asignar/quitar escena de un día. Sin drag&drop aún
 * (botones). Persiste a JSON al cambiar.
 */
class ProductionScheduleManager : public QObject, public IDocumentManager
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "app.starc.ManagementLayer.IDocumentManager")
    Q_INTERFACES(ManagementLayer::IDocumentManager)

public:
    explicit ProductionScheduleManager(QObject* _parent = nullptr);
    ~ProductionScheduleManager() override;

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
