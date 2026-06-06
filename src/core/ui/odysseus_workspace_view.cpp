#include "odysseus_workspace_view.h"

#include <functional>

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QNetworkCookie>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>


namespace Ui {

namespace {
const QString kOdysseusUrl = QStringLiteral("http://127.0.0.1:7860");

/**
 * @brief Lee el token de sesión admin que cachea BrainProcessManager tras el
 *        login (~/Library/Application Support/Diez50/Aula 122/odysseus_session).
 *        Devuelve cadena vacía si aún no existe (el workspace pedirá login).
 */
QString readSessionToken()
{
    const QString path = QDir::homePath()
        + QStringLiteral("/Library/Application Support/Diez50/Aula 122/odysseus_session");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}
} // namespace


bool OdysseusPage::acceptNavigationRequest(const QUrl& _url, NavigationType _type,
                                           bool _isMainFrame)
{
    Q_UNUSED(_type)
    Q_UNUSED(_isMainFrame)
    //
    // Puente "Aula 122" (JS->C++): el SPA navega a http://aula122.bridge/<verbo>...
    // Lo interceptamos y CANCELAMOS la navegación (return false) — esa URL nunca se
    // carga; solo sirve de canal de mensajes hacia el shell nativo. El resto de
    // navegaciones (127.0.0.1, enlaces internos del SPA) se permiten normalmente.
    //
    if (_url.host() == QLatin1String("aula122.bridge")) {
        //
        // Verbo "/theme": el SPA cambió de tema y manda los colores para sincronizar
        // la DesignSystem nativa (web→nativo). Hex de 6 dígitos sin '#'.
        //
        if (_url.path().startsWith(QLatin1String("/theme"))) {
            const QUrlQuery query(_url);
            emit themeRequested(query.queryItemValue(QStringLiteral("bg")),
                                query.queryItemValue(QStringLiteral("fg")),
                                query.queryItemValue(QStringLiteral("panel")),
                                query.queryItemValue(QStringLiteral("accent")),
                                query.queryItemValue(QStringLiteral("error")),
                                query.queryItemValue(QStringLiteral("mode")));
            //
            // Aula 122: el tema también puede traer la FUENTE de UI de Odiseo (param "font", una
            // familia CSS). La sincronizamos al nativo para que menús/paneles usen la misma fuente.
            //
            const QString font = query.queryItemValue(QStringLiteral("font"), QUrl::FullyDecoded);
            if (!font.isEmpty()) {
                emit fontRequested(font);
            }
            return false;
        }
        //
        // Verbo "/odiseo/chat?collapsed=0|1": el usuario ocultó/mostró el CHAT desde la barra de
        // Odiseo. El menú (barra) se queda; aquí encogemos/restauramos el panel de Odiseo para que
        // el editor nativo gane el espacio del chat.
        //
        if (_url.path().startsWith(QLatin1String("/odiseo/chat"))) {
            const QUrlQuery query(_url);
            emit chatCollapseRequested(query.queryItemValue(QStringLiteral("collapsed"))
                                       == QLatin1String("1"));
            return false;
        }
        //
        // Verbo "/odiseo/expand?on=0|1": el usuario abrió/cerró una HERRAMIENTA de Odiseo (Brain,
        // Email, Calendario, Tasks, Gallery, Cookbook, Compare, Deep Research, Notas, Ajustes,
        // Biblioteca…). on=1 → el panel de Odiseo ocupa TODO el ancho (el editor nativo se oculta
        // detrás, como en las pestañas propias de Odiseo, con la barra/menú visible). on=0 → se
        // cerró la última → restauramos el reparto anterior.
        //
        if (_url.path().startsWith(QLatin1String("/odiseo/expand"))) {
            const QUrlQuery query(_url);
            emit expandRequested(query.queryItemValue(QStringLiteral("on")) == QLatin1String("1"));
            return false;
        }
        //
        // Verbo "/odiseo/floatmenu": mostrar/ocultar el MENÚ en una ventana FLOTANTE encima del
        // editor (la tuerca del menú / la X del flotante).
        //
        if (_url.path().startsWith(QLatin1String("/odiseo/floatmenu"))) {
            emit floatMenuToggleRequested();
            return false;
        }
        //
        // Verbos de AJUSTES nativos (panel "Aula 122" dentro de los ajustes de Odiseo).
        //   /settings/native/get          → el shell empuja los ajustes nativos actuales (nativo→web).
        //   /settings/native/set?key&value → el shell cambia un ajuste reusando la lógica nativa.
        // Se manejan ANTES del fallback de /open/<etapa> para que "set"/"get" no se tomen por etapas.
        //
        if (_url.path().startsWith(QLatin1String("/settings/native/get"))) {
            emit nativeSettingsGetRequested();
            return false;
        }
        if (_url.path().startsWith(QLatin1String("/settings/native/set"))) {
            const QUrlQuery query(_url);
            emit nativeSettingChangeRequested(
                query.queryItemValue(QStringLiteral("key"), QUrl::FullyDecoded),
                query.queryItemValue(QStringLiteral("value"), QUrl::FullyDecoded));
            return false;
        }
        //
        // Verbo "/action/<nombre>": acción de app que antes solo vivía en el ☰ nativo (import,
        // save-as, fullscreen, cuenta, stats, sprint, …). Cerramos overlays web y la enrutamos al
        // shell, que la manda al MISMO slot del ☰ (paridad total, sin perder funciones).
        //
        if (_url.path().startsWith(QLatin1String("/action/"))) {
            runJavaScript(
                QStringLiteral("window.aula122CloseOverlays && window.aula122CloseOverlays()"));
            emit appActionRequested(_url.path().mid(QStringLiteral("/action/").length()));
            return false;
        }
        //
        // Verbos de proyecto: guardar / exportar el documento actual.
        //
        if (_url.path().startsWith(QLatin1String("/project/save"))) {
            emit saveProjectRequested();
            return false;
        }
        if (_url.path().startsWith(QLatin1String("/project/export"))) {
            emit exportProjectRequested();
            return false;
        }
        const QString path = _url.path();
        //
        // Verbo "/open/doc/<uuid>": abrir un DOCUMENTO concreto del proyecto (clic en un
        // personaje/locación/subdocumento del árbol de la barra de Odiseo). El uuid puede
        // contiener guiones, así que tomamos TODO lo que sigue al prefijo (no solo el último
        // segmento). Cierra overlays web y emite documentRequested(uuid).
        //
        if (path.startsWith(QLatin1String("/open/doc/"))) {
            runJavaScript(
                QStringLiteral("window.aula122CloseOverlays && window.aula122CloseOverlays()"));
            const QString uuid = path.mid(QStringLiteral("/open/doc/").length());
            if (!uuid.isEmpty()) {
                emit documentRequested(uuid);
            }
            return false;
        }
        //
        // Verbo "/open/add-document": abrir el diálogo nativo "Añadir documento".
        //
        if (path.startsWith(QLatin1String("/open/add-document"))) {
            runJavaScript(
                QStringLiteral("window.aula122CloseOverlays && window.aula122CloseOverlays()"));
            emit addDocumentRequested();
            return false;
        }
        //
        // Verbo "/open/<etapa>": cambiar a una vista NATIVA del pipeline. Antes de
        // mostrarla, cerramos cualquier overlay web (Guion/Idea/Bóveda) para no dejar
        // "dos cosas" a la vez (fix del bug de "dos pestañas").
        //
        runJavaScript(
            QStringLiteral("window.aula122CloseOverlays && window.aula122CloseOverlays()"));
        const QString view = path.section(QLatin1Char('/'), -1);
        if (!view.isEmpty()) {
            emit pipelineRequested(view);
        }
        return false;
    }
    return true;
}


OdysseusWorkspaceView::OdysseusWorkspaceView(QWidget* _parent)
    : QWidget(_parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_web = new QWebEngineView(this);
    layout->addWidget(m_web);

    //
    // Puente "Aula 122": instalamos una OdysseusPage que intercepta las
    // navegaciones del puente y reemite navigateRequested, para que la sidebar web
    // pueda cambiar a los módulos nativos. Reusa el perfil del page por defecto
    // (misma persistencia de cookies/sesión que antes).
    //
    auto* page = new OdysseusPage(m_web->page()->profile(), this);
    connect(page, &OdysseusPage::pipelineRequested, this,
            &OdysseusWorkspaceView::navigateRequested);
    connect(page, &OdysseusPage::documentRequested, this,
            &OdysseusWorkspaceView::documentRequested);
    connect(page, &OdysseusPage::addDocumentRequested, this,
            &OdysseusWorkspaceView::addDocumentRequested);
    connect(page, &OdysseusPage::chatCollapseRequested, this,
            &OdysseusWorkspaceView::chatCollapseRequested);
    connect(page, &OdysseusPage::expandRequested, this, &OdysseusWorkspaceView::expandRequested);
    connect(page, &OdysseusPage::floatMenuToggleRequested, this,
            &OdysseusWorkspaceView::toggleMenuOverlay);
    connect(page, &OdysseusPage::saveProjectRequested, this,
            &OdysseusWorkspaceView::saveProjectRequested);
    connect(page, &OdysseusPage::exportProjectRequested, this,
            &OdysseusWorkspaceView::exportProjectRequested);
    connect(page, &OdysseusPage::themeRequested, this, &OdysseusWorkspaceView::themeRequested);
    connect(page, &OdysseusPage::fontRequested, this, &OdysseusWorkspaceView::fontRequested);
    connect(page, &OdysseusPage::nativeSettingsGetRequested, this,
            &OdysseusWorkspaceView::nativeSettingsGetRequested);
    connect(page, &OdysseusPage::nativeSettingChangeRequested, this,
            &OdysseusWorkspaceView::nativeSettingChangeRequested);
    connect(page, &OdysseusPage::appActionRequested, this,
            &OdysseusWorkspaceView::appActionRequested);
    //
    // Aula 122 / UI unificada: al terminar cada carga (incluido el reload() tras ready()),
    // pedimos al SPA que reenvíe su tema actual → el nativo se alinea con Odiseo (maestro).
    //
    connect(page, &OdysseusPage::loadFinished, this, [this](bool _ok) {
        //
        // Aula 122 / UI unificada: al terminar la carga REAL del SPA le pedimos que reenvíe su tema.
        //
        if (_ok) {
            if (m_web->url().host() == QLatin1String("127.0.0.1")) {
                m_spaLoaded = true;
            }
            if (m_web->page() != nullptr) {
                m_web->page()->runJavaScript(
                    QStringLiteral("window.__aula122PushTheme && window.__aula122PushTheme()"));
                //
                // Aula 122 (fix navegación): re-aplicar el PROYECTO activo pedido antes de que el
                // SPA cargara. Sin esto el árbol del SPA quedaba en el proyecto por defecto del
                // backend (otro .starc) y sus uuids no encajaban con el modelo nativo → los
                // documentos no abrían. Va PRIMERO para que _load() del SPA use el proyecto correcto.
                //
                if (m_activeProjectSet && !m_activeProjectPath.isEmpty()) {
                    QString escaped = m_activeProjectPath;
                    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
                    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
                    m_web->page()->runJavaScript(
                        QStringLiteral(
                            "window.aula122SetProject && window.aula122SetProject(\"%1\")")
                            .arg(escaped));
                }
                //
                // Aula 122 (Etapa 1): re-aplicar el estado de chat colapsado pedido ANTES de que
                // el SPA cargara (showProject) → el editor-first del proyecto se respeta y el FAB
                // del SPA queda en el estado correcto (sincronizado con el split nativo).
                //
                if (m_chatCollapsedSet) {
                    m_web->page()->runJavaScript(
                        QStringLiteral(
                            "window.aula122SetChatCollapsed && window.aula122SetChatCollapsed(%1)")
                            .arg(m_chatCollapsed ? 1 : 0));
                }
                //
                // Aula 122: tras cargar el SPA, pedir al shell que empuje los ajustes nativos
                // actuales → el panel "Aula 122" de los ajustes de Odiseo queda fresco aunque se
                // haya pedido antes de cargar (mismo patrón de timing que tema/proyecto/chat).
                //
                emit nativeSettingsGetRequested();
            }
            return;
        }
        //
        // _ok == false. OJO: una navegación-puente CANCELADA (el push de tema a aula122.bridge, o
        // un /open de etapa) TAMBIÉN dispara loadFinished(false). Si el SPA YA cargó, eso NO es un
        // error → lo ignoramos (si no, mataríamos el SPA volviendo al splash en bucle). Solo si el
        // SPA aún no cargó (server abajo en el arranque) mostramos el splash y reanudamos el sondeo.
        //
        if (m_spaLoaded) {
            return;
        }
        showSplash();
        if (m_pollTimer != nullptr && !m_pollTimer->isActive()) {
            m_pollTimer->start();
        }
    });
    m_web->setPage(page);

    //
    // Aula 122: SPLASH. El server local de Odiseo (7860) tarda ~12 s en arrancar; si cargáramos la
    // URL antes, el webview mostraría su error de "página no encontrada". En vez de eso pintamos el
    // LOGO de Aula 122 DENTRO del propio webview (siempre visible: una vista oculta se suspende y no
    // termina de cargar). Cuando el token de sesión ya está cacheado (⇒ odysseus arriba + login admin
    // hecho) navegamos al SPA con auto-login. Sondeamos cada segundo; el reload() tras ready() es el
    // respaldo final.
    //
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        if (!readSessionToken().isEmpty()) {
            m_pollTimer->stop();
            reload();
        }
    });
    if (readSessionToken().isEmpty()) {
        showSplash();
        m_pollTimer->start();
    } else {
        reload();
    }
}

void OdysseusWorkspaceView::showSplash()
{
    //
    // Aula 122: pinta el logo de Aula 122 + "Cargando Odiseo…" DENTRO del webview, como HTML local.
    // El PNG va embebido en base64 desde el recurso :/images/logo, así NO depende de ningún server
    // (la gracia es justo que se vea antes de que Odiseo responda).
    //
    QString logoSrc;
    QFile logoFile(QStringLiteral(":/images/logo"));
    if (logoFile.open(QIODevice::ReadOnly)) {
        logoSrc = QStringLiteral("data:image/png;base64,")
            + QString::fromLatin1(logoFile.readAll().toBase64());
    }
    const QString html
        = QStringLiteral(
              "<!doctype html><html><head><meta charset='utf-8'></head>"
              "<body style='margin:0;height:100vh;display:flex;flex-direction:column;"
              "align-items:center;justify-content:center;background:#0d1117;"
              "font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;'>"
              "<img src='%1' style='width:240px;height:240px;' alt='Aula 122'>"
              "<div style='color:#9aa0a6;font-size:15px;margin-top:18px;'>Cargando Odiseo…</div>"
              "</body></html>")
              .arg(logoSrc);
    if (m_web != nullptr) {
        m_web->setHtml(html);
    }
}

OdysseusWorkspaceView::~OdysseusWorkspaceView() = default;

void OdysseusWorkspaceView::reload()
{
    //
    // Re-lee el token de sesión y vuelve a cargar. Útil cuando el cerebro queda
    // listo tras el arranque: la primera carga pudo fallar (server 7860 abajo) o
    // mostrar el login (token aún no cacheado).
    //
    m_loaded = false;
    loadWorkspace();
}

void OdysseusWorkspaceView::toggleMenuOverlay()
{
    //
    // Aula 122 (M3): MENÚ FLOTANTE NATIVO, ENCIMA del editor de STARC. Un 2º QWebEngineView en una
    // ventana top-level separada NO renderiza en macOS/Qt (queda en blanco), así que el flotante es
    // un menú NATIVO (botones) que emite las MISMAS señales que la barra web → mismos handlers del
    // ☰. Cubre la navegación por ETAPAS + acciones; los documentos individuales (cada personaje) y
    // los modales web (Bóveda/Hermes) siguen en la barra lateral. Ventana Qt::Tool (flota), parent
    // nullptr (no hija constreñida), posicionada sobre el área del editor y siguiendo la ventana.
    //
    if (m_menuOverlay != nullptr && m_menuOverlay->isVisible()) {
        m_menuOverlay->hide();
        return;
    }
    if (m_menuOverlay == nullptr) {
        //
        // FIX compositing: el overlay es un WIDGET HIJO de la ventana principal (no una ventana
        // top-level separada — esas no se componen en este macOS/Qt, salían en blanco). El editor de
        // STARC es Qt NATIVO, así que un hijo nativo superpuesto y elevado (raise) compone bien
        // (nativo-sobre-nativo). Se posiciona sobre el ÁREA DEL EDITOR (a la derecha de la barra de
        // Odiseo si está visible; todo el contenido si el editor está a pantalla completa).
        //
        m_menuOverlay = new QWidget(window());
        m_menuOverlay->setAutoFillBackground(true);
        m_menuOverlay->setStyleSheet(QStringLiteral(
            "QWidget{background:#16161c;color:#e6e6e6;}"
            "QPushButton{background:transparent;border:none;text-align:left;padding:8px 16px;"
            "font-size:13px;} QPushButton:hover{background:#2a2a34;}"));
        auto* outer = new QVBoxLayout(m_menuOverlay);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // Cabecera: "Aula 122" + cerrar (X).
        auto* header = new QWidget(m_menuOverlay);
        auto* hl = new QHBoxLayout(header);
        hl->setContentsMargins(16, 12, 8, 8);
        auto* titleLbl = new QLabel(QStringLiteral("Aula 122"), header);
        titleLbl->setStyleSheet(QStringLiteral("color:#3a6df0;font-weight:800;font-size:14px;"));
        auto* closeBtn = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), header);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setFixedSize(26, 26);
        closeBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;color:#8a8a93;border:none;font-size:14px;padding:0;}"
            "QPushButton:hover{color:#e6e6e6;}"));
        connect(closeBtn, &QPushButton::clicked, this, [this] {
            if (m_menuOverlay != nullptr) {
                m_menuOverlay->hide();
            }
        });
        hl->addWidget(titleLbl);
        hl->addStretch(1);
        hl->addWidget(closeBtn);
        outer->addWidget(header);

        auto* scroll = new QScrollArea(m_menuOverlay);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content = new QWidget(scroll);
        auto* col = new QVBoxLayout(content);
        col->setContentsMargins(0, 0, 0, 10);
        col->setSpacing(0);

        const auto addSection = [content, col](const QString& _t) {
            auto* lbl = new QLabel(_t, content);
            lbl->setStyleSheet(QStringLiteral(
                "color:#8a8a93;font-size:11px;font-weight:700;padding:12px 16px 4px;"));
            col->addWidget(lbl);
        };
        const auto addItem
            = [this, content, col](const QString& _label, std::function<void()> _act) {
                  auto* b = new QPushButton(_label, content);
                  b->setCursor(Qt::PointingHandCursor);
                  connect(b, &QPushButton::clicked, this, [this, _act] {
                      _act();
                      if (m_menuOverlay != nullptr) {
                          m_menuOverlay->hide();
                      }
                  });
                  col->addWidget(b);
              };

        addSection(tr("Documentos"));
        addItem(tr("Guion"), [this] { emit navigateRequested(QStringLiteral("guion")); });
        addItem(tr("Sinopsis / Idea"), [this] { emit navigateRequested(QStringLiteral("idea")); });
        addItem(tr("Personajes"), [this] { emit navigateRequested(QStringLiteral("personajes")); });
        addItem(tr("Locaciones"), [this] { emit navigateRequested(QStringLiteral("locaciones")); });
        addItem(tr("Añadir documento"), [this] { emit addDocumentRequested(); });
        addSection(tr("Producción"));
        addItem(tr("Desglose"), [this] { emit navigateRequested(QStringLiteral("desglose")); });
        addItem(tr("Plan de rodaje"),
                [this] { emit navigateRequested(QStringLiteral("plan-rodaje")); });
        addSection(tr("Proyecto"));
        addItem(tr("Proyectos"), [this] { emit navigateRequested(QStringLiteral("proyectos")); });
        addItem(tr("Guardar"), [this] { emit saveProjectRequested(); });
        addItem(tr("Exportar"), [this] { emit exportProjectRequested(); });
        addItem(tr("Importar…"), [this] { emit appActionRequested(QStringLiteral("import")); });
        addItem(tr("Guardar como…"),
                [this] { emit appActionRequested(QStringLiteral("save-as")); });
        addItem(tr("Ajustes"), [this] { emit navigateRequested(QStringLiteral("ajustes")); });
        addSection(tr("App"));
        addItem(tr("Pantalla completa"),
                [this] { emit appActionRequested(QStringLiteral("fullscreen")); });
        addItem(tr("Asistente"), [this] { emit appActionRequested(QStringLiteral("assistant")); });
        addItem(tr("Estadísticas"), [this] { emit appActionRequested(QStringLiteral("stats")); });
        addItem(tr("Sprint de escritura"),
                [this] { emit appActionRequested(QStringLiteral("sprint")); });
        addItem(tr("Cuenta"), [this] { emit appActionRequested(QStringLiteral("account")); });
        col->addStretch(1);
        scroll->setWidget(content);
        outer->addWidget(scroll, 1);

        // Seguir la ventana principal: reposicionar el flotante cuando se mueva/redimensione.
        if (auto* w = window()) {
            w->installEventFilter(this);
        }
    }
    positionOverlay();
    m_menuOverlay->show();
    m_menuOverlay->raise();
    m_menuOverlay->activateWindow();
}

void OdysseusWorkspaceView::positionOverlay()
{
    if (m_menuOverlay == nullptr) {
        return;
    }
    auto* w = window();
    if (w == nullptr) {
        return;
    }
    //
    // El overlay es HIJO de la ventana principal → coords RELATIVAS a su área de contenido. Lo
    // pegamos al ÁREA DEL EDITOR: si la barra de Odiseo (este widget, dentro de odiseoHost) está
    // visible, empezamos a SU DERECHA (sobre el editor, no sobre la barra web que no se compondría);
    // si el editor está a pantalla completa (Odiseo oculto), cubrimos desde el borde izquierdo.
    //
    constexpr int kOverlayWidth = 320;
    int left = 0;
    int top = 0;
    int height = w->height();
    QWidget* host = parentWidget(); // odiseoHost (panel de Odiseo)
    if (host != nullptr && host->isVisible() && host->width() > 0) {
        const QPoint tr = host->mapTo(w, QPoint(host->width(), 0));
        left = tr.x();
        top = tr.y();
        height = host->height();
    }
    const int width = qMin(kOverlayWidth, qMax(0, w->width() - left));
    m_menuOverlay->setGeometry(left, top, width, height);
    m_menuOverlay->raise();
}

bool OdysseusWorkspaceView::eventFilter(QObject* _watched, QEvent* _event)
{
    if (_watched == window() && m_menuOverlay != nullptr && m_menuOverlay->isVisible()) {
        switch (_event->type()) {
        case QEvent::Resize:
        case QEvent::WindowStateChange:
            // El overlay (hijo) se mueve con la ventana solo; al redimensionar hay que recolocarlo
            // sobre el editor.
            positionOverlay();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(_watched, _event);
}

void OdysseusWorkspaceView::showMenuGear()
{
    //
    // Aula 122 (B): la TUERCA (botón nativo) abajo-izquierda sobre el editor a pantalla completa.
    // Al picarla aparece/oculta el menú de Odiseo como ventana flotante encima del editor.
    //
    if (m_menuGear == nullptr) {
        m_menuGear = new QPushButton(QString::fromUtf8("\xE2\x9A\x99  Men\xC3\xBA"), window());
        m_menuGear->setCursor(Qt::PointingHandCursor);
        m_menuGear->setToolTip(tr("Mostrar el menú de Odiseo encima del editor"));
        m_menuGear->setStyleSheet(QStringLiteral(
            "QPushButton{background:rgba(26,26,32,0.92);color:#e6e6e6;"
            "border:1px solid rgba(255,255,255,0.14);border-radius:19px;padding:7px 16px;"
            "font-size:13px;font-weight:600;} QPushButton:hover{background:rgba(42,42,52,0.96);}"));
        m_menuGear->adjustSize();
        connect(m_menuGear, &QPushButton::clicked, this, &OdysseusWorkspaceView::toggleMenuOverlay);
    }
    if (auto* w = window()) {
        m_menuGear->move(16, w->height() - m_menuGear->height() - 16);
    }
    m_menuGear->show();
    m_menuGear->raise();
}

void OdysseusWorkspaceView::hideMenuGear()
{
    if (m_menuGear != nullptr) {
        m_menuGear->hide();
    }
    // Al salir de la vista de proyecto, también ocultamos el menú flotante si estaba abierto.
    if (m_menuOverlay != nullptr) {
        m_menuOverlay->hide();
    }
}

void OdysseusWorkspaceView::loadWorkspace()
{
    const QUrl url(kOdysseusUrl);

    const QString token = readSessionToken();
    if (token.isEmpty() || m_web->page() == nullptr) {
        //
        // Sin token cacheado: cargamos directo (el workspace mostrará su login;
        // tras entrar una vez, el perfil persistente recuerda la sesión).
        //
        m_loaded = true;
        m_web->load(url);
        return;
    }

    //
    // Auto-login: inyectar la cookie 'odysseus_session' (la misma que cachea
    // BrainProcessManager) en el cookie store del webview. setCookie es
    // asíncrono, así que NO navegamos hasta que la cookie quede registrada
    // (evita la carrera setCookie/navegación que mandaría a /login).
    //
    auto* cookieStore = m_web->page()->profile()->cookieStore();
    QNetworkCookie cookie(QByteArrayLiteral("odysseus_session"), token.toUtf8());
    cookie.setDomain(QStringLiteral("127.0.0.1"));
    cookie.setPath(QStringLiteral("/"));

    connect(cookieStore, &QWebEngineCookieStore::cookieAdded, this,
            [this, url](const QNetworkCookie& _cookie) {
                if (_cookie.name() == QByteArrayLiteral("odysseus_session") && !m_loaded) {
                    m_loaded = true;
                    m_web->load(url);
                }
            });
    cookieStore->setCookie(cookie, url);

    //
    // Fallback: si por lo que sea no llega cookieAdded, cargar de todos modos.
    //
    QTimer::singleShot(1500, this, [this, url]() {
        if (!m_loaded) {
            m_loaded = true;
            m_web->load(url);
        }
    });
}

void OdysseusWorkspaceView::applyThemeFromNative(const QString& _bg, const QString& _fg,
                                                 const QString& _panel, const QString& _accent,
                                                 const QString& _error, const QString& _mode)
{
    if (m_web == nullptr || m_web->page() == nullptr) {
        return;
    }
    //
    // Empuja el tema nativo al SPA (nativo→web). Los colores llegan con '#'.
    //
    const QString js
        = QStringLiteral("window.__aula122ApplyExternalTheme && window.__aula122ApplyExternalTheme("
                         "{bg:'%1',fg:'%2',panel:'%3',accent:'%4',error:'%5',mode:'%6'})")
              .arg(_bg, _fg, _panel, _accent, _error, _mode);
    m_web->page()->runJavaScript(js);
}

void OdysseusWorkspaceView::applyNativeSettings(const QString& _jsonObject)
{
    if (m_web == nullptr || m_web->page() == nullptr || _jsonObject.isEmpty()) {
        return;
    }
    //
    // _jsonObject ya es un literal de objeto JS válido ({...}); lo pasamos tal cual al global del
    // SPA (mismo patrón que applyThemeFromNative). El SPA puebla el panel "Aula 122" de ajustes.
    //
    m_web->page()->runJavaScript(QStringLiteral("window.aula122ApplyNativeSettings && "
                                                "window.aula122ApplyNativeSettings(%1)")
                                     .arg(_jsonObject));
}

void OdysseusWorkspaceView::setActiveProject(const QString& _path)
{
    //
    // Recordamos el proyecto para RE-APLICARLO en loadFinished: al abrir un proyecto el shell
    // llama aquí ANTES de que el SPA cargue → este runJavaScript se perdería y el SPA seguiría
    // pidiendo /api/guion/estructura SIN proyecto = el default del backend (otro .starc), cuyos
    // uuids no encajan con el modelo nativo → los documentos del árbol no abrían.
    //
    m_activeProjectPath = _path;
    m_activeProjectSet = true;
    if (m_web == nullptr || m_web->page() == nullptr) {
        return;
    }
    //
    // Escapamos la ruta para incrustarla como string JS (puede tener espacios, p. ej.
    // "Aula 122"; protegemos también '\' y '"').
    //
    QString escaped = _path;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    m_web->page()->runJavaScript(
        QStringLiteral("window.aula122SetProject && window.aula122SetProject(\"%1\")").arg(escaped));
}

void OdysseusWorkspaceView::refreshProjectTree()
{
    if (m_web == nullptr || m_web->page() == nullptr) {
        return;
    }
    m_web->page()->runJavaScript(
        QStringLiteral("window.aula122RefreshTree && window.aula122RefreshTree()"));
}

void OdysseusWorkspaceView::setMenuCollapsed(bool _collapsed)
{
    if (m_web == nullptr || m_web->page() == nullptr) {
        return;
    }
    //
    // Aula 122 / menú nativo: pedimos al SPA que OCULTE su barra/menú web y deje SOLO el chat (el
    // menú ahora es el navegador NATIVO de STARC). Canal nativo→web (window.aula122SetMenuCollapsed).
    //
    m_web->page()->runJavaScript(
        QStringLiteral("window.aula122SetMenuCollapsed && window.aula122SetMenuCollapsed(%1)")
            .arg(_collapsed ? 1 : 0));
}

void OdysseusWorkspaceView::setChatCollapsed(bool _collapsed)
{
    //
    // Recordamos el estado para RE-APLICARLO en loadFinished: showProject puede pedir el
    // colapso ANTES de que el SPA cargue (~varios s) → esta llamada nativo→web se perdería.
    //
    m_chatCollapsed = _collapsed;
    m_chatCollapsedSet = true;
    if (m_web == nullptr || m_web->page() == nullptr) {
        return;
    }
    m_web->page()->runJavaScript(
        QStringLiteral("window.aula122SetChatCollapsed && window.aula122SetChatCollapsed(%1)")
            .arg(_collapsed ? 1 : 0));
}

} // namespace Ui
