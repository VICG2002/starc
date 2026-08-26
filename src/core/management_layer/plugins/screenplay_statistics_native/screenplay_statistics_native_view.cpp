#include "screenplay_statistics_native_view.h"

#include <business_layer/model/abstract_model.h>
#include <business_layer/model/screenplay/screenplay_information_model.h>
#include <business_layer/model/screenplay/screenplay_statistics_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model.h>
#include <business_layer/plots/abstract_plot.h>
#include <business_layer/plots/screenplay/screenplay_characters_activity_plot.h>
#include <business_layer/plots/screenplay/screenplay_structure_analysis_plot.h>
#include <business_layer/reports/screenplay/screenplay_cast_report.h>
#include <business_layer/reports/screenplay/screenplay_dialogues_report.h>
#include <business_layer/reports/screenplay/screenplay_gender_report.h>
#include <business_layer/reports/screenplay/screenplay_location_report.h>
#include <business_layer/reports/screenplay/screenplay_scene_report.h>
#include <business_layer/reports/screenplay/screenplay_summary_report.h>
#include <ui/design_system/design_system.h>
#include <ui/widgets/plot/plot.h>
#include <utils/helpers/time_helper.h>

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTreeView>
#include <QVBoxLayout>


namespace Ui {

namespace {

//
// Aula 122: índices canónicos, fijados por el navegador `screenplay_statistics_structure`
// (ver screenplay_statistics_structure_view.cpp — el orden en que puebla sus dos árboles).
//
enum ReportIndex {
    kSummaryReportIndex = 0,
    kSceneReportIndex,
    kLocationReportIndex,
    kCastReportIndex,
    kDialoguesReportIndex,
    kGenderReportIndex,
    kReportsCount,
};
enum PlotIndex {
    kStoryStructureAnalysisPlotIndex = 0,
    kCharactersActivityPlotIndex,
    kPlotsCount,
};
//
// El QStackedWidget aloja los 6 reportes (páginas 0-5) seguidos de las 2
// gráficas (páginas 6-7), en ese orden fijo.
//
constexpr int kPageOffsetForPlots = kReportsCount;

/**
 * @brief Configura un QTreeView para que muestre un reporte con el estilo
 *        del resto de la app. `_hierarchical` decide si se expanden todos
 *        los niveles (Escenas/Locaciones/Diálogos) o si es una tabla plana.
 */
void setupReportTree(QTreeView* _tree, bool _hierarchical)
{
    _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    _tree->setAlternatingRowColors(true);
    _tree->setRootIsDecorated(_hierarchical);
    _tree->setItemsExpandable(_hierarchical);
    _tree->setUniformRowHeights(true);
    if (_tree->header() != nullptr) {
        _tree->header()->setStretchLastSection(false);
    }
}

void applyReportModel(QTreeView* _tree, QAbstractItemModel* _model, bool _hierarchical)
{
    _tree->setModel(_model);
    if (_model == nullptr) {
        return;
    }
    if (_tree->header() != nullptr && _model->columnCount() > 0) {
        _tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int column = 1; column < _model->columnCount(); ++column) {
            _tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        }
    }
    if (_hierarchical) {
        _tree->expandAll();
    }
}

QLabel* makeSectionLabel(QWidget* _parent, const QString& _text)
{
    auto* label = new QLabel(_text, _parent);
    return label;
}

} // namespace


// ****


class ScreenplayStatisticsNativeView::Implementation
{
public:
    explicit Implementation(ScreenplayStatisticsNativeView* _q);

    void rebuildSummaryPage();
    void rebuildSceneReportPage();
    void rebuildLocationReportPage();
    void rebuildCastReportPage();
    void rebuildDialoguesReportPage();
    void rebuildGenderPage();
    void rebuildStructurePlotPage();
    void rebuildActivityPlotPage();

    QWidget* buildSummaryPage();
    QWidget* buildSceneReportPage();
    QWidget* buildLocationReportPage();
    QWidget* buildCastReportPage();
    QWidget* buildDialoguesReportPage();
    QWidget* buildGenderPage();
    QWidget* buildStructurePlotPage();
    QWidget* buildActivityPlotPage();

    ScreenplayStatisticsNativeView* q = nullptr;
    QPointer<BusinessLayer::ScreenplayStatisticsModel> model;

    //
    // Encabezado
    //
    QLabel* titleLabel = nullptr;
    QPushButton* exportButton = nullptr;
    QLabel* statusLabel = nullptr;

    QStackedWidget* stack = nullptr;
    //
    // Modo activo: si isPlotMode es true, currentIndex es un PlotIndex;
    // si no, es un ReportIndex. Necesario porque el navegador puede mandar
    // -1 al deseleccionar (clic en el otro árbol) y no hay que tocar la
    // página visible en ese caso.
    //
    bool isPlotMode = false;
    int currentIndex = kSummaryReportIndex;

    //
    // Resumen — cuatro sub-tablas + fila de escalares
    //
    QLabel* summaryStatsLabel = nullptr;
    QTreeView* summaryTextTree = nullptr;
    QTreeView* summarySceneTree = nullptr;
    QTreeView* summaryLocationTree = nullptr;
    QTreeView* summaryCharacterTree = nullptr;
    QLabel* summaryEmptyLabel = nullptr;

    //
    // Escenas
    //
    QCheckBox* sceneShowCharactersCheck = nullptr;
    QComboBox* sceneSortCombo = nullptr;
    QTreeView* sceneTree = nullptr;

    //
    // Locaciones
    //
    QCheckBox* locationExtendedCheck = nullptr;
    QComboBox* locationSortCombo = nullptr;
    QTreeView* locationTree = nullptr;

    //
    // Personajes (cast)
    //
    QCheckBox* castShowDetailsCheck = nullptr;
    QCheckBox* castShowWordsCheck = nullptr;
    QComboBox* castSortCombo = nullptr;
    QTreeView* castTree = nullptr;

    //
    // Diálogos — filtro de personajes visibles
    //
    QListWidget* dialoguesCharactersList = nullptr;
    QTreeView* dialoguesTree = nullptr;

    //
    // Género — dos escalares (Bechdel/reverso) + tres sub-tablas
    //
    QLabel* genderScalarsLabel = nullptr;
    QTreeView* genderScenesTree = nullptr;
    QTreeView* genderDialoguesTree = nullptr;
    QTreeView* genderCharactersTree = nullptr;
    QLabel* genderEmptyLabel = nullptr;

    //
    // Gráfica de análisis de estructura
    //
    QCheckBox* structSceneDurationCheck = nullptr;
    QCheckBox* structActionDurationCheck = nullptr;
    QCheckBox* structDialoguesDurationCheck = nullptr;
    QCheckBox* structCharactersCountCheck = nullptr;
    QCheckBox* structDialoguesCountCheck = nullptr;
    Plot* structurePlot = nullptr;

    //
    // Gráfica de actividad de personajes
    //
    QListWidget* activityCharactersList = nullptr;
    Plot* activityPlot = nullptr;
};

ScreenplayStatisticsNativeView::Implementation::Implementation(ScreenplayStatisticsNativeView* _q)
    : q(_q)
    , titleLabel(new QLabel(_q))
    , exportButton(new QPushButton(_q))
    , statusLabel(new QLabel(_q))
    , stack(new QStackedWidget(_q))
{
    titleLabel->setText(QStringLiteral("Estadísticas"));

    exportButton->setText(QStringLiteral("Exportar..."));
    exportButton->setToolTip(
        QStringLiteral("Exportar el reporte o gráfica visible a PDF/XLSX"));

    statusLabel->setText(QString());
    statusLabel->setAlignment(Qt::AlignCenter);

    stack->addWidget(buildSummaryPage());
    stack->addWidget(buildSceneReportPage());
    stack->addWidget(buildLocationReportPage());
    stack->addWidget(buildCastReportPage());
    stack->addWidget(buildDialoguesReportPage());
    stack->addWidget(buildGenderPage());
    stack->addWidget(buildStructurePlotPage());
    stack->addWidget(buildActivityPlotPage());
    stack->setCurrentIndex(kSummaryReportIndex);
}

//
// ---- Resumen -------------------------------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildSummaryPage()
{
    auto* page = new QWidget;
    summaryStatsLabel = new QLabel(page);
    summaryStatsLabel->setWordWrap(true);
    summaryEmptyLabel = new QLabel(
        QStringLiteral("Las estadísticas aparecerán cuando empieces a escribir el guion."), page);
    summaryEmptyLabel->setAlignment(Qt::AlignCenter);
    summaryEmptyLabel->setWordWrap(true);
    summaryEmptyLabel->hide();

    summaryTextTree = new QTreeView(page);
    summarySceneTree = new QTreeView(page);
    summaryLocationTree = new QTreeView(page);
    summaryCharacterTree = new QTreeView(page);
    for (auto* tree : { summaryTextTree, summarySceneTree, summaryLocationTree,
                       summaryCharacterTree }) {
        setupReportTree(tree, false);
    }

    auto* row1 = new QHBoxLayout;
    row1->setSpacing(16);
    {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        col->addWidget(makeSectionLabel(page, QStringLiteral("Texto")));
        col->addWidget(summaryTextTree, 1);
        row1->addLayout(col, 1);
    }
    {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        col->addWidget(makeSectionLabel(page, QStringLiteral("Duración de escena")));
        col->addWidget(summarySceneTree, 1);
        row1->addLayout(col, 1);
    }
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(16);
    {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        col->addWidget(makeSectionLabel(page, QStringLiteral("Locaciones")));
        col->addWidget(summaryLocationTree, 1);
        row2->addLayout(col, 1);
    }
    {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        col->addWidget(makeSectionLabel(page, QStringLiteral("Personajes")));
        col->addWidget(summaryCharacterTree, 1);
        row2->addLayout(col, 1);
    }

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->setSpacing(16);
    layout->addWidget(summaryStatsLabel);
    layout->addWidget(summaryEmptyLabel);
    layout->addLayout(row1, 1);
    layout->addLayout(row2, 1);
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildSummaryPage()
{
    if (model.isNull()) {
        summaryStatsLabel->clear();
        summaryEmptyLabel->show();
        for (auto* tree : { summaryTextTree, summarySceneTree, summaryLocationTree,
                           summaryCharacterTree }) {
            tree->setModel(nullptr);
            tree->hide();
        }
        return;
    }

    const auto& report = model->summaryReport();
    const bool valid = report.isValid();
    summaryEmptyLabel->setVisible(!valid);
    for (auto* tree : { summaryTextTree, summarySceneTree, summaryLocationTree,
                       summaryCharacterTree }) {
        tree->setVisible(valid);
    }
    if (!valid) {
        summaryStatsLabel->clear();
        return;
    }

    const auto characters = report.charactersCount();
    summaryStatsLabel->setText(QStringLiteral(
                                    "Duración: %1  ·  Páginas: %2  ·  Palabras: %3  ·  "
                                    "Caracteres: %4 (%5 con espacios)")
                                    .arg(TimeHelper::toString(report.duration()))
                                    .arg(report.pagesCount())
                                    .arg(report.wordsCount())
                                    .arg(characters.withoutSpaces)
                                    .arg(characters.withSpaces));

    applyReportModel(summaryTextTree, report.textInfoModel(), false);
    applyReportModel(summarySceneTree, report.scenesInfoModel(), false);
    applyReportModel(summaryLocationTree, report.locationsInfoModel(), false);
    applyReportModel(summaryCharacterTree, report.charactersInfoModel(), false);
}

//
// ---- Escenas --------------------------------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildSceneReportPage()
{
    auto* page = new QWidget;
    sceneShowCharactersCheck = new QCheckBox(QStringLiteral("Mostrar personajes"), page);
    sceneShowCharactersCheck->setChecked(true);
    sceneSortCombo = new QComboBox(page);
    sceneSortCombo->addItems({
        QStringLiteral("Orden del guion"),
        QStringLiteral("Nombre A-Z"),
        QStringLiteral("Duración (mayor a menor)"),
        QStringLiteral("Duración (menor a mayor)"),
        QStringLiteral("N.º personajes (mayor a menor)"),
        QStringLiteral("N.º personajes (menor a mayor)"),
    });
    sceneTree = new QTreeView(page);
    setupReportTree(sceneTree, true);

    auto* paramsRow = new QHBoxLayout;
    paramsRow->setSpacing(12);
    paramsRow->addWidget(sceneShowCharactersCheck);
    paramsRow->addWidget(sceneSortCombo);
    paramsRow->addStretch();

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->setSpacing(8);
    layout->addLayout(paramsRow);
    layout->addWidget(sceneTree, 1);

    QObject::connect(sceneShowCharactersCheck, &QCheckBox::toggled, q,
                      [this] { rebuildSceneReportPage(); });
    QObject::connect(sceneSortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                      [this] { rebuildSceneReportPage(); });
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildSceneReportPage()
{
    if (model.isNull()) {
        sceneTree->setModel(nullptr);
        return;
    }
    model->setSceneReportParameters(sceneShowCharactersCheck->isChecked(),
                                    sceneSortCombo->currentIndex());
    applyReportModel(sceneTree, model->sceneReport().sceneModel(), true);
}

//
// ---- Locaciones ------------------------------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildLocationReportPage()
{
    auto* page = new QWidget;
    locationExtendedCheck = new QCheckBox(QStringLiteral("Vista extendida (por escena)"), page);
    locationExtendedCheck->setChecked(true);
    locationSortCombo = new QComboBox(page);
    locationSortCombo->addItems({
        QStringLiteral("Orden de aparición"),
        QStringLiteral("Nombre A-Z"),
        QStringLiteral("N.º escenas (mayor a menor)"),
        QStringLiteral("N.º escenas (menor a mayor)"),
        QStringLiteral("Duración (mayor a menor)"),
        QStringLiteral("Duración (menor a mayor)"),
    });
    locationTree = new QTreeView(page);
    setupReportTree(locationTree, true);

    auto* paramsRow = new QHBoxLayout;
    paramsRow->setSpacing(12);
    paramsRow->addWidget(locationExtendedCheck);
    paramsRow->addWidget(locationSortCombo);
    paramsRow->addStretch();

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->setSpacing(8);
    layout->addLayout(paramsRow);
    layout->addWidget(locationTree, 1);

    QObject::connect(locationExtendedCheck, &QCheckBox::toggled, q,
                      [this] { rebuildLocationReportPage(); });
    QObject::connect(locationSortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                      [this] { rebuildLocationReportPage(); });
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildLocationReportPage()
{
    if (model.isNull()) {
        locationTree->setModel(nullptr);
        return;
    }
    model->setLocationReportParameters(locationExtendedCheck->isChecked(),
                                       locationSortCombo->currentIndex());
    applyReportModel(locationTree, model->locationReport().locationModel(), true);
}

//
// ---- Personajes (cast) ------------------------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildCastReportPage()
{
    auto* page = new QWidget;
    castShowDetailsCheck = new QCheckBox(QStringLiteral("Detalle de escenas"), page);
    castShowDetailsCheck->setChecked(true);
    castShowWordsCheck = new QCheckBox(QStringLiteral("Mostrar palabras"), page);
    castShowWordsCheck->setChecked(true);
    castSortCombo = new QComboBox(page);
    castSortCombo->addItems({
        QStringLiteral("Orden de aparición"),
        QStringLiteral("Nombre A-Z"),
        QStringLiteral("N.º escenas (mayor a menor)"),
        QStringLiteral("N.º escenas (menor a mayor)"),
        QStringLiteral("N.º diálogos (mayor a menor)"),
        QStringLiteral("N.º diálogos (menor a mayor)"),
    });
    castTree = new QTreeView(page);
    setupReportTree(castTree, false);

    auto* paramsRow = new QHBoxLayout;
    paramsRow->setSpacing(12);
    paramsRow->addWidget(castShowDetailsCheck);
    paramsRow->addWidget(castShowWordsCheck);
    paramsRow->addWidget(castSortCombo);
    paramsRow->addStretch();

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->setSpacing(8);
    layout->addLayout(paramsRow);
    layout->addWidget(castTree, 1);

    QObject::connect(castShowDetailsCheck, &QCheckBox::toggled, q,
                      [this] { rebuildCastReportPage(); });
    QObject::connect(castShowWordsCheck, &QCheckBox::toggled, q,
                      [this] { rebuildCastReportPage(); });
    QObject::connect(castSortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                      [this] { rebuildCastReportPage(); });
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildCastReportPage()
{
    if (model.isNull()) {
        castTree->setModel(nullptr);
        return;
    }
    //
    // Aula 122: el modelo de este reporte AGREGA o QUITA columnas según estos
    // flags (columnCount varía entre 4 y 7) — nunca asumir posiciones fijas,
    // por eso applyReportModel() lee columnCount()/headerData() en runtime.
    //
    model->setCastReportParameters(castShowDetailsCheck->isChecked(),
                                   castShowWordsCheck->isChecked(), castSortCombo->currentIndex());
    applyReportModel(castTree, model->castReport().castModel(), false);
}

//
// ---- Diálogos --------------------------------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildDialoguesReportPage()
{
    auto* page = new QWidget;
    dialoguesCharactersList = new QListWidget(page);
    dialoguesCharactersList->setMaximumWidth(220);
    dialoguesTree = new QTreeView(page);
    setupReportTree(dialoguesTree, true);

    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(4);
    leftCol->addWidget(makeSectionLabel(page, QStringLiteral("Personajes")));
    leftCol->addWidget(dialoguesCharactersList, 1);

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    row->addLayout(leftCol);
    row->addWidget(dialoguesTree, 1);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->addLayout(row, 1);

    QObject::connect(dialoguesCharactersList, &QListWidget::itemChanged, q,
                      [this](QListWidgetItem*) {
                          if (model.isNull()) {
                              return;
                          }
                          QVector<QString> visible;
                          for (int i = 0; i < dialoguesCharactersList->count(); ++i) {
                              auto* item = dialoguesCharactersList->item(i);
                              if (item->checkState() == Qt::Checked) {
                                  visible.append(item->text());
                              }
                          }
                          model->setDialoguesReportParameters(visible);
                          applyReportModel(dialoguesTree, model->dialoguesReport().dialoguesModel(),
                                           true);
                      });
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildDialoguesReportPage()
{
    if (model.isNull()) {
        dialoguesCharactersList->clear();
        dialoguesTree->setModel(nullptr);
        return;
    }

    //
    // Repoblar el catálogo de personajes solo si cambió (evita perder la
    // selección del usuario en cada refresco)
    //
    const auto characters = model->dialoguesReport().characters();
    if (dialoguesCharactersList->count() != characters.size()) {
        QSignalBlocker blocker(dialoguesCharactersList);
        dialoguesCharactersList->clear();
        for (const auto& name : characters) {
            auto* item = new QListWidgetItem(name, dialoguesCharactersList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
    }
    applyReportModel(dialoguesTree, model->dialoguesReport().dialoguesModel(), true);
}

//
// ---- Género -----------------------------------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildGenderPage()
{
    auto* page = new QWidget;
    genderScalarsLabel = new QLabel(page);
    genderScalarsLabel->setWordWrap(true);
    genderEmptyLabel = new QLabel(
        QStringLiteral("El análisis de género aparece cuando el guion tiene personajes con "
                       "género asignado."),
        page);
    genderEmptyLabel->setAlignment(Qt::AlignCenter);
    genderEmptyLabel->setWordWrap(true);
    genderEmptyLabel->hide();

    genderScenesTree = new QTreeView(page);
    genderDialoguesTree = new QTreeView(page);
    genderCharactersTree = new QTreeView(page);
    for (auto* tree : { genderScenesTree, genderDialoguesTree, genderCharactersTree }) {
        setupReportTree(tree, false);
    }

    auto* row = new QHBoxLayout;
    row->setSpacing(16);
    auto addCol = [&row, page](const QString& _title, QTreeView* _tree) {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        col->addWidget(makeSectionLabel(page, _title));
        col->addWidget(_tree, 1);
        row->addLayout(col, 1);
    };
    addCol(QStringLiteral("Por escena"), genderScenesTree);
    addCol(QStringLiteral("Por diálogo"), genderDialoguesTree);
    addCol(QStringLiteral("Por personaje"), genderCharactersTree);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->setSpacing(16);
    layout->addWidget(genderScalarsLabel);
    layout->addWidget(genderEmptyLabel);
    layout->addLayout(row, 1);
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildGenderPage()
{
    if (model.isNull()) {
        genderScalarsLabel->clear();
        genderEmptyLabel->show();
        for (auto* tree : { genderScenesTree, genderDialoguesTree, genderCharactersTree }) {
            tree->setModel(nullptr);
            tree->hide();
        }
        return;
    }

    const auto& report = model->genderReport();
    const bool valid = report.charactersInfoModel() != nullptr
        && report.charactersInfoModel()->rowCount() > 0;
    genderEmptyLabel->setVisible(!valid);
    for (auto* tree : { genderScenesTree, genderDialoguesTree, genderCharactersTree }) {
        tree->setVisible(valid);
    }
    if (!valid) {
        genderScalarsLabel->clear();
        return;
    }

    const auto bechdel = report.bechdelTest();
    const auto reverseBechdel = report.reverseBechdelTest();
    genderScalarsLabel->setText(
        QStringLiteral("Test de Bechdel: %1  ·  Test de Bechdel inverso: %2")
            .arg(bechdel > 0 ? QStringLiteral("aprobado (%1 escena/s)").arg(bechdel)
                             : QStringLiteral("no aprobado"),
                 reverseBechdel > 0 ? QStringLiteral("aprobado (%1 escena/s)").arg(reverseBechdel)
                                    : QStringLiteral("no aprobado")));

    applyReportModel(genderScenesTree, report.scenesInfoModel(), false);
    applyReportModel(genderDialoguesTree, report.dialoguesInfoModel(), false);
    applyReportModel(genderCharactersTree, report.charactersInfoModel(), false);
}

//
// ---- Gráfica: análisis de estructura -----------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildStructurePlotPage()
{
    auto* page = new QWidget;
    structSceneDurationCheck = new QCheckBox(QStringLiteral("Duración de escena"), page);
    structActionDurationCheck = new QCheckBox(QStringLiteral("Duración de acción"), page);
    structDialoguesDurationCheck = new QCheckBox(QStringLiteral("Duración de diálogo"), page);
    structCharactersCountCheck = new QCheckBox(QStringLiteral("N.º de personajes"), page);
    structDialoguesCountCheck = new QCheckBox(QStringLiteral("N.º de diálogos"), page);
    for (auto* check : { structSceneDurationCheck, structActionDurationCheck,
                        structDialoguesDurationCheck, structCharactersCountCheck,
                        structDialoguesCountCheck }) {
        check->setChecked(true);
    }
    structurePlot = new Plot(page);

    auto* paramsRow = new QHBoxLayout;
    paramsRow->setSpacing(12);
    for (auto* check : { structSceneDurationCheck, structActionDurationCheck,
                        structDialoguesDurationCheck, structCharactersCountCheck,
                        structDialoguesCountCheck }) {
        paramsRow->addWidget(check);
    }
    paramsRow->addStretch();

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->setSpacing(8);
    layout->addLayout(paramsRow);
    layout->addWidget(structurePlot, 1);

    for (auto* check : { structSceneDurationCheck, structActionDurationCheck,
                        structDialoguesDurationCheck, structCharactersCountCheck,
                        structDialoguesCountCheck }) {
        QObject::connect(check, &QCheckBox::toggled, q, [this] { rebuildStructurePlotPage(); });
    }
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildStructurePlotPage()
{
    structurePlot->clearGraphs();
    if (model.isNull()) {
        structurePlot->replot();
        return;
    }

    model->setStructureAnalysisPlotParameters(
        structSceneDurationCheck->isChecked(), structActionDurationCheck->isChecked(),
        structDialoguesDurationCheck->isChecked(), structCharactersCountCheck->isChecked(),
        structDialoguesCountCheck->isChecked());
    const auto plotData = model->structureAnalysisPlot().plot();

    //
    // Aula 122: dos ejes Y — las tres series de duración vienen en MINUTOS,
    // las dos de conteo en unidades crudas. Mezclarlas en un solo eje
    // engañaría la lectura de la gráfica. yAxis2 ya existe en todo
    // QCustomPlot (oculto por defecto); solo hay que mostrarlo.
    //
    auto* countsAxis = structurePlot->yAxis2;
    countsAxis->setVisible(true);
    structurePlot->xAxis->setLabel(QStringLiteral("Minutos"));
    structurePlot->yAxis->setLabel(QStringLiteral("Duración (min)"));
    countsAxis->setLabel(QStringLiteral("Cantidad"));

    for (const auto& series : plotData.data) {
        auto* graph = structurePlot->addGraph();
        graph->setName(series.name);
        graph->setPen(QPen(series.color, 2));
        //
        // Las dos últimas series activadas (personajes/diálogos por conteo)
        // van al eje derecho; se distinguen porque su nombre no contiene
        // "duración" — el propio negocio ya las nombra así.
        //
        if (series.name.contains(QStringLiteral("duración"), Qt::CaseInsensitive)
            || series.name.contains(QStringLiteral("duration"), Qt::CaseInsensitive)) {
            graph->setValueAxis(structurePlot->yAxis);
        } else {
            graph->setValueAxis(countsAxis);
        }
        graph->setData(series.x, series.y);
    }
    structurePlot->setPlotInfo(plotData.info);
    structurePlot->legend->setVisible(!plotData.data.isEmpty());
    structurePlot->rescaleAxes();
    countsAxis->rescale();
    structurePlot->replot();
}

//
// ---- Gráfica: actividad de personajes -----------------------------------------
//

QWidget* ScreenplayStatisticsNativeView::Implementation::buildActivityPlotPage()
{
    auto* page = new QWidget;
    activityCharactersList = new QListWidget(page);
    activityCharactersList->setMaximumWidth(220);
    activityPlot = new Plot(page);

    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(4);
    leftCol->addWidget(makeSectionLabel(page, QStringLiteral("Personajes")));
    leftCol->addWidget(activityCharactersList, 1);

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    row->addLayout(leftCol);
    row->addWidget(activityPlot, 1);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->addLayout(row, 1);

    QObject::connect(activityCharactersList, &QListWidget::itemChanged, q,
                      [this](QListWidgetItem*) {
                          if (model.isNull()) {
                              return;
                          }
                          QVector<QString> visible;
                          for (int i = 0; i < activityCharactersList->count(); ++i) {
                              auto* item = activityCharactersList->item(i);
                              if (item->checkState() == Qt::Checked) {
                                  visible.append(item->text());
                              }
                          }
                          model->setCharactersActivityPlotParameters(visible);
                          const auto plotData = model->charactersActivityPlot().plot();
                          activityPlot->clearGraphs();
                          for (const auto& series : plotData.data) {
                              auto* graph = activityPlot->addGraph();
                              graph->setName(series.name);
                              QPen pen(series.color);
                              pen.setWidthF(5);
                              pen.setCapStyle(Qt::RoundCap);
                              graph->setPen(pen);
                              graph->setData(series.x, series.y);
                          }
                          activityPlot->setPlotInfo(plotData.info);
                          activityPlot->legend->setVisible(!plotData.data.isEmpty());
                          activityPlot->yAxis->setTickLabels(false);
                          activityPlot->xAxis->setLabel(QStringLiteral("Minutos"));
                          activityPlot->rescaleAxes();
                          activityPlot->replot();
                      });
    return page;
}

void ScreenplayStatisticsNativeView::Implementation::rebuildActivityPlotPage()
{
    activityPlot->clearGraphs();
    if (model.isNull()) {
        activityCharactersList->clear();
        activityPlot->replot();
        return;
    }

    const auto characters = model->charactersActivityPlot().characters();
    if (activityCharactersList->count() != characters.size()) {
        QSignalBlocker blocker(activityCharactersList);
        activityCharactersList->clear();
        for (const auto& name : characters) {
            auto* item = new QListWidgetItem(name, activityCharactersList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
    }

    const auto plotData = model->charactersActivityPlot().plot();
    for (const auto& series : plotData.data) {
        auto* graph = activityPlot->addGraph();
        graph->setName(series.name);
        QPen pen(series.color);
        pen.setWidthF(5);
        pen.setCapStyle(Qt::RoundCap);
        graph->setPen(pen);
        graph->setData(series.x, series.y);
    }
    activityPlot->setPlotInfo(plotData.info);
    activityPlot->legend->setVisible(!plotData.data.isEmpty());
    //
    // Aula 122: el valor Y crudo es un carril interno sin significado para
    // quien lee la gráfica — la identificación de personaje va por color +
    // leyenda + el tooltip (que sí lista los personajes de cada escena).
    //
    activityPlot->yAxis->setTickLabels(false);
    activityPlot->xAxis->setLabel(QStringLiteral("Minutos"));
    activityPlot->rescaleAxes();
    activityPlot->replot();
}


// ****


ScreenplayStatisticsNativeView::ScreenplayStatisticsNativeView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    auto* headerRow = new QHBoxLayout;
    headerRow->setContentsMargins({});
    headerRow->setSpacing(8);
    headerRow->addWidget(d->titleLabel, 1);
    headerRow->addWidget(d->exportButton);

    auto* layout = new QVBoxLayout;
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addLayout(headerRow);
    layout->addWidget(d->stack, 1);
    layout->addWidget(d->statusLabel);
    setLayout(layout);

    connect(d->exportButton, &QPushButton::clicked, this,
            &ScreenplayStatisticsNativeView::onExportClicked);
}

ScreenplayStatisticsNativeView::~ScreenplayStatisticsNativeView() = default;

QWidget* ScreenplayStatisticsNativeView::asQWidget()
{
    return this;
}

void ScreenplayStatisticsNativeView::setEditingMode(ManagementLayer::DocumentEditingMode _mode)
{
    Q_UNUSED(_mode)
}

void ScreenplayStatisticsNativeView::setStatisticsModel(BusinessLayer::AbstractModel* _model)
{
    if (d->model != nullptr && d->model->textModel() != nullptr
        && d->model->textModel()->informationModel() != nullptr) {
        disconnect(d->model->textModel()->informationModel(),
                  &BusinessLayer::ScreenplayInformationModel::nameChanged, this, nullptr);
    }

    d->model = qobject_cast<BusinessLayer::ScreenplayStatisticsModel*>(_model);

    if (d->model != nullptr) {
        d->model->updateReports();

        if (d->model->textModel() != nullptr
            && d->model->textModel()->informationModel() != nullptr) {
            auto updateTitle = [this] {
                d->titleLabel->setText(QStringLiteral("Estadísticas | %1").arg(
                    d->model->textModel()->informationModel()->name()));
            };
            updateTitle();
            connect(d->model->textModel()->informationModel(),
                    &BusinessLayer::ScreenplayInformationModel::nameChanged, this, updateTitle);
        }
    } else {
        d->titleLabel->setText(QStringLiteral("Estadísticas"));
    }

    rebuildAll();
}

void ScreenplayStatisticsNativeView::rebuildAll()
{
    d->rebuildSummaryPage();
    d->rebuildSceneReportPage();
    d->rebuildLocationReportPage();
    d->rebuildCastReportPage();
    d->rebuildDialoguesReportPage();
    d->rebuildGenderPage();
    d->rebuildStructurePlotPage();
    d->rebuildActivityPlotPage();

    d->statusLabel->setText(d->model.isNull()
                                ? QStringLiteral("Abre un proyecto con guion para ver sus "
                                                 "estadísticas.")
                                : QString());
}

void ScreenplayStatisticsNativeView::setCurrentReport(int _index)
{
    if (_index < 0 || _index >= kReportsCount) {
        return;
    }
    d->isPlotMode = false;
    d->currentIndex = _index;
    d->stack->setCurrentIndex(_index);
}

void ScreenplayStatisticsNativeView::setCurrentPlot(int _index)
{
    if (_index < 0 || _index >= kPlotsCount) {
        return;
    }
    d->isPlotMode = true;
    d->currentIndex = _index;
    d->stack->setCurrentIndex(kPageOffsetForPlots + _index);
}

void ScreenplayStatisticsNativeView::onExportClicked()
{
    if (d->model.isNull()) {
        QMessageBox::information(this, tr("Exportar"),
                                 tr("No hay estadísticas para exportar. Abre un proyecto con "
                                    "guion."));
        return;
    }

    const QString defaultDir
        = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    if (d->isPlotMode) {
        //
        // Las gráficas siempre se guardan en XLSX (AbstractPlot::saveToFile
        // no tiene rama PDF)
        //
        const QString suggested = QStringLiteral("%1/grafica.xlsx").arg(defaultDir);
        const QString chosen = QFileDialog::getSaveFileName(
            this, tr("Exportar gráfica"), suggested, tr("Excel (*.xlsx)"));
        if (chosen.isEmpty()) {
            return;
        }
        QString path = chosen;
        if (!path.toLower().endsWith(QStringLiteral(".xlsx"))) {
            path += QStringLiteral(".xlsx");
        }
        if (d->currentIndex == kStoryStructureAnalysisPlotIndex) {
            d->model->structureAnalysisPlot().saveToFile(path);
        } else {
            d->model->charactersActivityPlot().saveToFile(path);
        }
        d->statusLabel->setText(tr("Exportado a %1").arg(QFileInfo(path).fileName()));
        return;
    }

    const QString suggested = QStringLiteral("%1/estadisticas.pdf").arg(defaultDir);
    QString selectedFilter;
    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("Exportar reporte"), suggested, tr("PDF (*.pdf);;Excel (*.xlsx)"),
        &selectedFilter);
    if (chosen.isEmpty()) {
        return;
    }
    QString path = chosen;
    const bool isXlsx = selectedFilter.contains(QStringLiteral("xlsx"), Qt::CaseInsensitive)
        || path.toLower().endsWith(QStringLiteral(".xlsx"));
    if (isXlsx) {
        if (!path.toLower().endsWith(QStringLiteral(".xlsx"))) {
            path += QStringLiteral(".xlsx");
        }
    } else if (!path.toLower().endsWith(QStringLiteral(".pdf"))) {
        path += QStringLiteral(".pdf");
    }

    //
    // AbstractReport::saveToFile despacha PDF/XLSX solo por la extensión
    //
    switch (d->currentIndex) {
    case kSummaryReportIndex:
        d->model->summaryReport().saveToFile(path);
        break;
    case kSceneReportIndex:
        d->model->sceneReport().saveToFile(path);
        break;
    case kLocationReportIndex:
        d->model->locationReport().saveToFile(path);
        break;
    case kCastReportIndex:
        d->model->castReport().saveToFile(path);
        break;
    case kDialoguesReportIndex:
        d->model->dialoguesReport().saveToFile(path);
        break;
    case kGenderReportIndex:
        d->model->genderReport().saveToFile(path);
        break;
    default:
        break;
    }
    d->statusLabel->setText(tr("Exportado a %1").arg(QFileInfo(path).fileName()));
}

void ScreenplayStatisticsNativeView::updateTranslations()
{
    d->exportButton->setText(tr("Exportar..."));
}

void ScreenplayStatisticsNativeView::designSystemChangeEvent(DesignSystemChangeEvent* _event)
{
    Widget::designSystemChangeEvent(_event);

    setBackgroundColor(DesignSystem::color().surface());

    const auto bodyColor = DesignSystem::color().onSurface().name();
    const auto mutedColor = DesignSystem::color().onBackground().name();
    const auto bgColor = DesignSystem::color().background().name();

    d->titleLabel->setFont(DesignSystem::font().h6());
    d->titleLabel->setStyleSheet(QString("color: %1;").arg(bodyColor));

    d->exportButton->setFont(DesignSystem::font().button());

    d->statusLabel->setFont(DesignSystem::font().caption());
    d->statusLabel->setStyleSheet(QString("color: %1;").arg(bodyColor));

    const auto treeStyle
        = QString("QTreeView { color: %1; background: %2; }"
                 "QHeaderView::section { background: %2; color: %1; padding: 4px; }")
              .arg(bodyColor, bgColor);
    for (auto* tree : { d->summaryTextTree, d->summarySceneTree, d->summaryLocationTree,
                       d->summaryCharacterTree, d->sceneTree, d->locationTree, d->castTree,
                       d->dialoguesTree, d->genderScenesTree, d->genderDialoguesTree,
                       d->genderCharactersTree }) {
        tree->setFont(DesignSystem::font().body2());
        tree->setStyleSheet(treeStyle);
    }

    const auto listStyle = QString("QListWidget { color: %1; background: %2; border: 1px "
                                   "solid %3; }")
                                .arg(bodyColor, bgColor, mutedColor);
    for (auto* list : { d->dialoguesCharactersList, d->activityCharactersList }) {
        list->setFont(DesignSystem::font().body2());
        list->setStyleSheet(listStyle);
    }

    for (auto* label : { d->summaryStatsLabel, d->summaryEmptyLabel, d->genderScalarsLabel,
                        d->genderEmptyLabel }) {
        label->setFont(DesignSystem::font().body1());
        label->setStyleSheet(QString("color: %1;").arg(bodyColor));
    }
}

} // namespace Ui
