#include "screenplay_breakdown_native_view.h"

#include <business_layer/model/abstract_model.h>
#include <business_layer/model/screenplay/screenplay_dictionaries_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model.h>
#include <business_layer/model/screenplay/text/screenplay_text_model_scene_item.h>
#include <business_layer/model/text/text_model_folder_item.h>
#include <business_layer/model/text/text_model_group_item.h>
#include <business_layer/model/text/text_model_text_item.h>
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
#include <QDir>
#include <QPdfWriter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTableView>
#include <QTextEdit>
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
    QPushButton* autoExtractButton = nullptr;

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
    , autoExtractButton(new QPushButton(_q))
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

    autoExtractButton->setText(QStringLiteral("Auto-extraer con Claude"));
    autoExtractButton->setToolTip(QStringLiteral(
        "Pide a Claude que analice tu guion y sugiera recursos por escena"));

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
 * @brief Recolecta recursivamente el TEXTO plano de una escena (cabecera + accion +
 *        dialogo) de sus items de tipo Text. Acotado a _maxChars para no inflar el
 *        prompt que se manda a Claude.
 */
void collectSceneText(BusinessLayer::TextModelItem* _item, QString& _out, int _maxChars)
{
    if (_item == nullptr || _out.length() >= _maxChars) {
        return;
    }
    for (int i = 0; i < _item->childCount(); ++i) {
        if (_out.length() >= _maxChars) {
            return;
        }
        auto* child = _item->childAt(i);
        if (child->type() == BusinessLayer::TextModelItemType::Text) {
            const auto* textItem = static_cast<BusinessLayer::TextModelTextItem*>(child);
            const QString s = textItem->text().trimmed();
            if (!s.isEmpty()) {
                _out += s;
                _out += QLatin1Char('\n');
            }
        } else {
            collectSceneText(child, _out, _maxChars);
        }
    }
}

/**
 * @brief Texto de una escena (para alimentar a Claude), capado a _maxChars.
 */
QString sceneText(BusinessLayer::TextModelItem* _scene, int _maxChars = 700)
{
    QString out;
    collectSceneText(_scene, out, _maxChars);
    out = out.trimmed();
    if (out.length() > _maxChars) {
        out = out.left(_maxChars).trimmed() + QStringLiteral("…");
    }
    return out;
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
    headerRow->addWidget(d->autoExtractButton);
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
    connect(d->autoExtractButton, &QPushButton::clicked, this,
            &ScreenplayBreakdownNativeView::onAutoExtractClicked);
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

namespace {

/**
 * @brief Buscar el CLI `claude` en ubicaciones típicas o en PATH.
 *        Duplica la lógica de ClaudeClient::findClaudeCli del plugin
 *        writing_assistant — se considera mover a corelib en bloque futuro.
 */
QString locateClaudeCli()
{
    const QStringList candidates = {
        QDir::homePath() + QStringLiteral("/.local/bin/claude"),
        QStringLiteral("/opt/homebrew/bin/claude"),
        QStringLiteral("/usr/local/bin/claude"),
    };
    for (const auto& path : candidates) {
        if (QFileInfo(path).isExecutable()) {
            return path;
        }
    }
    QProcess which;
    which.start(QStringLiteral("/usr/bin/which"), { QStringLiteral("claude") });
    if (which.waitForFinished(2000) && which.exitCode() == 0) {
        const QString out = QString::fromUtf8(which.readAllStandardOutput()).trimmed();
        if (!out.isEmpty() && QFileInfo(out).isExecutable()) {
            return out;
        }
    }
    return QString();
}

/**
 * @brief Modal de progreso + resultado para auto-extract con Claude.
 *        Bloquea la UI mientras Claude procesa (puede tardar 30-90s para
 *        guiones largos). Muestra el CSV crudo recibido para que el
 *        usuario verifique antes de aplicar.
 */
class AutoExtractDialog : public QDialog
{
public:
    AutoExtractDialog(const QString& _cliPath, const QString& _prompt, QWidget* _parent)
        : QDialog(_parent)
    {
        setWindowTitle(QObject::tr("Auto-extraer recursos con Claude"));
        resize(700, 500);

        m_statusLabel = new QLabel(QObject::tr("Procesando con Claude... puede tardar 30-90s."),
                                   this);
        m_outputEdit = new QTextEdit(this);
        m_outputEdit->setReadOnly(true);
        m_outputEdit->setPlaceholderText(QObject::tr("La respuesta de Claude aparecerá aquí."));

        m_buttons = new QDialogButtonBox(this);
        m_applyButton = new QPushButton(QObject::tr("Aplicar al breakdown"), this);
        m_applyButton->setEnabled(false);
        m_buttons->addButton(m_applyButton, QDialogButtonBox::AcceptRole);
        m_buttons->addButton(QDialogButtonBox::Cancel);
        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto* layout = new QVBoxLayout(this);
        layout->addWidget(m_statusLabel);
        layout->addWidget(m_outputEdit, 1);
        layout->addWidget(m_buttons);

        m_process = new QProcess(this);
        m_process->setProgram(_cliPath);
        m_process->setStandardInputFile(QProcess::nullDevice());
        m_process->setArguments({
            QStringLiteral("--print"),
            QStringLiteral("--output-format"),
            QStringLiteral("text"),
            _prompt,
        });
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int _code, QProcess::ExitStatus) {
                    const QString stdout_ = QString::fromUtf8(m_process->readAllStandardOutput());
                    const QString stderr_ = QString::fromUtf8(m_process->readAllStandardError());
                    if (_code != 0 && stdout_.isEmpty()) {
                        m_outputEdit->setPlainText(QObject::tr("Error CLI: %1").arg(stderr_));
                        m_statusLabel->setText(QObject::tr("Error al ejecutar Claude."));
                        return;
                    }
                    m_outputEdit->setPlainText(stdout_);
                    m_csvOutput = stdout_;
                    m_applyButton->setEnabled(true);
                    m_statusLabel->setText(QObject::tr(
                        "Listo. Revisa el CSV de Claude y aplica si está bien."));
                });
        m_process->start();
    }

    QString csv() const { return m_csvOutput; }

private:
    QLabel* m_statusLabel = nullptr;
    QTextEdit* m_outputEdit = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton* m_applyButton = nullptr;
    QProcess* m_process = nullptr;
    QString m_csvOutput;
};

} // anonymous namespace

void ScreenplayBreakdownNativeView::onAutoExtractClicked()
{
    if (d->screenplayModel.isNull() || d->sceneCache.isEmpty()) {
        QMessageBox::information(this, tr("Auto-extract"),
                                 tr("No hay escenas para analizar. Abre un proyecto."));
        return;
    }
    const QString cliPath = locateClaudeCli();
    if (cliPath.isEmpty()) {
        QMessageBox::warning(this, tr("Auto-extract"),
                             tr("Claude CLI no encontrado. Instálalo desde "
                                "https://docs.claude.com/claude-code y haz "
                                "'claude auth login --claudeai'."));
        return;
    }

    //
    // Construir resumen de escenas para enviar a Claude
    //
    // Aula 122: ahora enviamos, por escena, su cabecera Y su CONTENIDO real (accion +
    // dialogo, capado por escena) en vez de solo la cabecera → la inferencia de recursos
    // de Claude es mucho mas precisa (ve lo que de verdad pasa en la escena).
    QStringList sceneBlocks;
    int idx = 1;
    for (auto* scene : d->sceneCache) {
        if (scene == nullptr) {
            ++idx;
            continue;
        }
        const int n = idx++;
        QString block = QStringLiteral("=== Escena %1: %2 ===").arg(n).arg(scene->heading());
        const QString contenido = sceneText(scene);
        if (!contenido.isEmpty()) {
            block += QLatin1Char('\n');
            block += contenido;
        }
        sceneBlocks << block;
    }

    //
    // Prompt: que Claude responda en CSV estricto para que sea parseable
    //
    const QString prompt
        = QStringLiteral(
              "Eres un asistente de pre-producción cinematográfica. Te paso, por escena, "
              "su cabecera y su CONTENIDO (acción y diálogo). Para cada escena, lista los "
              "recursos físicos que REALMENTE aparecen o se mencionan en el contenido "
              "(props notables, vestuario distintivo, vehículos, armas, animales, efectos, "
              "música cue). Responde ÚNICAMENTE con líneas CSV en este formato exacto, sin "
              "cabecera ni comentarios ni markdown:\n\n"
              "numero_escena,categoria,recurso,cantidad,detalle\n\n"
              "Donde categoria es una de: Props, Vestuario, Vehículos, Animales, "
              "Maquillaje/SFX, VFX, Armas, Música, Stunts, Otros. "
              "Si una escena no necesita recursos físicos especiales, no la "
              "incluyas. Sé conservador — solo lo que realmente se vea o se "
              "mencione en el contenido, no inventes.\n\n"
              "Escenas del guion:\n%1")
              .arg(sceneBlocks.join(QStringLiteral("\n\n")));

    AutoExtractDialog dialog(cliPath, prompt, this);
    if (dialog.exec() == QDialog::Accepted) {
        applyAutoExtractedCsv(dialog.csv());
    }
}

void ScreenplayBreakdownNativeView::applyAutoExtractedCsv(const QString& _csv)
{
    if (d->screenplayModel.isNull()) {
        return;
    }
    auto* dictionaries = d->screenplayModel->dictionariesModel();
    if (dictionaries == nullptr) {
        return;
    }

    int applied = 0;
    int skipped = 0;
    const auto lines = _csv.split(QChar('\n'), Qt::SkipEmptyParts);
    for (const auto& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QChar('#'))) {
            continue;
        }
        //
        // Parseo CSV simple — no maneja comillas escapadas pero el prompt
        // pide formato simple. Si Claude se desvía, las líneas se ignoran.
        //
        const QStringList parts = line.split(QChar(','));
        if (parts.size() < 4) {
            ++skipped;
            continue;
        }
        bool ok = false;
        const int sceneNum = parts[0].trimmed().toInt(&ok);
        if (!ok || sceneNum < 1 || sceneNum > d->sceneCache.size()) {
            ++skipped;
            continue;
        }
        const QString category = parts[1].trimmed();
        const QString resourceName = parts[2].trimmed();
        const int qty = parts[3].trimmed().toInt(&ok);
        if (category.isEmpty() || resourceName.isEmpty() || !ok || qty < 1) {
            ++skipped;
            continue;
        }
        const QString detail = parts.size() >= 5
            ? parts.mid(4).join(QChar(',')).trimmed()
            : QString();

        //
        // Resolver/crear categoría
        //
        QUuid categoryUuid;
        for (const auto& cat : dictionaries->resourceCategories()) {
            if (cat.name.compare(category, Qt::CaseInsensitive) == 0) {
                categoryUuid = cat.uuid;
                break;
            }
        }
        if (categoryUuid.isNull()) {
            const QColor catColor = colorForCategory(category,
                                                     dictionaries->resourceCategories().size());
            dictionaries->addResourceCategory(category,
                                              QString::fromUtf8(u8"\U000F0766"),
                                              catColor, false);
            for (const auto& cat : dictionaries->resourceCategories()) {
                if (cat.name == category) {
                    categoryUuid = cat.uuid;
                    break;
                }
            }
        }
        if (categoryUuid.isNull()) {
            ++skipped;
            continue;
        }

        //
        // Resolver/crear recurso
        //
        QUuid resourceUuid;
        for (const auto& r : dictionaries->resources()) {
            if (r.categoryUuid == categoryUuid
                && r.name.compare(resourceName, Qt::CaseInsensitive) == 0) {
                resourceUuid = r.uuid;
                break;
            }
        }
        if (resourceUuid.isNull()) {
            dictionaries->addResource(categoryUuid, resourceName, QString());
            for (const auto& r : dictionaries->resources()) {
                if (r.categoryUuid == categoryUuid && r.name == resourceName) {
                    resourceUuid = r.uuid;
                    break;
                }
            }
        }
        if (resourceUuid.isNull()) {
            ++skipped;
            continue;
        }

        //
        // Aplicar a la escena (sceneNum es 1-based)
        //
        auto* scene = d->sceneCache[sceneNum - 1];
        if (scene == nullptr) {
            ++skipped;
            continue;
        }
        scene->storeResource(resourceUuid, qty, detail);
        ++applied;
    }

    //
    // Refrescar UI
    //
    refreshSceneTable();
    QMessageBox::information(this, tr("Auto-extract completado"),
                             tr("Se añadieron %1 recursos al breakdown.\n"
                                "Líneas ignoradas: %2.").arg(applied).arg(skipped));
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
