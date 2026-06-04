#include "odysseus_workspace_view.h"

#include <QDir>
#include <QFile>
#include <QNetworkCookie>
#include <QTimer>
#include <QUrl>
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
    // Puente "Aula 122" (JS->C++): el SPA navega a http://aula122.bridge/open/<etapa>
    // cuando el usuario hace clic en una etapa del pipeline. Lo interceptamos,
    // reemitimos la etapa y CANCELAMOS la navegación (return false) — esa URL nunca
    // se carga; solo sirve de canal de mensajes hacia el shell nativo. El resto de
    // navegaciones (127.0.0.1, enlaces internos del SPA) se permiten normalmente.
    //
    if (_url.host() == QLatin1String("aula122.bridge")) {
        const QString view = _url.path().section(QLatin1Char('/'), -1);
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
    m_web->setPage(page);

    loadWorkspace();
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

} // namespace Ui
