#pragma once

#include <QListWidget>
#include <QUuid>
#include <QVector>


namespace Ui {

/**
 * @brief QListWidget de escenas con drop event interceptable.
 *
 * Activa drag&drop entre listas (DragDrop + MoveAction). Cuando recibe
 * items desde OTRO QListWidget (no reordenamiento interno), emite
 * scenesDropped(uuids) tras procesar el drop. El receptor actualiza
 * su modelo de datos.
 *
 * Los items deben tener su UUID en Qt::UserRole.
 */
class DraggableSceneList : public QListWidget
{
    Q_OBJECT

public:
    explicit DraggableSceneList(QWidget* _parent = nullptr);

signals:
    /**
     * @brief Emitido cuando esta lista recibe drops desde otra lista del mismo
     *        tipo. _uuids contiene los uuids de los items recibidos.
     */
    void scenesDropped(const QVector<QUuid>& _uuids);

protected:
    void dropEvent(QDropEvent* _event) override;
};

} // namespace Ui
