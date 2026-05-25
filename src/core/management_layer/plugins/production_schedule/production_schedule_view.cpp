#include "production_schedule_view.h"

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
#include <QStackedWidget>
#include <QVBoxLayout>


namespace Ui {

class ProductionScheduleView::Implementation
{
public:
    explicit Implementation(ProductionScheduleView* _q);

    ProductionScheduleView* q = nullptr;
    QPointer<BusinessLayer::ScreenplayTextModel> screenplayModel;
    QString projectKey; // nombre del proyecto (documentName del modelo)
    BusinessLayer::ProductionState state;

    // Cache de escenas del guion (idx en orden de aparición)
    QVector<BusinessLayer::ScreenplayTextModelSceneItem*> sceneCache;

    int selectedDayRow = -1;

    QLabel* titleLabel = nullptr;
    QSplitter* splitter = nullptr;

    // Panel izquierdo: shooting days
    QLabel* daysHeader = nullptr;
    QListWidget* daysList = nullptr;
    QPushButton* newDayButton = nullptr;
    QPushButton* deleteDayButton = nullptr;

    // Panel derecho: stack (boneyard / day detail)
    QStackedWidget* rightStack = nullptr;
    QWidget* boneyardPage = nullptr;
    QWidget* dayDetailPage = nullptr;

    // Boneyard
    QLabel* boneyardHeader = nullptr;
    QListWidget* boneyardList = nullptr;
    QPushButton* assignSceneButton = nullptr;

    // Day detail
    QLabel* dayDetailHeader = nullptr;
    QListWidget* assignedScenesList = nullptr;
    QPushButton* unassignSceneButton = nullptr;

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
    , rightStack(new QStackedWidget(_q))
    , boneyardPage(new QWidget(_q))
    , dayDetailPage(new QWidget(_q))
    , boneyardHeader(new QLabel(_q))
    , boneyardList(new QListWidget(_q))
    , assignSceneButton(new QPushButton(_q))
    , dayDetailHeader(new QLabel(_q))
    , assignedScenesList(new QListWidget(_q))
    , unassignSceneButton(new QPushButton(_q))
    , statusLabel(new QLabel(_q))
{
    titleLabel->setText(QStringLiteral("Plan de rodaje"));
    titleLabel->setAlignment(Qt::AlignCenter);

    daysHeader->setText(QStringLiteral("Días de rodaje"));
    newDayButton->setText(QStringLiteral("Nuevo día"));
    deleteDayButton->setText(QStringLiteral("Eliminar día"));
    deleteDayButton->setEnabled(false);

    boneyardHeader->setText(QStringLiteral("Escenas sin asignar"));
    assignSceneButton->setText(QStringLiteral("Asignar al día seleccionado"));
    assignSceneButton->setEnabled(false);

    dayDetailHeader->setText(QStringLiteral("Selecciona un día"));
    unassignSceneButton->setText(QStringLiteral("Quitar del día"));
    unassignSceneButton->setEnabled(false);

    statusLabel->setText(QString());
    statusLabel->setAlignment(Qt::AlignCenter);
}


// ****


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


ProductionScheduleView::ProductionScheduleView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    //
    // Panel izquierdo
    //
    auto leftWidget = new QWidget(this);
    auto leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(d->daysHeader);
    leftLayout->addWidget(d->daysList, 1);
    auto leftButtons = new QHBoxLayout;
    leftButtons->setContentsMargins({});
    leftButtons->addWidget(d->newDayButton);
    leftButtons->addWidget(d->deleteDayButton);
    leftButtons->addStretch();
    leftLayout->addLayout(leftButtons);

    //
    // Panel derecho — boneyard page
    //
    {
        auto layout = new QVBoxLayout(d->boneyardPage);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(d->boneyardHeader);
        layout->addWidget(d->boneyardList, 1);
        layout->addWidget(d->assignSceneButton);
    }

    //
    // Panel derecho — day detail page
    //
    {
        auto layout = new QVBoxLayout(d->dayDetailPage);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(d->dayDetailHeader);
        layout->addWidget(d->assignedScenesList, 1);
        layout->addWidget(d->unassignSceneButton);
    }

    d->rightStack->addWidget(d->boneyardPage);
    d->rightStack->addWidget(d->dayDetailPage);
    d->rightStack->setCurrentWidget(d->boneyardPage);

    d->splitter->addWidget(leftWidget);
    d->splitter->addWidget(d->rightStack);
    d->splitter->setStretchFactor(0, 1);
    d->splitter->setStretchFactor(1, 1);

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
    connect(d->boneyardList, &QListWidget::itemSelectionChanged, this, [this] {
        d->assignSceneButton->setEnabled(d->boneyardList->currentItem() != nullptr
                                         && d->selectedDayRow >= 0);
    });
    connect(d->assignedScenesList, &QListWidget::itemSelectionChanged, this, [this] {
        d->unassignSceneButton->setEnabled(d->assignedScenesList->currentItem() != nullptr);
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

    //
    // Usamos documentName como clave del proyecto para localizar el JSON.
    // No tenemos acceso directo al path del .starc desde el modelo.
    //
    d->projectKey = d->screenplayModel->documentName();
    if (d->projectKey.isEmpty()) {
        d->projectKey = QStringLiteral("Sin nombre");
    }
    d->state = BusinessLayer::ProductionStorage::load(d->projectKey);

    refreshDaysList();
    refreshRightPanel();

    d->statusLabel->setText(tr("%1 escenas · %2 días planeados")
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
        d->rightStack->setCurrentWidget(d->boneyardPage);
        d->dayDetailHeader->setText(tr("Selecciona un día"));
        d->boneyardHeader->setText(tr("Escenas sin asignar"));
        return;
    }

    if (d->selectedDayRow < 0 || d->selectedDayRow >= d->state.shootingDays.size()) {
        //
        // Mostrar boneyard
        //
        d->rightStack->setCurrentWidget(d->boneyardPage);
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
        d->boneyardHeader->setText(
            tr("Escenas sin asignar (%1)").arg(countUnassigned));
        d->assignSceneButton->setEnabled(false); // sin día seleccionado, no se puede asignar
    } else {
        //
        // Mostrar detalle del día
        //
        d->rightStack->setCurrentWidget(d->dayDetailPage);
        const auto& day = d->state.shootingDays[d->selectedDayRow];
        d->dayDetailHeader->setText(dayLabel(day, day.sceneUuids.size()));
        //
        // Listar escenas del día en orden del guion
        //
        const QSet<QUuid> dayScenes(day.sceneUuids.begin(), day.sceneUuids.end());
        int idx = 1;
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
}

void ProductionScheduleView::onDaySelectionChanged(int _row)
{
    d->selectedDayRow = _row;
    d->deleteDayButton->setEnabled(_row >= 0);
    refreshRightPanel();
    //
    // Si volvemos a deseleccionar el día (no debería pasar con SingleSelection,
    // pero por completitud), también refrescar el panel
    //
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
    //
    // Seleccionar el día recién creado
    //
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
    auto* current = d->boneyardList->currentItem();
    if (current == nullptr) {
        return;
    }
    const QUuid sceneUuid = current->data(Qt::UserRole).toUuid();
    auto& day = d->state.shootingDays[d->selectedDayRow];
    if (!day.sceneUuids.contains(sceneUuid)) {
        day.sceneUuids.append(sceneUuid);
        saveState();
        refreshDaysList();
        //
        // Re-seleccionar el día actual (refreshDaysList recreó los items)
        //
        d->daysList->setCurrentRow(d->selectedDayRow);
    }
}

void ProductionScheduleView::onUnassignSceneClicked()
{
    if (d->selectedDayRow < 0 || d->selectedDayRow >= d->state.shootingDays.size()) {
        return;
    }
    auto* current = d->assignedScenesList->currentItem();
    if (current == nullptr) {
        return;
    }
    const QUuid sceneUuid = current->data(Qt::UserRole).toUuid();
    auto& day = d->state.shootingDays[d->selectedDayRow];
    day.sceneUuids.removeAll(sceneUuid);
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
    d->assignSceneButton->setText(tr("Asignar al día seleccionado"));
    d->unassignSceneButton->setText(tr("Quitar del día"));
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
    for (auto* lw : { d->daysList, d->boneyardList, d->assignedScenesList }) {
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
