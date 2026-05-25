#include "draggable_scene_list.h"

#include <QDropEvent>


namespace Ui {

DraggableSceneList::DraggableSceneList(QWidget* _parent)
    : QListWidget(_parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
}

void DraggableSceneList::dropEvent(QDropEvent* _event)
{
    QListWidget* source = qobject_cast<QListWidget*>(_event->source());
    const bool fromOther = source != nullptr && source != this;

    QVector<QUuid> droppedUuids;
    if (fromOther) {
        const auto selected = source->selectedItems();
        droppedUuids.reserve(selected.size());
        for (auto* it : selected) {
            const QUuid u = it->data(Qt::UserRole).toUuid();
            if (!u.isNull()) {
                droppedUuids.append(u);
            }
        }
    }

    QListWidget::dropEvent(_event);

    if (fromOther && !droppedUuids.isEmpty()) {
        emit scenesDropped(droppedUuids);
    }
}

} // namespace Ui
