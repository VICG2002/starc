#include "application_view.h"

#include <ui/design_system/design_system.h>
#include <ui/settings/theme_setup_view.h>
#include <ui/widgets/app_bar/app_bar.h>
#include <ui/widgets/label/label.h>
#include <ui/widgets/shadow/shadow.h>
#include <ui/widgets/splitter/splitter.h>
#include <ui/widgets/stack_widget/stack_widget.h>
#include <ui/widgets/task_bar/task_bar.h>
#include <utils/helpers/color_helper.h>
#include <utils/helpers/platform_helper.h>
#include <utils/logging.h>

#include <QCloseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVariantAnimation>


namespace Ui {

namespace {
const QString kSplitterState = "splitter/state";
const QString kViewGeometry = "view/geometry";
const QVector<int> kDefaultSizes = { 3, 7 };

} // namespace

class ApplicationView::Implementation
{
public:
    explicit Implementation(QWidget* _parent);

    Widget* navigationWidget = nullptr;
    StackWidget* toolBar = nullptr;
    StackWidget* navigator = nullptr;
    StackWidget* view = nullptr;

    QByteArray lastSplitterState;
    Splitter* splitter = nullptr;
    // Aula 122 / M2: Odiseo es el anfitrión. Vive PERMANENTE en odiseoHost, a la izquierda de
    // outerSplitter; el ensamblaje nativo (splitter interno) va a la derecha. Así el editor
    // nativo se muestra AL LADO de Odiseo sin reparentar su QWebEngineView (no recarga el chat):
    // solo se oculta/muestra/redimensiona.
    Widget* odiseoHost = nullptr;
    Splitter* outerSplitter = nullptr;
    // Aula 122: ocultar/restaurar el panel de navegación en la vista de Odiseo
    // (full-bleed) sin perder el ancho que dejó el usuario.
    bool navHiddenForView = false;
    QByteArray splitterStateBeforeView;
    // Aula 122: el usuario ocultó el CHAT de Odiseo desde su barra (el MENÚ se queda). Cuando es
    // true, el panel de Odiseo se encoge al ancho de su barra y el editor nativo gana el espacio.
    bool odiseoChatCollapsed = false;
    // Aula 122: mientras es true, una HERRAMIENTA de Odiseo ocupa TODO el ancho (editor nativo
    // oculto detrás). El reparto normal (chat 240 / 52-48) queda en pausa; al volver a false se
    // reaplica el modo recordado del chat.
    bool odiseoExpanded = false;
    // Aula 122: mientras es true, showContent() FUERZA oculto el navegador nativo (árbol de
    // documentos + barra de proyecto) → el menú de Odiseo es el único en esa pestaña. Lo activan las
    // vistas cuyo navegador es REDUNDANTE con la barra de Odiseo (proyecto, proyectos). Ajustes lo
    // deja en false (su navegador de secciones SÍ hace falta); cuenta/onboarding también (false).
    bool forceHideNativeNav = false;

    ThemeSetupView* themeSetupView = nullptr;

    IconsBigLabel* turnOffFullScreenIcon = nullptr;
};

ApplicationView::Implementation::Implementation(QWidget* _parent)
    : navigationWidget(new Widget(_parent))
    , toolBar(new StackWidget(_parent))
    , navigator(new StackWidget(_parent))
    , view(new StackWidget(_parent))
    , splitter(new Splitter(_parent))
    , odiseoHost(new Widget(_parent))
    , outerSplitter(new Splitter(_parent))
    , themeSetupView(new ThemeSetupView(_parent))
    , turnOffFullScreenIcon(new IconsBigLabel(_parent))
{
    new Shadow(view);
    auto splitterTopShadow = new Shadow(Qt::TopEdge, splitter);
    splitterTopShadow->setVisibilityAnchor(themeSetupView);

    turnOffFullScreenIcon->setIcon(u8"\U000F0294");
    turnOffFullScreenIcon->hide();
}


// ****


ApplicationView::ApplicationView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    d->view->installEventFilter(this);

    QVBoxLayout* navigationLayout = new QVBoxLayout(d->navigationWidget);
    navigationLayout->setContentsMargins({});
    navigationLayout->setSpacing(0);
    navigationLayout->addWidget(d->toolBar);
    navigationLayout->addWidget(d->navigator);

    d->splitter->setWidgets(d->navigationWidget, d->view);
    d->splitter->setSizes(kDefaultSizes);

    //
    // Aula 122 / M2: envolvemos el splitter interno (navegación + vista) en un splitter EXTERIOR
    // cuyo panel izquierdo (odiseoHost) hospeda a Odiseo de forma permanente. Por defecto va
    // OCULTO: las pantallas nativas ocupan todo el ancho (idéntico a hoy). showOdiseoFull()/
    // showOdiseoBeside() lo muestran como anfitrión o lado-a-lado sin reparentar la vista web.
    //
    {
        auto* odiseoLayout = new QVBoxLayout(d->odiseoHost);
        odiseoLayout->setContentsMargins({});
        odiseoLayout->setSpacing(0);
    }
    d->odiseoHost->hide();
    d->outerSplitter->setWidgets(d->odiseoHost, d->splitter);

    d->themeSetupView->hide();

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins({});
    layout->setSpacing(0);
    layout->addWidget(d->themeSetupView);
    layout->addWidget(d->outerSplitter, 1);


    connect(d->turnOffFullScreenIcon, &IconsBigLabel::clicked, this,
            &ApplicationView::turnOffFullScreenRequested);


#ifndef Q_OS_WIN
    //
    // Добавляем шорткат для выхода по Ctrl+Q
    //
    new QShortcut(QKeySequence("Ctrl+Q"), this, this, &ApplicationView::close,
                  Qt::ApplicationShortcut);
#endif
}

ApplicationView::~ApplicationView() = default;

ThemeSetupView* ApplicationView::themeSetupView() const
{
    return d->themeSetupView;
}

QWidget* ApplicationView::view() const
{
    return d->view;
}

int ApplicationView::slideViewOut()
{
    const auto finalWidth = DesignSystem::layout().px(1200);
    auto navigatorWidthAnimation = new QVariantAnimation(this);
    navigatorWidthAnimation->setDuration(40);
    navigatorWidthAnimation->setEasingCurve(QEasingCurve::OutQuart);
    navigatorWidthAnimation->setStartValue(width());
    navigatorWidthAnimation->setEndValue(
        static_cast<int>(finalWidth / std::accumulate(kDefaultSizes.begin(), kDefaultSizes.end(), 0)
                         * kDefaultSizes.constFirst()));
    auto fullWidthAnimation = new QVariantAnimation(this);
    fullWidthAnimation->setDuration(260);
    fullWidthAnimation->setEasingCurve(QEasingCurve::OutQuad);
    fullWidthAnimation->setStartValue(width());
    fullWidthAnimation->setEndValue(static_cast<int>(finalWidth));
    connect(fullWidthAnimation, &QVariantAnimation::valueChanged, this,
            [this, navigatorWidthAnimation](const QVariant& _value) {
                const auto width = _value.toInt();
                resize(width, height());
                const auto navigatorWidth = navigatorWidthAnimation->currentValue().toInt();
                d->splitter->setSizes({ navigatorWidth, width - navigatorWidth });

                move(screen()->availableGeometry().center() - QPoint(width / 2, height() / 2));
            });
    auto animation = new QParallelAnimationGroup(this);
    animation->addAnimation(fullWidthAnimation);
    animation->addAnimation(navigatorWidthAnimation);
    connect(
        animation, &QParallelAnimationGroup::stateChanged, this,
        [this, fullWidthAnimation, navigatorWidthAnimation](QAbstractAnimation::State _newState) {
            if (_newState == QAbstractAnimation::Running) {
                d->view->setMinimumSize(fullWidthAnimation->endValue().toInt()
                                            - navigatorWidthAnimation->endValue().toInt(),
                                        height());
            } else if (_newState == QAbstractAnimation::Stopped) {
                d->view->setMinimumSize(0, 0);
            }
        });
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    d->view->show();

    return animation->duration();
}

void ApplicationView::setHideNavigationButtonAvailable(bool _available)
{
    d->splitter->setHidePanelButtonAvailable(_available);
}

QVariantMap ApplicationView::saveState() const
{
    QVariantMap state;
    state[kSplitterState] = d->splitter->saveState();
    state[kViewGeometry] = saveGeometry();
    return state;
}

void ApplicationView::restoreState(bool _onboaringPassed, const QVariantMap& _state)
{
    //
    // Если это первый запуск, то конфигурируем геометрию под онбординг
    //
    if (!_onboaringPassed || _state.isEmpty()) {
        d->splitter->setSizes({ 1, 0 });
        d->view->hide();
        resize(Ui::DesignSystem::layout().px(468), Ui::DesignSystem::layout().px(740));
        move(screen()->availableGeometry().center() - QPoint(width() / 2, height() / 2));
        return;
    }

    //
    // Восстанавливаем геометрию окна
    //
    if (_state.contains(kViewGeometry)) {
        restoreGeometry(_state[kViewGeometry].toByteArray());
    }

    //
    // Почему-то иногда состояние геометрии может аффектить на видимость вьюхи, поэтому тут
    // принудительно показываем её, чтобы не словить кейс, когда приложение запущено, а интерфейс не
    // отображается
    //
    if (!isVisible()) {
        setVisible(true);
    }

    //
    // Иногда бывает так, что приложение после того, как станет видимым устанавливает геометрию окна
    // невалидной, поэтому сделана данная проверка и расширение размера вьюхи
    //
    constexpr int minSize = 100;
    if (height() < minSize || width() < minSize) {
        resize(Ui::DesignSystem::layout().px(1200), Ui::DesignSystem::layout().px(740));
        move(screen()->availableGeometry().center() - QPoint(width() / 2, height() / 2));
    }

    //
    // После того, как геометрия окна была окончательно настроена, восстановим состояние сплиттера
    //
    if (_state.contains(kSplitterState)) {
        d->splitter->restoreState(_state[kSplitterState].toByteArray());
    }

    //
    // Если пользователь закрыл приложение в полноэкранном состоянии, то при старте выходим из него
    //
    if (isFullScreen()) {
        toggleFullScreen(true);
        showMaximized();
    }
}

void ApplicationView::showContent(QWidget* _toolbar, QWidget* _navigator, QWidget* _view,
                                  bool _showNavigation)
{
    Log::debug("Show content: %1, %2, %3", _toolbar->metaObject()->className(),
               _navigator->metaObject()->className(), _view->metaObject()->className());

    d->toolBar->setCurrentWidget(_toolbar);
    d->navigator->setCurrentWidget(_navigator);
    d->view->setCurrentWidget(_view);

    //
    // Aula 122: la vista de Odiseo trae su propia barra lateral, así que ocultamos
    // el panel de navegación nativo (toolbar + navigator) para que no quede una
    // columna vacía a su lado. Guardamos el ancho antes de ocultar y lo restauramos
    // al volver a cualquier otra pantalla (_showNavigation = true por defecto).
    //
    if (!_showNavigation || d->forceHideNativeNav) {
        if (!d->navHiddenForView) {
            d->splitterStateBeforeView = d->splitter->saveState();
            d->navHiddenForView = true;
        }
        d->navigationWidget->setVisible(false);
    } else {
        d->navigationWidget->setVisible(true);
        if (d->navHiddenForView) {
            if (!d->splitterStateBeforeView.isEmpty()) {
                d->splitter->restoreState(d->splitterStateBeforeView);
            }
            d->navHiddenForView = false;
        }
    }

    //
    // Aula 122 / M2: por defecto el contenido nativo ocupa todo el ancho (Odiseo oculto). Los
    // modos anfitrión / lado-a-lado se activan con showOdiseoFull() / showOdiseoBeside() DESPUÉS
    // de showContent(). El Splitter cede el ancho al panel visible vía sus eventos Hide/Show.
    //
    d->splitter->setVisible(true);
    d->odiseoHost->setVisible(false);

    //
    // Фокусируем представление, после того, как оно будет отображено пользователю
    //
    QTimer::singleShot(d->view->animationDuration() * 1.3, this, [this] { d->view->setFocus(); });
}

void ApplicationView::setOdiseoWidget(QWidget* _odiseo)
{
    if (_odiseo == nullptr || _odiseo->parentWidget() == d->odiseoHost) {
        return;
    }
    d->odiseoHost->layout()->addWidget(_odiseo);
}

void ApplicationView::showOdiseoFull()
{
    //
    // Odiseo anfitrión a pantalla completa: mostramos su panel y ocultamos el ensamblaje
    // nativo. El Splitter cede todo el ancho al panel visible.
    //
    d->odiseoHost->setVisible(true);
    d->splitter->setVisible(false);
    d->forceHideNativeNav = true;
    //
    // Aula 122: a pantalla completa de Odiseo no hay nada que revelar al lado → sin botón de colapso.
    //
    d->outerSplitter->setHidePanelButtonAvailable(false, /*forLeftPanel=*/true);
}

void ApplicationView::showOdiseoBeside(bool _hideNativeNavigator)
{
    //
    // Odiseo + el editor nativo lado a lado, en la MISMA ventana. Odiseo (chats) a la izquierda,
    // el ensamblaje nativo (árbol de escenas + barra de formato + lienzo) a la derecha.
    //
    d->odiseoHost->setVisible(true);
    d->splitter->setVisible(true);
    //
    // Aula 122: si el navegador nativo es REDUNDANTE con la barra de Odiseo (proyecto/proyectos),
    // lo ocultamos y showContent() lo mantiene oculto en toda sub-navegación → el menú de Odiseo es
    // el único. Ajustes pasa false: su navegador de secciones (Aplicación/Componentes/Atajos/
    // Avanzado) NO lo replica Odiseo, así que se conserva (se reestiliza en vez de esconderse).
    //
    d->forceHideNativeNav = _hideNativeNavigator;
    if (_hideNativeNavigator) {
        d->navigationWidget->setVisible(false);
    }
    //
    // Aula 122: el botón "‹" del splitter escondía TODO Odiseo (menú + chat) → el usuario perdía la
    // navegación. Lo deshabilitamos: ahora el CHAT se oculta desde la propia barra de Odiseo
    // (setOdiseoChatCollapsed) y el MENÚ SIEMPRE se queda. Aplicamos el reparto según el estado
    // recordado (chat visible → 52/48; chat oculto → panel angosto = solo el menú).
    //
    d->outerSplitter->setHidePanelButtonAvailable(false, /*forLeftPanel=*/true);
    setOdiseoChatCollapsed(d->odiseoChatCollapsed);
}

void ApplicationView::showEditorFullWidth()
{
    //
    // Aula 122 (B): el menú LATERAL fijo de Odiseo desaparece y el ensamblaje nativo (editor) ocupa
    // TODO el ancho. El menú de Odiseo se invoca aparte como VENTANA FLOTANTE encima del editor (la
    // tuerca). Al ocultar odiseoHost (primer panel del outerSplitter), el Splitter da el 100% al
    // editor; forceHideNativeNav mantiene oculto el navegador nativo → editor a pantalla completa.
    //
    d->odiseoHost->setVisible(false);
    d->splitter->setVisible(true);
    d->forceHideNativeNav = true;
    d->outerSplitter->setHidePanelButtonAvailable(false, /*forLeftPanel=*/true);
}

void ApplicationView::setOdiseoChatCollapsed(bool _collapsed)
{
    d->odiseoChatCollapsed = _collapsed;
    //
    // Si Odiseo no está visible al lado (modo full o solo-nativo), solo recordamos el estado para
    // aplicarlo cuando se muestre (showOdiseoBeside lo vuelve a llamar).
    //
    if (!d->odiseoHost->isVisible()) {
        return;
    }
    if (_collapsed) {
        //
        // Encoge el panel de Odiseo al ancho de su barra. La barra del SPA mide 240 px CSS (=
        // px lógicos en el webview); con un pelín de margen llena el panel (su CSS la fuerza a
        // width:100% en este modo, así que el ancho exacto no es crítico). El editor nativo
        // se queda con el resto.
        //
        // 240px = ancho exacto de la barra del SPA en escritorio, y queda por encima del breakpoint
        // móvil (200px) → la barra se ve IDÉNTICA con el chat abierto o cerrado (sin adivinar nada).
        const int total = d->outerSplitter->width();
        constexpr int kSidebar = 240;
        d->outerSplitter->setSizes(total > kSidebar ? QVector<int>{ kSidebar, total - kSidebar }
                                                    : QVector<int>{ 52, 48 });
    } else {
        d->outerSplitter->setSizes({ 52, 48 });
    }
}

void ApplicationView::setOdiseoExpanded(bool _on)
{
    //
    // Igual que setOdiseoChatCollapsed: si Odiseo no está al lado (modo full o solo-nativo), solo
    // recordamos la intención (showOdiseoBeside reaplica el reparto al volver).
    //
    if (!d->odiseoHost->isVisible()) {
        d->odiseoExpanded = _on;
        return;
    }
    d->odiseoExpanded = _on;
    if (_on) {
        //
        // Ancho COMPLETO de Odiseo reusando el MISMO mecanismo del chat: setSizes sobre el
        // outerSplitter, que REPINTA ambos paneles (sin región sin pintar). El editor va a 0 de
        // ancho → la herramienta (position:fixed) cubre toda el área con la barra/menú visible, como
        // en Odiseo standalone. NUNCA ocultamos el panel del editor: hacerlo deja el área sin repintar
        // (ROJO), y además otros reajustes (setOdiseoChatCollapsed) lo reabrían a 52/48 con el panel
        // oculto → rojo. NO tocamos d->odiseoChatCollapsed: se conserva para restaurarlo al cerrar.
        //
        const int total = d->outerSplitter->width();
        d->outerSplitter->setSizes({ total, 0 });
    } else {
        //
        // Se cerró la última herramienta: restauramos el reparto PREVIO reaplicando el modo recordado
        // del chat (240px si compactado, 52/48 si visible). Reusar setOdiseoChatCollapsed evita
        // duplicar aritmética.
        //
        setOdiseoChatCollapsed(d->odiseoChatCollapsed);
    }
}

void ApplicationView::collapseNativeNavigator()
{
    //
    // Aula 122: en la vista de PROYECTO la navegación vive en la barra de Odiseo (que refleja el
    // árbol de documentos del .starc), así que ocultamos el navegador nativo (árbol + barra de
    // proyecto) para que el editor ocupe TODO el ancho del panel nativo, al lado de Odiseo. Es el
    // mismo mecanismo que usa la vista full-bleed de Odiseo: ocultar navigationWidget. Con el modo
    // "Odiseo al lado" activo, showContent() ya lo mantiene oculto en CUALQUIER pestaña; este método
    // deja el estado explícito en la vista de proyecto.
    //
    d->navigationWidget->setVisible(false);
}

void ApplicationView::allowNativeNavigator()
{
    //
    // Aula 122: desactiva el force-hide para que la PRÓXIMA showContent() respete _showNavigation y
    // muestre el navegador nativo (restaurando su ancho). Lo invocan ANTES de showContent las vistas
    // cuyo navegador NO replica Odiseo: cuenta, onboarding y Ajustes (su navegador de secciones).
    //
    d->forceHideNativeNav = false;
}

void ApplicationView::applyNativeMenuLayout()
{
    //
    // Aula 122 / menú nativo: reparto de la vista de proyecto. Diferimos al siguiente ciclo del bucle
    // de eventos para que el splitter ya tenga su tamaño definitivo (showProject corre en transición).
    // Odiseo (chat) angosto a la izquierda; el navegador NATIVO (menú) ~260px; el editor con el resto.
    //
    QTimer::singleShot(0, this, [this] {
        const int outerTotal = d->outerSplitter->width();
        if (outerTotal > 800) {
            const int odiseoW = qBound(300, outerTotal * 22 / 100, 400);
            d->outerSplitter->setSizes({ odiseoW, outerTotal - odiseoW });
        }
        QTimer::singleShot(0, this, [this] {
            const int innerTotal = d->splitter->width();
            if (innerTotal > 520) {
                constexpr int kNavWidth = 260;
                d->splitter->setSizes({ kNavWidth, innerTotal - kNavWidth });
            }
        });
    });
}

void ApplicationView::toggleFullScreen(bool _isFullScreen)
{
    if (!_isFullScreen) {
        d->lastSplitterState = d->splitter->saveState();
        //
        // Aula 122: el icono FLOTANTE de salir de pantalla completa se encimaba
        // al sidebar de Odiseo (esquina superior izquierda) y confundía. Ya no
        // se muestra: el botón ⛶ de la barra de borradores alterna entrar/salir
        // (cambia de icono según el modo) y el atajo de fullscreen sigue vivo.
        // Para reactivar el flotante: d->turnOffFullScreenIcon->show();
        //
    }

    d->navigationWidget->setVisible(_isFullScreen);

    if (_isFullScreen) {
        d->turnOffFullScreenIcon->hide();
        if (!d->lastSplitterState.isEmpty()) {
            d->splitter->restoreState(d->lastSplitterState);
        } else {
            d->splitter->setSizes(kDefaultSizes);
        }
    }
}

void ApplicationView::closeEvent(QCloseEvent* _event)
{
    //
    // Вместо реального закрытия формы сигнализируем об этом намерении
    //

    _event->ignore();
    emit closeRequested();
}

void ApplicationView::updateTranslations()
{
    d->turnOffFullScreenIcon->setToolTip(tr("Turn off full screen"));
}

void ApplicationView::designSystemChangeEvent(DesignSystemChangeEvent* _event)
{
    Q_UNUSED(_event)

    Log::trace("Init design system for the application view");

    setBackgroundColor(Ui::DesignSystem::color().primary());

    QPalette toolTipPalette;
    toolTipPalette.setColor(QPalette::ToolTipBase, Ui::DesignSystem::color().onSurface());
    toolTipPalette.setColor(QPalette::ToolTipText, Ui::DesignSystem::color().surface());
    QToolTip::setPalette(toolTipPalette);
    QToolTip::setFont(Ui::DesignSystem::font().subtitle2());

    d->navigationWidget->setBackgroundColor(DesignSystem::color().primary());

    d->toolBar->setBackgroundColor(DesignSystem::color().primary());
    d->toolBar->setFixedHeight(static_cast<int>(DesignSystem::appBar().heightRegular()));

    d->navigator->setBackgroundColor(DesignSystem::color().primary());

    d->view->setBackgroundColor(DesignSystem::color().surface());

    d->turnOffFullScreenIcon->raise();
    d->turnOffFullScreenIcon->setTextColor(Ui::DesignSystem::color().onSurface());
    d->turnOffFullScreenIcon->setBackgroundColor(Qt::transparent);
    d->turnOffFullScreenIcon->resize(d->turnOffFullScreenIcon->sizeHint());
    d->turnOffFullScreenIcon->move(Ui::DesignSystem::layout().px24(),
                                   Ui::DesignSystem::layout().px24());

    Log::trace("Register task bar");
    TaskBar::registerTaskBar(this, Ui::DesignSystem::color().primary(),
                             Ui::DesignSystem::color().onPrimary(),
                             Ui::DesignSystem::color().accent());

    Log::trace("Init window title bar theme");
    PlatformHelper::setTitleBarTheme(
        this, ColorHelper::isColorLight(Ui::DesignSystem::color().background()));

    Log::trace("Application view design system successfully initialized");
}

} // namespace Ui
