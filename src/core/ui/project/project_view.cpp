#include "project_view.h"

#include <business_layer/model/structure/structure_model_item.h>
#include <ui/design_system/design_system.h>
#include <ui/widgets/icon_button/icon_button.h>
#include <ui/widgets/label/label.h>
#include <ui/widgets/label/link_label.h>
#include <ui/widgets/tab_bar/tab_bar.h>
#include <utils/helpers/color_helper.h>

#include <QVBoxLayout>
#include <QVariantAnimation>


namespace Ui {

class ProjectView::Implementation
{
public:
    explicit Implementation(QWidget* _parent);


    Widget* defaultPage = nullptr;
    H6Label* defaultPageTitleLabel = nullptr;
    Body1Label* defaultPageBodyLabel = nullptr;
    Body1LinkLabel* defaultPageAddItemButton = nullptr;

    Widget* documentLoadingPage = nullptr;
    H6Label* documentLoadingPageTitleLabel = nullptr;
    Body1Label* documentLoadingPageBodyLabel = nullptr;

    Widget* notImplementedPage = nullptr;
    H6Label* notImplementedPageTitleLabel = nullptr;
    Body1Label* notImplementedPageBodyLabel = nullptr;

    Widget* documentEditorPage = nullptr;
    TabBar* documentDrafts = nullptr;
    QVariantAnimation documentDraftsHeightAnimation;
    //
    // Aula 122: botón "+" junto a la barra de borradores para crear uno nuevo
    // (visible en todo documento de texto editable, aunque aún no haya borradores)
    //
    IconButton* createDraftButton = nullptr;
    bool draftCreationEnabled = false;
    //
    // Aula 122: sprint de escritura y pantalla completa viven junto al "+"
    // (pedido de Victor 2026-06-09)
    //
    IconButton* sprintButton = nullptr;
    IconButton* fullscreenButton = nullptr;
    bool isFullScreenMode = false;
    StackWidget* documentEditor = nullptr;

    Widget* overlay = nullptr;
    QVariantAnimation overlayOpacityAnimation;
};

ProjectView::Implementation::Implementation(QWidget* _parent)
    : defaultPage(new Widget(_parent))
    , defaultPageTitleLabel(new H6Label(defaultPage))
    , defaultPageBodyLabel(new Body1Label(defaultPage))
    , defaultPageAddItemButton(new Body1LinkLabel(defaultPage))
    , documentLoadingPage(new Widget(_parent))
    , documentLoadingPageTitleLabel(new H6Label(documentLoadingPage))
    , documentLoadingPageBodyLabel(new Body1Label(documentLoadingPage))
    , notImplementedPage(new Widget(_parent))
    , notImplementedPageTitleLabel(new H6Label(notImplementedPage))
    , notImplementedPageBodyLabel(new Body1Label(notImplementedPage))
    , documentEditorPage(new Widget(_parent))
    , documentDrafts(new TabBar(documentEditorPage))
    , createDraftButton(new IconButton(documentEditorPage))
    , sprintButton(new IconButton(documentEditorPage))
    , fullscreenButton(new IconButton(documentEditorPage))
    , documentEditor(new StackWidget(documentEditorPage))
    , overlay(new Widget(_parent))
{
    defaultPage->setFocusPolicy(Qt::StrongFocus);
    defaultPageBodyLabel->setAlignment(Qt::AlignCenter);
    documentLoadingPage->setFocusPolicy(Qt::StrongFocus);
    documentLoadingPageBodyLabel->setAlignment(Qt::AlignCenter);
    notImplementedPage->setFocusPolicy(Qt::StrongFocus);
    notImplementedPageBodyLabel->setAlignment(Qt::AlignCenter);
    documentDrafts->hide();
    documentDrafts->setContextMenuPolicy(Qt::CustomContextMenu);
    createDraftButton->setIcon(u8"\U000F0415"); // plus (MDI)
    createDraftButton->hide();
    sprintButton->setIcon(u8"\U000F13AB"); // timer/sprint (MDI, mismo del ☰)
    sprintButton->hide();
    fullscreenButton->setIcon(u8"\U000F0293"); // fullscreen (MDI, mismo del ☰)
    fullscreenButton->hide();
    documentEditor->setAnimationType(AnimationType::FadeThrough);
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->hide();
    overlayOpacityAnimation.setDuration(180);
    overlayOpacityAnimation.setEasingCurve(QEasingCurve::OutQuad);
    overlayOpacityAnimation.setStartValue(0.0);
    overlayOpacityAnimation.setEndValue(1.0);

    {
        QVBoxLayout* layout = new QVBoxLayout(defaultPage);
        layout->setContentsMargins({});
        layout->setSpacing(0);
        layout->addStretch();
        layout->addWidget(defaultPageTitleLabel, 0, Qt::AlignHCenter);
        QHBoxLayout* bodyLayout = new QHBoxLayout;
        bodyLayout->setContentsMargins({});
        bodyLayout->setSpacing(0);
        bodyLayout->addStretch();
        bodyLayout->addWidget(defaultPageBodyLabel, 0, Qt::AlignHCenter);
        bodyLayout->addWidget(defaultPageAddItemButton, 0, Qt::AlignHCenter);
        bodyLayout->addStretch();
        layout->addLayout(bodyLayout);
        layout->addStretch();
    }

    {
        QVBoxLayout* layout = new QVBoxLayout(documentLoadingPage);
        layout->setContentsMargins({});
        layout->setSpacing(0);
        layout->addStretch();
        layout->addWidget(documentLoadingPageTitleLabel, 0, Qt::AlignHCenter);
        QHBoxLayout* bodyLayout = new QHBoxLayout;
        bodyLayout->setContentsMargins({});
        bodyLayout->setSpacing(0);
        bodyLayout->addStretch();
        bodyLayout->addWidget(documentLoadingPageBodyLabel, 0, Qt::AlignHCenter);
        bodyLayout->addStretch();
        layout->addLayout(bodyLayout);
        layout->addStretch();
    }

    {
        QVBoxLayout* layout = new QVBoxLayout(notImplementedPage);
        layout->setContentsMargins({});
        layout->setSpacing(0);
        layout->addStretch();
        layout->addWidget(notImplementedPageTitleLabel, 0, Qt::AlignHCenter);
        QHBoxLayout* bodyLayout = new QHBoxLayout;
        bodyLayout->setContentsMargins({});
        bodyLayout->setSpacing(0);
        bodyLayout->addStretch();
        bodyLayout->addWidget(notImplementedPageBodyLabel, 0, Qt::AlignHCenter);
        bodyLayout->addStretch();
        layout->addLayout(bodyLayout);
        layout->addStretch();
    }

    {
        auto layout = new QVBoxLayout(documentEditorPage);
        layout->setContentsMargins({});
        layout->setSpacing(0);
        //
        // Aula 122: fila de borradores = barra de pestañas + botón "+"
        //
        auto draftsLayout = new QHBoxLayout;
        draftsLayout->setContentsMargins({});
        draftsLayout->setSpacing(0);
        draftsLayout->addWidget(documentDrafts, 1);
        draftsLayout->addWidget(createDraftButton, 0, Qt::AlignVCenter);
        draftsLayout->addWidget(sprintButton, 0, Qt::AlignVCenter);
        draftsLayout->addWidget(fullscreenButton, 0, Qt::AlignVCenter);
        layout->addLayout(draftsLayout);
        layout->addWidget(documentEditor, 1);
    }

    documentDraftsHeightAnimation.setEasingCurve(QEasingCurve::OutQuad);
    documentDraftsHeightAnimation.setDuration(160);
}


// ****


ProjectView::ProjectView(QWidget* _parent)
    : StackWidget(_parent)
    , d(new Implementation(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setAnimationType(AnimationType::FadeThrough);

    addWidget(d->defaultPage);
    addWidget(d->documentLoadingPage);
    addWidget(d->notImplementedPage);
    addWidget(d->documentEditorPage);

    showDefaultPage();

    connect(d->defaultPageAddItemButton, &Body1LinkLabel::clicked, this,
            &ProjectView::createNewItemPressed);
    // Aula 122: botones de la barra de borradores ("+", sprint, pantalla completa)
    connect(d->createDraftButton, &IconButton::clicked, this,
            &ProjectView::createNewDraftPressed);
    connect(d->sprintButton, &IconButton::clicked, this, &ProjectView::sprintPressed);
    connect(d->fullscreenButton, &IconButton::clicked, this, &ProjectView::fullscreenPressed);
    connect(d->documentDrafts, &TabBar::currentIndexChanged, this, &ProjectView::showDraftPressed);
    connect(d->documentDrafts, &TabBar::customContextMenuRequested, this,
            [this](const QPoint _position) {
                //
                // Aula 122 (fix de crash): solo pedir el menú si el clic cae SOBRE una
                // pestaña de borrador. tabAt() devuelve -1 fuera de toda pestaña (área
                // vacía de la barra, doble clic, etc.) y aguas abajo eso reventaba.
                //
                const int draftIndex = d->documentDrafts->tabAt(_position);
                if (draftIndex < 0) {
                    return;
                }
                emit showDraftContextMenuPressed(draftIndex);
            });
    connect(&d->documentDraftsHeightAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& _value) { d->documentDrafts->setFixedHeight(_value.toInt()); });
    connect(&d->documentDraftsHeightAnimation, &QVariantAnimation::finished, this, [this] {
        if (d->documentDraftsHeightAnimation.direction() == QVariantAnimation::Backward) {
            d->documentDrafts->hide();
        }
        d->documentDrafts->setMinimumHeight(0);
        d->documentDrafts->setMaximumHeight(QWIDGETSIZE_MAX);
    });
    connect(&d->overlayOpacityAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& _value) { d->overlay->setOpacity(_value.toReal()); });
    connect(&d->overlayOpacityAnimation, &QVariantAnimation::finished, this, [this] {
        if (d->overlayOpacityAnimation.direction() == QVariantAnimation::Backward) {
            d->overlay->hide();
        }
    });
}

ProjectView::~ProjectView() = default;

void ProjectView::showDefaultPage()
{
    setCurrentWidget(d->defaultPage);
}

void ProjectView::showDocumentLoadingPage()
{
    setCurrentWidget(d->documentLoadingPage);
}

void ProjectView::showNotImplementedPage()
{
    setCurrentWidget(d->notImplementedPage);
}

QWidget* ProjectView::currentEditor() const
{
    if (currentWidget() != d->documentEditorPage) {
        return nullptr;
    }

    return d->documentEditor->currentWidget();
}

void ProjectView::showEditor(QWidget* _widget)
{
    setFocus();
    d->documentEditor->setCurrentWidget(_widget);
    setCurrentWidget(d->documentEditorPage);
}

void ProjectView::addEditor(QWidget* _widget)
{
    d->documentEditor->addWidget(_widget);
}

void ProjectView::setActive(bool _active)
{
    d->overlayOpacityAnimation.stop();
    d->overlayOpacityAnimation.setDirection(_active ? QVariantAnimation::Backward
                                                    : QVariantAnimation::Forward);
    d->overlayOpacityAnimation.start();
    if (!_active) {
        d->overlay->raise();
        d->overlay->show();
    }
}

void ProjectView::setDocumentDrafts(const BusinessLayer::StructureModelItem* _item)
{
    //
    // Aula 122: aquí había un early-return que ocultaba la barra cuando el
    // documento no tenía borradores. Se quitó (2026-06-09): la visibilidad la
    // decide el manager (showView y compañía) — con el botón "+" la barra ahora
    // puede mostrarse con la única pestaña del borrador actual.
    //

    //
    // Блокируем сигналы, чтобы менеджер не думал, что мы переключаемся тут между разными драфтами
    //
    QSignalBlocker blocker(this);

    const auto lastActiveDraft = d->documentDrafts->currentTab();

    d->documentDrafts->removeAllTabs();
    d->documentDrafts->addTab(_item->draftName(), u8"\U000F0765", _item->draftColor());
    for (const auto draft : _item->drafts()) {
        d->documentDrafts->addTab(draft->name(),
                                  draft->isComparison()
                                      ? u8"\U000F1492"
                                      : (draft->isReadOnly() ? u8"\U000F033E" : u8"\U000F0765"),
                                  draft->color());
    }

    d->documentDrafts->setCurrentTab(lastActiveDraft);

    d->documentDraftsHeightAnimation.setStartValue(0);
    d->documentDraftsHeightAnimation.setEndValue(d->documentDrafts->sizeHint().height());

    //
    // Aula 122: dimensionar aquí el botón "+" — el sizeHint del TabBar es 0
    // SIN pestañas, así que el designSystemChangeEvent del arranque (proyecto
    // restaurado, barra aún vacía) lo dejaba en 0x0 e invisible para siempre.
    // Tras reconstruir las pestañas el alto ya es real.
    //
    const int draftsBarHeight = d->documentDrafts->sizeHint().height();
    if (draftsBarHeight > 0) {
        d->createDraftButton->setFixedSize(draftsBarHeight, draftsBarHeight);
        d->sprintButton->setFixedSize(draftsBarHeight, draftsBarHeight);
        d->fullscreenButton->setFixedSize(draftsBarHeight, draftsBarHeight);
    }
}

void ProjectView::setDraftsVisible(bool _visible)
{
    //
    // Aula 122: los botones acompañan a la barra. Al ocultar se esconden de
    // inmediato (si quedaran visibles, la fila no colapsaría con la animación
    // de altura de la barra). El "+" además exige creación habilitada; sprint
    // y pantalla completa se muestran siempre que la barra esté visible.
    //
    d->createDraftButton->setVisible(_visible && d->draftCreationEnabled);
    d->sprintButton->setVisible(_visible);
    d->fullscreenButton->setVisible(_visible);

    const bool wasAnimationInterrupted
        = d->documentDraftsHeightAnimation.state() == QVariantAnimation::Running;
    if (wasAnimationInterrupted) {
        if ((d->documentDraftsHeightAnimation.direction() == QVariantAnimation::Forward && _visible)
            || (d->documentDraftsHeightAnimation.direction() == QVariantAnimation::Backward
                && !_visible)) {
            return;
        }
        d->documentDraftsHeightAnimation.stop();
    }

    //
    // Aula 122 (fix barra congelada): tras interrumpir la animación contraria
    // NO basta comparar isVisible() — la barra queda "visible" pero con altura
    // fija parcial (~0px) y todas las llamadas posteriores con true rebotaban
    // aquí, dejándola invisible para siempre (visto con los borradores de EDLP
    // al restaurar el proyecto). Tras una interrupción SIEMPRE se relanza la
    // animación en la dirección correcta.
    //
    if (!wasAnimationInterrupted && d->documentDrafts->isVisible() == _visible) {
        return;
    }

    if (_visible && !d->documentDrafts->isVisible()) {
        d->documentDrafts->setFixedHeight(0);
        d->documentDrafts->show();
    }
    d->documentDraftsHeightAnimation.setDirection(_visible ? QVariantAnimation::Forward
                                                           : QVariantAnimation::Backward);
    d->documentDraftsHeightAnimation.start();
}

int ProjectView::currentDraft() const
{
    return d->documentDrafts->currentTab();
}

void ProjectView::setCurrentDraft(int _index)
{
    d->documentDrafts->setCurrentTab(_index);
}

void ProjectView::setDraftCreationEnabled(bool _enabled)
{
    d->draftCreationEnabled = _enabled;
    d->createDraftButton->setVisible(_enabled && d->documentDrafts->isVisible());
}

void ProjectView::setFullScreenMode(bool _isFullScreen)
{
    d->isFullScreenMode = _isFullScreen;
    d->fullscreenButton->setIcon(_isFullScreen ? u8"\U000F0294"   // fullscreen-exit (MDI)
                                               : u8"\U000F0293"); // fullscreen (MDI)
    d->fullscreenButton->setToolTip(_isFullScreen ? tr("Salir de pantalla completa")
                                                  : tr("Pantalla completa"));
}

void ProjectView::resizeEvent(QResizeEvent* _event)
{
    StackWidget::resizeEvent(_event);

    d->overlay->resize(size());
}

void ProjectView::updateTranslations()
{
    d->defaultPageTitleLabel->setText(
        tr("Here will be an editor of the document you choose in the navigator (at left)."));
    d->defaultPageBodyLabel->setText(tr("Choose an item to edit, or"));
    d->defaultPageAddItemButton->setText(tr("create a new one"));

    d->documentLoadingPageTitleLabel->setText(tr("Document content loading..."));
    d->documentLoadingPageBodyLabel->setText(
        tr("Please, wait a while and document editor will be activated."));

    d->notImplementedPageTitleLabel->setText(
        tr("Ooops... looks like editor of this document not implemented yet."));
    d->notImplementedPageBodyLabel->setText(
        tr("But don't worry, it will be here in one of the future updates!"));

    d->createDraftButton->setToolTip(tr("Nuevo borrador"));
    d->sprintButton->setToolTip(tr("Sprint de escritura"));
    d->fullscreenButton->setToolTip(d->isFullScreenMode ? tr("Salir de pantalla completa")
                                                        : tr("Pantalla completa"));
}

void ProjectView::designSystemChangeEvent(DesignSystemChangeEvent* _event)
{
    StackWidget::designSystemChangeEvent(_event);

    setBackgroundColor(DesignSystem::color().surface());

    d->defaultPage->setBackgroundColor(DesignSystem::color().surface());
    d->defaultPageBodyLabel->setContentsMargins(0, static_cast<int>(DesignSystem::layout().px16()),
                                                static_cast<int>(DesignSystem::layout().px4()), 0);
    for (auto label : std::vector<Widget*>{
             d->defaultPageTitleLabel,
             d->defaultPageBodyLabel,
         }) {
        label->setBackgroundColor(DesignSystem::color().surface());
        label->setTextColor(DesignSystem::color().onSurface());
    }
    d->defaultPageAddItemButton->setContentsMargins(
        0, static_cast<int>(DesignSystem::layout().px16()), 0, 0);
    d->defaultPageAddItemButton->setBackgroundColor(DesignSystem::color().surface());
    d->defaultPageAddItemButton->setTextColor(DesignSystem::color().accent());

    d->documentLoadingPage->setBackgroundColor(DesignSystem::color().surface());
    d->documentLoadingPageBodyLabel->setContentsMargins(
        0, static_cast<int>(DesignSystem::layout().px16()),
        static_cast<int>(DesignSystem::layout().px4()), 0);
    for (auto label : std::vector<Widget*>{
             d->documentLoadingPageTitleLabel,
             d->documentLoadingPageBodyLabel,
         }) {
        label->setBackgroundColor(DesignSystem::color().surface());
        label->setTextColor(DesignSystem::color().onSurface());
    }

    d->notImplementedPage->setBackgroundColor(DesignSystem::color().surface());
    d->notImplementedPageBodyLabel->setContentsMargins(
        0, static_cast<int>(DesignSystem::layout().px16()),
        static_cast<int>(DesignSystem::layout().px4()), 0);
    for (auto label : std::vector<Widget*>{
             d->notImplementedPageTitleLabel,
             d->notImplementedPageBodyLabel,
         }) {
        label->setBackgroundColor(DesignSystem::color().surface());
        label->setTextColor(DesignSystem::color().onSurface());
    }

    d->documentEditorPage->setBackgroundColor(DesignSystem::color().surface());
    d->documentDrafts->setBackgroundColor(ColorHelper::nearby(DesignSystem::color().background()));
    d->documentDrafts->setTextColor(DesignSystem::color().onBackground());
    //
    // Aula 122: el botón "+" comparte estilo con la barra de borradores y se
    // acota a su misma altura para que la fila no crezca
    //
    for (auto button : { d->createDraftButton, d->sprintButton, d->fullscreenButton }) {
        button->setBackgroundColor(ColorHelper::nearby(DesignSystem::color().background()));
        button->setTextColor(DesignSystem::color().onBackground());
    }
    // (el alto real se fija en setDocumentDrafts; aquí solo si ya hay pestañas)
    const int draftsBarHeight = d->documentDrafts->sizeHint().height();
    if (draftsBarHeight > 0) {
        for (auto button : { d->createDraftButton, d->sprintButton, d->fullscreenButton }) {
            button->setFixedSize(draftsBarHeight, draftsBarHeight);
        }
    }
    d->documentEditor->setBackgroundColor(DesignSystem::color().surface());

    d->overlay->setBackgroundColor(backgroundColor());
    d->overlayOpacityAnimation.setEndValue(DesignSystem::focusBackgroundOpacity());
}

void ProjectView::setCurrentWidget(QWidget* _widget)
{
    StackWidget::setCurrentWidget(_widget);
}

} // namespace Ui
