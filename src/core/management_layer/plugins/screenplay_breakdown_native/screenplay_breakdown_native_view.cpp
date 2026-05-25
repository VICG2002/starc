#include "screenplay_breakdown_native_view.h"

#include <business_layer/model/abstract_model.h>
#include <business_layer/model/screenplay/screenplay_dictionaries_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model_scene_item.h>
#include <business_layer/model/text/text_model_folder_item.h>
#include <business_layer/model/text/text_model_group_item.h>
#include <business_layer/templates/text_template.h>
#include <ui/design_system/design_system.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPdfWriter>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTableView>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QTextTable>
#include <QTextTableFormat>
#include <QUuid>
#include <QVBoxLayout>


namespace Ui {

class ScreenplayBreakdownNativeView::Implementation
{
public:
    explicit Implementation(ScreenplayBreakdownNativeView* _q);

    ScreenplayBreakdownNativeView* q = nullptr;
    QPointer<BusinessLayer::ScreenplayTextModel> screenplayModel;
    QVector<BusinessLayer::ScreenplayTextModelSceneItem*> sceneCache;
    int selectedRow = -1;

    QLabel* titleLabel = nullptr;
    QSplitter* splitter = nullptr;

    // Lado izquierdo
    QTableView* sceneTable = nullptr;
    QStandardItemModel* sceneTableModel = nullptr;

    // Lado derecho — detalle
    QLabel* detailHeading = nullptr;
    QListWidget* resourcesList = nullptr;
    QPushButton* addResourceButton = nullptr;
    QPushButton* removeResourceButton = nullptr;

    // Toolbar superior (al lado del título)
    QPushButton* exportButton = nullptr;

    QLabel* statusLabel = nullptr;
};

ScreenplayBreakdownNativeView::Implementation::Implementation(ScreenplayBreakdownNativeView* _q)
    : q(_q)
    , titleLabel(new QLabel(_q))
    , splitter(new QSplitter(Qt::Horizontal, _q))
    , sceneTable(new QTableView(_q))
    , sceneTableModel(new QStandardItemModel(_q))
    , detailHeading(new QLabel(_q))
    , resourcesList(new QListWidget(_q))
    , addResourceButton(new QPushButton(_q))
    , removeResourceButton(new QPushButton(_q))
    , exportButton(new QPushButton(_q))
    , statusLabel(new QLabel(_q))
{
    titleLabel->setText(QStringLiteral("Desglose del guion"));
    titleLabel->setAlignment(Qt::AlignCenter);

    sceneTableModel->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        QStringLiteral("Heading"),
        QStringLiteral("Recursos"),
    });
    sceneTable->setModel(sceneTableModel);
    sceneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sceneTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    sceneTable->setSelectionMode(QAbstractItemView::SingleSelection);
    sceneTable->setAlternatingRowColors(true);
    sceneTable->horizontalHeader()->setStretchLastSection(false);
    sceneTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    sceneTable->verticalHeader()->setVisible(false);

    detailHeading->setText(QStringLiteral("Selecciona una escena para ver sus recursos"));
    detailHeading->setWordWrap(true);

    addResourceButton->setText(QStringLiteral("Añadir recurso"));
    removeResourceButton->setText(QStringLiteral("Quitar"));
    removeResourceButton->setEnabled(false);
    addResourceButton->setEnabled(false);

    exportButton->setText(QStringLiteral("Exportar..."));
    exportButton->setToolTip(QStringLiteral("Exportar breakdown a PDF o CSV"));

    statusLabel->setText(QString());
    statusLabel->setAlignment(Qt::AlignCenter);
}


// ****


namespace {

/**
 * @brief Recorre recursivamente el árbol del modelo recolectando todas las
 *        escenas (TextModelGroupItem cuyo subtype es Scene).
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

/**
 * @brief Paleta color-coded estilo StudioBinder. Mapeo nombre de categoría
 *        (case-insensitive, español o inglés) → QColor. Si no hay match,
 *        retorna un color del fallback rotativo.
 */
QColor colorForCategory(const QString& _categoryName, int _fallbackIndex = 0)
{
    static const QHash<QString, QColor> kCategoryColors = {
        // Cast / personajes
        { "cast", QColor("#FF6B6B") },
        { "personajes", QColor("#FF6B6B") },
        // Extras
        { "extras", QColor("#FFD93D") },
        // Stunts / dobles
        { "stunts", QColor("#FF8C42") },
        { "dobles", QColor("#FF8C42") },
        // Vehicles
        { "vehicles", QColor("#95D86E") },
        { "vehículos", QColor("#95D86E") },
        { "vehiculos", QColor("#95D86E") },
        // Props
        { "props", QColor("#B5A4E3") },
        { "atrezo", QColor("#B5A4E3") },
        // Sound effects
        { "sfx", QColor("#4ECDC4") },
        { "sound effects", QColor("#4ECDC4") },
        // Special FX (efectos prácticos)
        { "maquillaje/sfx", QColor("#E67373") },
        { "special effects", QColor("#E67373") },
        // VFX
        { "vfx", QColor("#3D5A80") },
        // Wardrobe
        { "wardrobe", QColor("#A0E7E5") },
        { "vestuario", QColor("#A0E7E5") },
        // Makeup / Hair
        { "makeup", QColor("#C9B1FF") },
        { "maquillaje", QColor("#C9B1FF") },
        { "hair", QColor("#C9B1FF") },
        // Animals
        { "animals", QColor("#7FE3D4") },
        { "animales", QColor("#7FE3D4") },
        // Music
        { "music", QColor("#D6B3FF") },
        { "música", QColor("#D6B3FF") },
        { "musica", QColor("#D6B3FF") },
        // Special equipment
        { "equipment", QColor("#B0B0B0") },
        { "equipo", QColor("#B0B0B0") },
        // Set dressing
        { "set dressing", QColor("#808080") },
        { "ambientación", QColor("#808080") },
        // Greenery
        { "greenery", QColor("#5A8C3F") },
        { "follaje", QColor("#5A8C3F") },
        // Security
        { "security", QColor("#404040") },
        { "seguridad", QColor("#404040") },
        // Armas (común en cine de acción / drama)
        { "armas", QColor("#8B4513") },
        { "weapons", QColor("#8B4513") },
    };

    const auto it = kCategoryColors.constFind(_categoryName.toLower().trimmed());
    if (it != kCategoryColors.constEnd()) {
        return it.value();
    }

    //
    // Fallback rotativo (10 colores distinguibles si la categoría no es estándar)
    //
    static const QVector<QColor> kFallback = {
        QColor("#E91E63"), QColor("#9C27B0"), QColor("#3F51B5"), QColor("#009688"),
        QColor("#FF5722"), QColor("#795548"), QColor("#607D8B"), QColor("#FFC107"),
        QColor("#673AB7"), QColor("#00BCD4"),
    };
    return kFallback[std::abs(_fallbackIndex) % kFallback.size()];
}

} // anonymous namespace


ScreenplayBreakdownNativeView::ScreenplayBreakdownNativeView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    //
    // Panel izquierdo: tabla de escenas
    //
    auto leftWidget = new QWidget(this);
    auto leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(d->sceneTable, 1);

    //
    // Panel derecho: detalle de recursos
    //
    auto rightWidget = new QWidget(this);
    auto rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(d->detailHeading);
    rightLayout->addWidget(d->resourcesList, 1);
    auto buttonsRow = new QHBoxLayout;
    buttonsRow->setContentsMargins({});
    buttonsRow->addWidget(d->addResourceButton);
    buttonsRow->addWidget(d->removeResourceButton);
    buttonsRow->addStretch();
    rightLayout->addLayout(buttonsRow);

    d->splitter->addWidget(leftWidget);
    d->splitter->addWidget(rightWidget);
    d->splitter->setStretchFactor(0, 2);
    d->splitter->setStretchFactor(1, 1);

    auto headerRow = new QHBoxLayout;
    headerRow->setContentsMargins({});
    headerRow->setSpacing(8);
    headerRow->addWidget(d->titleLabel, 1);
    headerRow->addWidget(d->exportButton);

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addLayout(headerRow);
    layout->addWidget(d->splitter, 1);
    layout->addWidget(d->statusLabel);
    setLayout(layout);

    //
    // Wiring
    //
    connect(d->sceneTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& _current, const QModelIndex&) {
                onSceneSelectionChanged(_current.row());
            });
    connect(d->addResourceButton, &QPushButton::clicked, this,
            &ScreenplayBreakdownNativeView::onAddResourceClicked);
    connect(d->removeResourceButton, &QPushButton::clicked, this,
            &ScreenplayBreakdownNativeView::onRemoveResourceClicked);
    connect(d->resourcesList, &QListWidget::itemSelectionChanged, this, [this] {
        d->removeResourceButton->setEnabled(d->resourcesList->currentItem() != nullptr);
    });
    connect(d->exportButton, &QPushButton::clicked, this,
            &ScreenplayBreakdownNativeView::onExportClicked);
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

void ScreenplayBreakdownNativeView::setScreenplayModel(BusinessLayer::AbstractModel* _model)
{
    d->screenplayModel = qobject_cast<BusinessLayer::ScreenplayTextModel*>(_model);
    d->selectedRow = -1;
    refreshSceneTable();
}

void ScreenplayBreakdownNativeView::refreshSceneTable()
{
    d->sceneTableModel->removeRows(0, d->sceneTableModel->rowCount());
    d->sceneCache.clear();
    d->detailHeading->setText(tr("Selecciona una escena para ver sus recursos"));
    d->resourcesList->clear();
    d->addResourceButton->setEnabled(false);
    d->removeResourceButton->setEnabled(false);

    if (d->screenplayModel.isNull()) {
        d->statusLabel->setText(
            tr("Abre un proyecto de guion para ver el desglose de escenas."));
        return;
    }

    collectScenes(d->screenplayModel->itemForIndex(QModelIndex()), d->sceneCache);

    int idx = 1;
    for (auto* scene : d->sceneCache) {
        if (scene == nullptr) {
            continue;
        }
        auto* numberItem = new QStandardItem(QString::number(idx++));
        auto* headingItem = new QStandardItem(scene->heading());
        auto* resourcesItem = new QStandardItem(
            QString::number(scene->resources().size()));
        numberItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        headingItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        resourcesItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        d->sceneTableModel->appendRow({ numberItem, headingItem, resourcesItem });
    }

    d->statusLabel->setText(tr("%1 escenas. Selecciona una para editar sus recursos.")
                                .arg(d->sceneCache.size()));
}

void ScreenplayBreakdownNativeView::onSceneSelectionChanged(int _row)
{
    d->selectedRow = _row;
    refreshResourcesList();
}

void ScreenplayBreakdownNativeView::refreshResourcesList()
{
    d->resourcesList->clear();
    d->removeResourceButton->setEnabled(false);

    if (d->selectedRow < 0 || d->selectedRow >= d->sceneCache.size()
        || d->screenplayModel.isNull()) {
        d->detailHeading->setText(tr("Selecciona una escena para ver sus recursos"));
        d->addResourceButton->setEnabled(false);
        return;
    }

    auto* scene = d->sceneCache[d->selectedRow];
    if (scene == nullptr) {
        return;
    }

    d->detailHeading->setText(scene->heading());
    d->addResourceButton->setEnabled(true);

    auto* dictionaries = d->screenplayModel->dictionariesModel();
    const auto sceneResources = scene->resources();
    for (const auto& sr : sceneResources) {
        QString categoryName;
        QColor categoryColor;
        QString resourceName;
        if (dictionaries != nullptr) {
            const auto resource = dictionaries->resource(sr.uuid);
            resourceName = resource.name;
            if (!resource.categoryUuid.isNull()) {
                const auto category = dictionaries->resourceCategory(resource.categoryUuid);
                categoryName = category.name;
                categoryColor = category.color.isValid()
                    ? category.color
                    : colorForCategory(category.name);
            }
        }
        if (resourceName.isEmpty()) {
            resourceName = sr.description.isEmpty() ? tr("(sin nombre)") : sr.description;
        }
        QString label;
        if (!categoryName.isEmpty()) {
            label = QStringLiteral("%1: %2").arg(categoryName, resourceName);
        } else {
            label = resourceName;
        }
        if (sr.qty > 1) {
            label += QStringLiteral(" × %1").arg(sr.qty);
        }
        if (!sr.description.isEmpty() && sr.description != resourceName) {
            label += QStringLiteral(" — %1").arg(sr.description);
        }
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, sr.uuid);
        //
        // Aula 122 / Bloque 5.C: tag visual con color de categoría (alpha
        // bajo para que el texto siga siendo legible sobre el fondo).
        //
        if (categoryColor.isValid()) {
            QColor bg = categoryColor;
            bg.setAlpha(60);
            item->setBackground(QBrush(bg));
        }
        d->resourcesList->addItem(item);
    }
}

namespace {

/**
 * @brief Diálogo simple para añadir un recurso a una escena.
 *        Categoría (combo), nombre del recurso, cantidad, descripción.
 */
struct AddResourceResult {
    bool accepted = false;
    QString categoryName;
    QString resourceName;
    int qty = 1;
    QString description;
};

AddResourceResult promptForResource(QWidget* _parent,
                                    const QStringList& _existingCategories)
{
    AddResourceResult result;
    QDialog dialog(_parent);
    dialog.setWindowTitle(QObject::tr("Añadir recurso a la escena"));

    auto* categoryCombo = new QComboBox(&dialog);
    categoryCombo->setEditable(true);
    if (_existingCategories.isEmpty()) {
        categoryCombo->addItems({
            QObject::tr("Props"),
            QObject::tr("Vestuario"),
            QObject::tr("Vehículos"),
            QObject::tr("Animales"),
            QObject::tr("Maquillaje/SFX"),
            QObject::tr("VFX"),
            QObject::tr("Armas"),
            QObject::tr("Música"),
            QObject::tr("Stunts"),
            QObject::tr("Otros"),
        });
    } else {
        categoryCombo->addItems(_existingCategories);
    }

    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(QObject::tr("Nombre del recurso (ej. Pistola, Vestido rojo)"));

    auto* qtySpin = new QSpinBox(&dialog);
    qtySpin->setRange(1, 9999);
    qtySpin->setValue(1);

    auto* descEdit = new QLineEdit(&dialog);
    descEdit->setPlaceholderText(QObject::tr("Detalle opcional para esta escena"));

    auto* form = new QFormLayout;
    form->addRow(QObject::tr("Categoría:"), categoryCombo);
    form->addRow(QObject::tr("Recurso:"), nameEdit);
    form->addRow(QObject::tr("Cantidad:"), qtySpin);
    form->addRow(QObject::tr("Detalle escena:"), descEdit);

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttonBox);

    nameEdit->setFocus();
    if (dialog.exec() == QDialog::Accepted && !nameEdit->text().trimmed().isEmpty()) {
        result.accepted = true;
        result.categoryName = categoryCombo->currentText().trimmed();
        result.resourceName = nameEdit->text().trimmed();
        result.qty = qtySpin->value();
        result.description = descEdit->text().trimmed();
    }
    return result;
}

} // anonymous namespace

void ScreenplayBreakdownNativeView::onAddResourceClicked()
{
    if (d->selectedRow < 0 || d->selectedRow >= d->sceneCache.size()
        || d->screenplayModel.isNull()) {
        return;
    }
    auto* scene = d->sceneCache[d->selectedRow];
    auto* dictionaries = d->screenplayModel->dictionariesModel();
    if (scene == nullptr || dictionaries == nullptr) {
        return;
    }

    //
    // Recolectar nombres de categorías existentes en el diccionario para el combo
    //
    QStringList existingCategoryNames;
    for (const auto& cat : dictionaries->resourceCategories()) {
        if (!cat.name.isEmpty()) {
            existingCategoryNames << cat.name;
        }
    }

    const auto input = promptForResource(this, existingCategoryNames);
    if (!input.accepted) {
        return;
    }

    //
    // Encontrar/crear categoría
    //
    QUuid categoryUuid;
    for (const auto& cat : dictionaries->resourceCategories()) {
        if (cat.name.compare(input.categoryName, Qt::CaseInsensitive) == 0) {
            categoryUuid = cat.uuid;
            break;
        }
    }
    if (categoryUuid.isNull()) {
        //
        // Crear nueva categoría con color estilo StudioBinder (mapa de colores
        // industry-standard); fallback rotativo si la categoría es custom.
        //
        const QColor catColor = colorForCategory(input.categoryName,
                                                 dictionaries->resourceCategories().size());
        dictionaries->addResourceCategory(input.categoryName,
                                          QString::fromUtf8(u8"\U000F0766"), // tag icon
                                          catColor, false);
        for (const auto& cat : dictionaries->resourceCategories()) {
            if (cat.name == input.categoryName) {
                categoryUuid = cat.uuid;
                break;
            }
        }
    }

    //
    // Encontrar/crear recurso dentro de la categoría
    //
    QUuid resourceUuid;
    for (const auto& r : dictionaries->resources()) {
        if (r.categoryUuid == categoryUuid
            && r.name.compare(input.resourceName, Qt::CaseInsensitive) == 0) {
            resourceUuid = r.uuid;
            break;
        }
    }
    if (resourceUuid.isNull()) {
        dictionaries->addResource(categoryUuid, input.resourceName, QString());
        for (const auto& r : dictionaries->resources()) {
            if (r.categoryUuid == categoryUuid && r.name == input.resourceName) {
                resourceUuid = r.uuid;
                break;
            }
        }
    }
    if (resourceUuid.isNull()) {
        return;
    }

    //
    // Asignar a la escena
    //
    scene->storeResource(resourceUuid, input.qty, input.description);

    //
    // Refrescar UI
    //
    refreshResourcesList();
    //
    // También actualizar la columna "Recursos" en la tabla
    //
    if (auto* item = d->sceneTableModel->item(d->selectedRow, 2)) {
        item->setText(QString::number(scene->resources().size()));
    }
}

void ScreenplayBreakdownNativeView::onRemoveResourceClicked()
{
    if (d->selectedRow < 0 || d->selectedRow >= d->sceneCache.size()) {
        return;
    }
    auto* current = d->resourcesList->currentItem();
    if (current == nullptr) {
        return;
    }
    const auto resourceUuid = current->data(Qt::UserRole).toUuid();
    if (resourceUuid.isNull()) {
        return;
    }
    auto* scene = d->sceneCache[d->selectedRow];
    if (scene == nullptr) {
        return;
    }
    scene->removeResource(resourceUuid);
    refreshResourcesList();
    if (auto* item = d->sceneTableModel->item(d->selectedRow, 2)) {
        item->setText(QString::number(scene->resources().size()));
    }
}

void ScreenplayBreakdownNativeView::onExportClicked()
{
    if (d->screenplayModel.isNull() || d->sceneCache.isEmpty()) {
        QMessageBox::information(this, tr("Exportar breakdown"),
                                 tr("No hay escenas para exportar. Abre un proyecto con guion."));
        return;
    }

    const QString defaultDir
        = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested
        = QStringLiteral("%1/breakdown.pdf").arg(defaultDir);

    QString selectedFilter;
    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("Exportar breakdown"), suggested,
        tr("PDF (*.pdf);;CSV (*.csv)"), &selectedFilter);
    if (chosen.isEmpty()) {
        return;
    }

    QString path = chosen;
    const QString lower = path.toLower();
    const bool isCsv = selectedFilter.contains(QStringLiteral("csv"), Qt::CaseInsensitive)
        || lower.endsWith(QStringLiteral(".csv"));
    if (isCsv) {
        if (!lower.endsWith(QStringLiteral(".csv"))) {
            path += QStringLiteral(".csv");
        }
        exportToCsv(path);
    } else {
        if (!lower.endsWith(QStringLiteral(".pdf"))) {
            path += QStringLiteral(".pdf");
        }
        exportToPdf(path);
    }
}

void ScreenplayBreakdownNativeView::exportToCsv(const QString& _filePath) const
{
    QFile file(_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(const_cast<ScreenplayBreakdownNativeView*>(this),
                             tr("Error exportando"),
                             tr("No se pudo escribir el archivo: %1").arg(_filePath));
        return;
    }
    QTextStream out(&file);
    //
    // CSV con BOM UTF-8 para que Excel/Numbers en macOS reconozca acentos
    //
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out << "# escena,heading,categoría,recurso,cantidad,detalle\n";

    auto* dictionaries = d->screenplayModel->dictionariesModel();
    auto escape = [](const QString& _s) {
        QString s = _s;
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(s);
    };

    int idx = 1;
    for (auto* scene : d->sceneCache) {
        if (scene == nullptr) {
            ++idx;
            continue;
        }
        const QString heading = scene->heading();
        const auto resources = scene->resources();
        if (resources.isEmpty()) {
            out << idx << ',' << escape(heading) << ",,,,\n";
        } else {
            for (const auto& sr : resources) {
                QString categoryName;
                QString resourceName;
                if (dictionaries != nullptr) {
                    const auto resource = dictionaries->resource(sr.uuid);
                    resourceName = resource.name;
                    if (!resource.categoryUuid.isNull()) {
                        categoryName
                            = dictionaries->resourceCategory(resource.categoryUuid).name;
                    }
                }
                out << idx << ',' << escape(heading) << ',' << escape(categoryName) << ','
                    << escape(resourceName) << ',' << sr.qty << ',' << escape(sr.description)
                    << '\n';
            }
        }
        ++idx;
    }
    file.close();

    d->statusLabel->setText(tr("Exportado a %1").arg(QFileInfo(_filePath).fileName()));
}

void ScreenplayBreakdownNativeView::exportToPdf(const QString& _filePath) const
{
    QPdfWriter writer(_filePath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    writer.setResolution(96);

    QTextDocument doc;
    doc.setDefaultStyleSheet(
        "h1 { font-size: 18pt; margin-bottom: 8pt; }"
        "h2 { font-size: 12pt; color: #444; margin-top: 12pt; margin-bottom: 4pt; }"
        "table { border-collapse: collapse; width: 100%; }"
        "th { background: #EEE; padding: 6px; text-align: left; border: 1px solid #888; }"
        "td { padding: 4px 6px; border: 1px solid #CCC; vertical-align: top; }"
        ".cat { font-weight: bold; }");

    QString html;
    html += QStringLiteral("<h1>Desglose del guion</h1>");
    html += QStringLiteral("<p>Generado por Aula 122 — %1 escenas</p>")
                .arg(d->sceneCache.size());

    auto* dictionaries = d->screenplayModel->dictionariesModel();
    int idx = 1;
    for (auto* scene : d->sceneCache) {
        if (scene == nullptr) {
            ++idx;
            continue;
        }
        const auto resources = scene->resources();
        html += QStringLiteral("<h2>%1. %2</h2>").arg(idx).arg(scene->heading().toHtmlEscaped());
        if (resources.isEmpty()) {
            html += QStringLiteral("<p><i>Sin recursos asignados.</i></p>");
        } else {
            html += QStringLiteral(
                "<table><thead><tr><th>Categoría</th><th>Recurso</th>"
                "<th>Cantidad</th><th>Detalle</th></tr></thead><tbody>");
            for (const auto& sr : resources) {
                QString categoryName;
                QString resourceName;
                if (dictionaries != nullptr) {
                    const auto resource = dictionaries->resource(sr.uuid);
                    resourceName = resource.name;
                    if (!resource.categoryUuid.isNull()) {
                        categoryName
                            = dictionaries->resourceCategory(resource.categoryUuid).name;
                    }
                }
                html += QStringLiteral(
                    "<tr><td class='cat'>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                            .arg(categoryName.toHtmlEscaped(),
                                 resourceName.toHtmlEscaped(),
                                 QString::number(sr.qty),
                                 sr.description.toHtmlEscaped());
            }
            html += QStringLiteral("</tbody></table>");
        }
        ++idx;
    }

    doc.setHtml(html);
    doc.print(&writer);

    d->statusLabel->setText(tr("Exportado a %1").arg(QFileInfo(_filePath).fileName()));
}

void ScreenplayBreakdownNativeView::updateTranslations()
{
    d->titleLabel->setText(tr("Desglose del guion"));
    d->sceneTableModel->setHorizontalHeaderLabels({
        tr("#"),
        tr("Heading"),
        tr("Recursos"),
    });
    d->addResourceButton->setText(tr("Añadir recurso"));
    d->removeResourceButton->setText(tr("Quitar"));
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

    d->detailHeading->setFont(DesignSystem::font().subtitle2());
    d->detailHeading->setStyleSheet(QString("color: %1;").arg(bodyColor));

    d->resourcesList->setFont(DesignSystem::font().body2());
    d->resourcesList->setStyleSheet(
        QString("QListWidget { color: %1; background: %2; border: 1px solid %3; }")
            .arg(bodyColor,
                 DesignSystem::color().background().name(),
                 DesignSystem::color().onBackground().name()));

    d->addResourceButton->setFont(DesignSystem::font().button());
    d->removeResourceButton->setFont(DesignSystem::font().button());

    d->statusLabel->setFont(DesignSystem::font().caption());
    d->statusLabel->setStyleSheet(QString("color: %1;").arg(bodyColor));
}

} // namespace Ui
