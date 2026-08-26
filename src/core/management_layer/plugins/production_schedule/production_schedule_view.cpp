#include "production_schedule_view.h"

#include "draggable_scene_list.h"

#include <business_layer/model/abstract_model.h>
#include <business_layer/model/production/production_models.h>
#include <business_layer/model/production/production_storage.h>
#include <business_layer/model/screenplay/text/screenplay_text_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model_scene_item.h>
#include <business_layer/model/text/text_model_folder_item.h>
#include <business_layer/model/text/text_model_group_item.h>
#include <business_layer/templates/text_template.h>
#include <ui/design_system/design_system.h>

#include <QDate>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>


namespace Ui {

namespace {

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

QString dayLabel(const BusinessLayer::ShootingDay& _day, int _sceneCount)
{
    const QString datePart = _day.date.isValid()
        ? _day.date.toString(QStringLiteral("ddd dd MMM"))
        : QObject::tr("(sin fecha)");
    const QString locPart
        = _day.primaryLocation.isEmpty() ? QObject::tr("Sin location") : _day.primaryLocation;
    return QStringLiteral("%1 · %2 · %3 escenas").arg(datePart, locPart).arg(_sceneCount);
}

} // anonymous namespace


class ProductionScheduleView::Implementation
{
public:
    explicit Implementation(ProductionScheduleView* _q);

    ProductionScheduleView* q = nullptr;
    QPointer<BusinessLayer::ScreenplayTextModel> screenplayModel;
    QString projectKey;
    BusinessLayer::ProductionState state;
    QVector<BusinessLayer::ScreenplayTextModelSceneItem*> sceneCache;
    int selectedDayRow = -1;

    QLabel* titleLabel = nullptr;
    QSplitter* splitter = nullptr;

    // Col 1: shooting days
    QLabel* daysHeader = nullptr;
    QListWidget* daysList = nullptr;
    QPushButton* newDayButton = nullptr;
    QPushButton* deleteDayButton = nullptr;

    // Col 2: detalle del día (escenas asignadas)
    QLabel* dayDetailHeader = nullptr;
    DraggableSceneList* assignedScenesList = nullptr;
    QPushButton* unassignSceneButton = nullptr;

    // Col 3: boneyard
    QLabel* boneyardHeader = nullptr;
    DraggableSceneList* boneyardList = nullptr;
    QPushButton* assignSceneButton = nullptr;

    QLabel* statusLabel = nullptr;
};

ProductionScheduleView::Implementation::Implementation(ProductionScheduleView* _q)
    : q(_q)
    , titleLabel(new QLabel(_q))
    , splitter(new QSplitter(Qt::Horizontal, _q))
    , daysHeader(new QLabel(_q))
    , daysList(new QListWidget(_q))
    , newDayButton(new QPushButton(_q))
    , deleteDayButton(new QPushButton(_q))
    , dayDetailHeader(new QLabel(_q))
    , assignedScenesList(new DraggableSceneList(_q))
    , unassignSceneButton(new QPushButton(_q))
    , boneyardHeader(new QLabel(_q))
    , boneyardList(new DraggableSceneList(_q))
    , assignSceneButton(new QPushButton(_q))
    , statusLabel(new QLabel(_q))
{
    titleLabel->setText(QStringLiteral("Plan de rodaje"));
    titleLabel->setAlignment(Qt::AlignCenter);

    daysHeader->setText(QStringLiteral("Días de rodaje"));
    newDayButton->setText(QStringLiteral("Nuevo día"));
    deleteDayButton->setText(QStringLiteral("Eliminar día"));
    deleteDayButton->setEnabled(false);

    dayDetailHeader->setText(QStringLiteral("Selecciona un día"));
    unassignSceneButton->setText(QStringLiteral("← Boneyard"));
    unassignSceneButton->setEnabled(false);

    boneyardHeader->setText(QStringLiteral("Escenas sin asignar"));
    assignSceneButton->setText(QStringLiteral("→ Día"));
    assignSceneButton->setEnabled(false);

    statusLabel->setText(QString());
    statusLabel->setAlignment(Qt::AlignCenter);
}


// ****


ProductionScheduleView::ProductionScheduleView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    //
    // Col 1: shooting days
    //
    auto col1 = new QWidget(this);
    auto col1Layout = new QVBoxLayout(col1);
    col1Layout->setContentsMargins(0, 0, 0, 0);
    col1Layout->setSpacing(8);
    col1Layout->addWidget(d->daysHeader);
    col1Layout->addWidget(d->daysList, 1);
    auto col1Buttons = new QHBoxLayout;
    col1Buttons->setContentsMargins({});
    col1Buttons->addWidget(d->newDayButton);
    col1Buttons->addWidget(d->deleteDayButton);
    col1Buttons->addStretch();
    col1Layout->addLayout(col1Buttons);

    //
    // Col 2: detalle del día (escenas asignadas)
    //
    auto col2 = new QWidget(this);
    auto col2Layout = new QVBoxLayout(col2);
    col2Layout->setContentsMargins(0, 0, 0, 0);
    col2Layout->setSpacing(8);
    col2Layout->addWidget(d->dayDetailHeader);
    col2Layout->addWidget(d->assignedScenesList, 1);
    col2Layout->addWidget(d->unassignSceneButton);

    //
    // Col 3: boneyard
    //
    auto col3 = new QWidget(this);
    auto col3Layout = new QVBoxLayout(col3);
    col3Layout->setContentsMargins(0, 0, 0, 0);
    col3Layout->setSpacing(8);
    col3Layout->addWidget(d->boneyardHeader);
    col3Layout->addWidget(d->boneyardList, 1);
    col3Layout->addWidget(d->assignSceneButton);

    d->splitter->addWidget(col1);
    d->splitter->addWidget(col2);
    d->splitter->addWidget(col3);
    d->splitter->setStretchFactor(0, 2);
    d->splitter->setStretchFactor(1, 3);
    d->splitter->setStretchFactor(2, 3);

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addWidget(d->titleLabel);
    layout->addWidget(d->splitter, 1);
    layout->addWidget(d->statusLabel);
    setLayout(layout);

    //
    // Wiring
    //
    connect(d->daysList, &QListWidget::currentRowChanged, this,
            &ProductionScheduleView::onDaySelectionChanged);
    connect(d->newDayButton, &QPushButton::clicked, this,
            &ProductionScheduleView::onNewDayClicked);
    connect(d->deleteDayButton, &QPushButton::clicked, this,
            &ProductionScheduleView::onDeleteDayClicked);
    connect(d->assignSceneButton, &QPushButton::clicked, this,
            &ProductionScheduleView::onAssignSceneClicked);
    connect(d->unassignSceneButton, &QPushButton::clicked, this,
            &ProductionScheduleView::onUnassignSceneClicked);

    //
    // Habilitar/deshabilitar botones según selección
    //
    connect(d->boneyardList, &QListWidget::itemSelectionChanged, this, [this] {
        d->assignSceneButton->setEnabled(
            !d->boneyardList->selectedItems().isEmpty() && d->selectedDayRow >= 0);
    });
    connect(d->assignedScenesList, &QListWidget::itemSelectionChanged, this, [this] {
        d->unassignSceneButton->setEnabled(!d->assignedScenesList->selectedItems().isEmpty());
    });

    //
    // Drag&drop entre boneyard y día seleccionado
    //
    connect(d->assignedScenesList, &DraggableSceneList::scenesDropped, this,
            [this](const QVector<QUuid>& _uuids) {
                if (d->selectedDayRow < 0
                    || d->selectedDayRow >= d->state.shootingDays.size()) {
                    //
                    // Sin día seleccionado no se puede asignar; refrescar
                    // para revertir el drop visual
                    //
                    refreshRightPanel();
                    return;
                }
                auto& day = d->state.shootingDays[d->selectedDayRow];
                for (const auto& u : _uuids) {
                    if (!day.sceneUuids.contains(u)) {
                        day.sceneUuids.append(u);
                    }
                }
                saveState();
                refreshDaysList();
                d->daysList->setCurrentRow(d->selectedDayRow);
            });
    connect(d->boneyardList, &DraggableSceneList::scenesDropped, this,
            [this](const QVector<QUuid>& _uuids) {
                if (d->selectedDayRow < 0
                    || d->selectedDayRow >= d->state.shootingDays.size()) {
                    return;
                }
                auto& day = d->state.shootingDays[d->selectedDayRow];
                for (const auto& u : _uuids) {
                    day.sceneUuids.removeAll(u);
                }
                saveState();
                refreshDaysList();
                d->daysList->setCurrentRow(d->selectedDayRow);
            });
}

ProductionScheduleView::~ProductionScheduleView() = default;

QWidget* ProductionScheduleView::asQWidget()
{
    return this;
}

void ProductionScheduleView::setEditingMode(ManagementLayer::DocumentEditingMode _mode)
{
    Q_UNUSED(_mode)
}

void ProductionScheduleView::setScreenplayModel(BusinessLayer::AbstractModel* _model)
{
    d->screenplayModel = qobject_cast<BusinessLayer::ScreenplayTextModel*>(_model);
    d->sceneCache.clear();
    d->state = BusinessLayer::ProductionState();
    d->projectKey.clear();
    d->selectedDayRow = -1;

    if (d->screenplayModel.isNull()) {
        d->statusLabel->setText(
            tr("Abre un proyecto de guion para empezar a planear el rodaje."));
        d->daysList->clear();
        d->boneyardList->clear();
        d->assignedScenesList->clear();
        return;
    }

    collectScenes(d->screenplayModel->itemForIndex(QModelIndex()), d->sceneCache);

    d->projectKey = d->screenplayModel->documentName();
    if (d->projectKey.isEmpty()) {
        d->projectKey = QStringLiteral("Sin nombre");
    }
    d->state = BusinessLayer::ProductionStorage::load(d->projectKey);

    refreshDaysList();
    refreshRightPanel();

    d->statusLabel->setText(tr("%1 escenas · %2 días planeados · "
                               "arrastra entre columnas para asignar/quitar")
                                .arg(d->sceneCache.size())
                                .arg(d->state.shootingDays.size()));
}

void ProductionScheduleView::refreshDaysList()
{
    d->daysList->clear();
    for (const auto& day : d->state.shootingDays) {
        auto* item = new QListWidgetItem(dayLabel(day, day.sceneUuids.size()));
        item->setData(Qt::UserRole, day.uuid);
        d->daysList->addItem(item);
    }
}

void ProductionScheduleView::refreshRightPanel()
{
    d->boneyardList->clear();
    d->assignedScenesList->clear();

    if (d->screenplayModel.isNull()) {
        d->dayDetailHeader->setText(tr("Selecciona un día"));
        d->boneyardHeader->setText(tr("Escenas sin asignar"));
        return;
    }

    //
    // Boneyard (siempre visible)
    //
    const auto assignedScenes = d->state.assignedScenes();
    int idx = 1;
    int countUnassigned = 0;
    for (auto* scene : d->sceneCache) {
        if (scene == nullptr) {
            ++idx;
            continue;
        }
        if (!assignedScenes.contains(scene->uuid())) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1. %2").arg(idx).arg(scene->heading()));
            item->setData(Qt::UserRole, scene->uuid());
            d->boneyardList->addItem(item);
            ++countUnassigned;
        }
        ++idx;
    }
    d->boneyardHeader->setText(tr("Escenas sin asignar (%1)").arg(countUnassigned));

    //
    // Detalle del día seleccionado (puede estar vacío)
    //
    if (d->selectedDayRow < 0 || d->selectedDayRow >= d->state.shootingDays.size()) {
        d->dayDetailHeader->setText(tr("Selecciona un día →"));
        return;
    }
    const auto& day = d->state.shootingDays[d->selectedDayRow];
    d->dayDetailHeader->setText(dayLabel(day, day.sceneUuids.size()));

    const QSet<QUuid> dayScenes(day.sceneUuids.begin(), day.sceneUuids.end());
    idx = 1;
    for (auto* scene : d->sceneCache) {
        if (scene == nullptr) {
            ++idx;
            continue;
        }
        if (dayScenes.contains(scene->uuid())) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1. %2").arg(idx).arg(scene->heading()));
            item->setData(Qt::UserRole, scene->uuid());
            d->assignedScenesList->addItem(item);
        }
        ++idx;
    }
}

void ProductionScheduleView::onDaySelectionChanged(int _row)
{
    d->selectedDayRow = _row;
    d->deleteDayButton->setEnabled(_row >= 0);
    refreshRightPanel();
}

void ProductionScheduleView::onNewDayClicked()
{
    bool ok = false;
    const QString location = QInputDialog::getText(
        this, tr("Nuevo día de rodaje"),
        tr("Location principal (opcional):"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    BusinessLayer::ShootingDay day;
    day.uuid = QUuid::createUuid();
    day.date = QDate::currentDate();
    day.primaryLocation = location.trimmed();
    d->state.shootingDays.append(day);
    saveState();
    refreshDaysList();
    d->daysList->setCurrentRow(d->state.shootingDays.size() - 1);
}

void ProductionScheduleView::onDeleteDayClicked()
{
    if (d->selectedDayRow < 0 || d->selectedDayRow >= d->state.shootingDays.size()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Eliminar día de rodaje"),
        tr("¿Eliminar este día? Las escenas asignadas vuelven al boneyard."));
    if (answer != QMessageBox::Yes) {
        return;
    }
    d->state.shootingDays.removeAt(d->selectedDayRow);
    saveState();
    d->selectedDayRow = -1;
    refreshDaysList();
    refreshRightPanel();
}

void ProductionScheduleView::onAssignSceneClicked()
{
    if (d->selectedDayRow < 0 || d->selectedDayRow >= d->state.shootingDays.size()) {
        return;
    }
    const auto selected = d->boneyardList->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    auto& day = d->state.shootingDays[d->selectedDayRow];
    for (auto* it : selected) {
        const QUuid u = it->data(Qt::UserRole).toUuid();
        if (!u.isNull() && !day.sceneUuids.contains(u)) {
            day.sceneUuids.append(u);
        }
    }
    saveState();
    refreshDaysList();
    d->daysList->setCurrentRow(d->selectedDayRow);
}

void ProductionScheduleView::onUnassignSceneClicked()
{
    if (d->selectedDayRow < 0 || d->selectedDayRow >= d->state.shootingDays.size()) {
        return;
    }
    const auto selected = d->assignedScenesList->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    auto& day = d->state.shootingDays[d->selectedDayRow];
    for (auto* it : selected) {
        const QUuid u = it->data(Qt::UserRole).toUuid();
        day.sceneUuids.removeAll(u);
    }
    saveState();
    refreshDaysList();
    d->daysList->setCurrentRow(d->selectedDayRow);
}

void ProductionScheduleView::saveState() const
{
    if (d->projectKey.isEmpty()) {
        return;
    }
    if (!BusinessLayer::ProductionStorage::save(d->projectKey, d->state)) {
        d->statusLabel->setText(tr("⚠ No se pudo guardar el plan de rodaje"));
    }
}

void ProductionScheduleView::updateTranslations()
{
    d->titleLabel->setText(tr("Plan de rodaje"));
    d->daysHeader->setText(tr("Días de rodaje"));
    d->newDayButton->setText(tr("Nuevo día"));
    d->deleteDayButton->setText(tr("Eliminar día"));
    d->assignSceneButton->setText(tr("→ Día"));
    d->unassignSceneButton->setText(tr("← Boneyard"));
}

void ProductionScheduleView::designSystemChangeEvent(DesignSystemChangeEvent* _event)
{
    Widget::designSystemChangeEvent(_event);

    setBackgroundColor(DesignSystem::color().surface());
    const auto bodyColor = DesignSystem::color().onSurface().name();

    d->titleLabel->setFont(DesignSystem::font().h6());
    d->titleLabel->setStyleSheet(QString("color: %1;").arg(bodyColor));

    for (auto* l : { d->daysHeader, d->boneyardHeader, d->dayDetailHeader }) {
        l->setFont(DesignSystem::font().subtitle2());
        l->setStyleSheet(QString("color: %1;").arg(bodyColor));
    }

    const QString listSs
        = QString("QListWidget { color: %1; background: %2; border: 1px solid %3; }")
              .arg(bodyColor,
                   DesignSystem::color().background().name(),
                   DesignSystem::color().onBackground().name());
    for (auto* lw : { static_cast<QListWidget*>(d->daysList),
                      static_cast<QListWidget*>(d->boneyardList),
                      static_cast<QListWidget*>(d->assignedScenesList) }) {
        lw->setFont(DesignSystem::font().body2());
        lw->setStyleSheet(listSs);
    }

    for (auto* b : { d->newDayButton, d->deleteDayButton, d->assignSceneButton,
                     d->unassignSceneButton }) {
        b->setFont(DesignSystem::font().button());
    }

    d->statusLabel->setFont(DesignSystem::font().caption());
    d->statusLabel->setStyleSheet(QString("color: %1;").arg(bodyColor));
}

} // namespace Ui
