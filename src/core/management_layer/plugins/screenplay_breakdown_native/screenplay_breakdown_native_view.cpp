#include "screenplay_breakdown_native_view.h"

#include <business_layer/model/abstract_model.h>
#include <business_layer/model/screenplay/screenplay_dictionaries_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model_scene_item.h>
#include <business_layer/model/text/text_model_folder_item.h>
#include <business_layer/model/text/text_model_group_item.h>
#include <business_layer/templates/text_template.h>
#include <ui/design_system/design_system.h>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>


namespace Ui {

class ScreenplayBreakdownNativeView::Implementation
{
public:
    explicit Implementation(QWidget* _parent);

    QLabel* titleLabel = nullptr;
    QTableView* sceneTable = nullptr;
    QStandardItemModel* sceneModel = nullptr;
    QLabel* statusLabel = nullptr;
};

ScreenplayBreakdownNativeView::Implementation::Implementation(QWidget* _parent)
    : titleLabel(new QLabel(_parent))
    , sceneTable(new QTableView(_parent))
    , sceneModel(new QStandardItemModel(_parent))
    , statusLabel(new QLabel(_parent))
{
    titleLabel->setText(QStringLiteral("Desglose del guion"));
    titleLabel->setAlignment(Qt::AlignCenter);

    sceneModel->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        QStringLiteral("Heading"),
        QStringLiteral("Recursos"),
    });
    sceneTable->setModel(sceneModel);
    sceneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sceneTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    sceneTable->setAlternatingRowColors(true);
    sceneTable->horizontalHeader()->setStretchLastSection(true);
    sceneTable->verticalHeader()->setVisible(false);

    statusLabel->setText(QString());
    statusLabel->setAlignment(Qt::AlignCenter);
}


// ****


ScreenplayBreakdownNativeView::ScreenplayBreakdownNativeView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    auto layout = new QVBoxLayout;
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addWidget(d->titleLabel);
    layout->addWidget(d->sceneTable, 1);
    layout->addWidget(d->statusLabel);
    setLayout(layout);
}

ScreenplayBreakdownNativeView::~ScreenplayBreakdownNativeView() = default;

QWidget* ScreenplayBreakdownNativeView::asQWidget()
{
    return this;
}

void ScreenplayBreakdownNativeView::setEditingMode(ManagementLayer::DocumentEditingMode _mode)
{
    Q_UNUSED(_mode)
}

namespace {

/**
 * @brief Recorre recursivamente el árbol del modelo recolectando todas
 *        las escenas (TextModelGroupItem cuyo subtype es Scene).
 */
void collectScenes(BusinessLayer::TextModelItem* _item,
                   QVector<BusinessLayer::ScreenplayTextModelSceneItem*>& _out)
{
    if (_item == nullptr) {
        return;
    }
    for (int i = 0; i < _item->childCount(); ++i) {
        auto* child = _item->childAt(i);
        if (child->type() == BusinessLayer::TextModelItemType::Group) {
            auto* group = static_cast<BusinessLayer::TextModelGroupItem*>(child);
            if (group->groupType() == BusinessLayer::TextGroupType::Scene) {
                _out.append(static_cast<BusinessLayer::ScreenplayTextModelSceneItem*>(group));
            }
            collectScenes(child, _out);
        } else if (child->type() == BusinessLayer::TextModelItemType::Folder) {
            collectScenes(child, _out);
        }
    }
}

} // anonymous namespace

void ScreenplayBreakdownNativeView::setScreenplayModel(BusinessLayer::AbstractModel* _model)
{
    d->sceneModel->removeRows(0, d->sceneModel->rowCount());

    auto* screenplay = qobject_cast<BusinessLayer::ScreenplayTextModel*>(_model);
    if (screenplay == nullptr) {
        d->statusLabel->setText(
            tr("Abre un proyecto de guion para ver el desglose de escenas."));
        return;
    }

    QVector<BusinessLayer::ScreenplayTextModelSceneItem*> scenes;
    collectScenes(screenplay->itemForIndex(QModelIndex()), scenes);

    int idx = 1;
    for (auto* scene : scenes) {
        if (scene == nullptr) {
            continue;
        }
        auto* numberItem = new QStandardItem(QString::number(idx++));
        auto* headingItem = new QStandardItem(scene->heading());
        auto* resourcesItem = new QStandardItem(
            QString::number(scene->resources().size()));
        d->sceneModel->appendRow({ numberItem, headingItem, resourcesItem });
    }

    d->statusLabel->setText(
        tr("%1 escenas en el guion. Etapas siguientes habilitan tagging "
           "y export.").arg(scenes.size()));
}

void ScreenplayBreakdownNativeView::updateTranslations()
{
    d->titleLabel->setText(tr("Desglose del guion"));
    d->sceneModel->setHorizontalHeaderLabels({
        tr("#"),
        tr("Heading"),
        tr("Recursos"),
    });
}

void ScreenplayBreakdownNativeView::designSystemChangeEvent(DesignSystemChangeEvent* _event)
{
    Widget::designSystemChangeEvent(_event);

    setBackgroundColor(DesignSystem::color().surface());

    const auto bodyColor = DesignSystem::color().onSurface().name();

    d->titleLabel->setFont(DesignSystem::font().h6());
    d->titleLabel->setStyleSheet(QString("color: %1;").arg(bodyColor));

    d->sceneTable->setFont(DesignSystem::font().body2());
    d->sceneTable->setStyleSheet(
        QString("QTableView { color: %1; background: %2; gridline-color: %3; }"
                "QHeaderView::section { background: %2; color: %1; padding: 4px; }")
            .arg(bodyColor,
                 DesignSystem::color().background().name(),
                 DesignSystem::color().onBackground().name()));

    d->statusLabel->setFont(DesignSystem::font().caption());
    d->statusLabel->setStyleSheet(
        QString("color: %1;").arg(DesignSystem::color().onSurface().name()));
}

} // namespace Ui
