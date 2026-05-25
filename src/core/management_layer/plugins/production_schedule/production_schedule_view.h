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
 * @brief Plan de rodaje (Strip Board) — etapa 7.A.
 *
 * Layout split horizontal:
 *   - Izquierda: lista de Shooting Days. Cada item muestra:
 *       fecha + location + # escenas asignadas.
 *     Botones [Nuevo día] [Eliminar día]
 *   - Derecha: detalle del día seleccionado o boneyard
 *     Si hay día seleccionado: lista de escenas del día + botones
 *       [Asignar escena] [Quitar escena del día]
 *     Si NO hay día seleccionado: lista de escenas todavía no asignadas
 *       (el boneyard).
 *
 * Persistencia: ProductionStorage::save/load contra
 *   ~/Documents/Aula 122/projects/<projectName>/production.json
 *
 * Etapas siguientes (7.B/C): drag&drop real entre días + boneyard,
 * crew manager, call sheets.
 */
class ProductionScheduleView : public Widget, public IDocumentView
{
    Q_OBJECT

public:
    explicit ProductionScheduleView(QWidget* _parent = nullptr);
    ~ProductionScheduleView() override;

    QWidget* asQWidget() override;
    void setEditingMode(ManagementLayer::DocumentEditingMode _mode) override;

    /**
     * @brief Cargar el modelo del guion. Si es un ScreenplayTextModel
     *        válido, también carga el ProductionState desde JSON.
     */
    void setScreenplayModel(BusinessLayer::AbstractModel* _model);

protected:
    void updateTranslations() override;
    void designSystemChangeEvent(DesignSystemChangeEvent* _event) override;

private:
    void onDaySelectionChanged(int _row);
    void onNewDayClicked();
    void onDeleteDayClicked();
    void onAssignSceneClicked();
    void onUnassignSceneClicked();
    void refreshDaysList();
    void refreshRightPanel();
    void saveState() const;

    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace Ui
