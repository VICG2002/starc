#include "odysseus_client.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>


namespace ManagementLayer {

namespace {
// Modo agente + modelo local 14b + ida y vuelta de tools puede tardar.
// Es un timeout de "sin datos transferidos"; odysseus emite eventos SSE
// periódicos (model_info, tool_start/-output, deltas), así que no se dispara
// mientras el stream avanza.
constexpr int kTimeoutMs = 180000;
} // namespace

OdysseusClient::OdysseusClient(QObject* _parent)
    : QObject(_parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_sessionToken = loadSessionToken();
    installSessionCookie();

    // Endpoint + modelo del LLM (si hay config; si no, defaults del header).
    QFile f(modelConfigPath());
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
        const QString endpoint = cfg.value(QStringLiteral("endpoint_url")).toString();
        const QString model = cfg.value(QStringLiteral("model")).toString();
        if (!endpoint.isEmpty()) {
            m_endpointUrl = endpoint;
        }
        if (!model.isEmpty()) {
            m_model = model;
        }
    }
}

OdysseusClient::~OdysseusClient() = default;

QString OdysseusClient::sessionFilePath() const
{
    return QDir::homePath()
        + QStringLiteral("/Library/Application Support/Diez50/Aula 122/odysseus_session");
}

QString OdysseusClient::modelConfigPath() const
{
    return QDir::homePath()
        + QStringLiteral("/Library/Application Support/Diez50/Aula 122/odysseus_model.json");
}

QString OdysseusClient::loadSessionToken() const
{
    QFile f(sessionFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

// La cookie de sesión se inyecta en el cookie jar del manager: QNetworkAccessManager
// gestiona el header Cookie a partir del jar (un setRawHeader("Cookie", ...) puede
// ser ignorado). insertCookie reemplaza una cookie homónima previa (refresh).
void OdysseusClient::installSessionCookie()
{
    if (!m_net || m_sessionToken.isEmpty()) {
        return;
    }
    QNetworkCookie cookie("odysseus_session", m_sessionToken.toUtf8());
    cookie.setDomain(QStringLiteral("127.0.0.1"));
    cookie.setPath(QStringLiteral("/"));
    m_net->cookieJar()->insertCookie(cookie);
}

bool OdysseusClient::isAvailable() const
{
    return !m_sessionToken.isEmpty() || !loadSessionToken().isEmpty();
}

QString OdysseusClient::cliPath() const
{
    return m_baseUrl;
}

bool OdysseusClient::hasActiveSession() const
{
    return !m_sessionId.isEmpty();
}

void OdysseusClient::setScreenplayContext(const QString& _context)
{
    m_screenplayContext = _context;
    m_contextSent = false; // se reenvía en el próximo mensaje
}

QString OdysseusClient::screenplayContext() const
{
    return m_screenplayContext;
}

void OdysseusClient::resetConversation()
{
    m_sessionId.clear();
    m_contextSent = false;
}

void OdysseusClient::fail(const QString& _error)
{
    m_busy = false;
    emit errorOccurred(_error);
}

void OdysseusClient::sendMessage(const QString& _prompt)
{
    if (m_busy) {
        emit errorOccurred(tr("Espera la respuesta anterior."));
        return;
    }

    // Releer el token más reciente del archivo (pudo re-cachearse tras un login).
    // Si cambió, la sesión de chat anterior ya no aplica.
    const QString fresh = loadSessionToken();
    if (!fresh.isEmpty() && fresh != m_sessionToken) {
        m_sessionToken = fresh;
        m_sessionId.clear();
        m_contextSent = false;
    } else if (m_sessionToken.isEmpty()) {
        m_sessionToken = fresh;
    }
    if (m_sessionToken.isEmpty()) {
        emit errorOccurred(
            tr("odysseus no está autenticado: falta la sesión admin en\n"
               "~/Library/Application Support/Diez50/Aula 122/odysseus_session\n"
               "Inicia sesión como admin en odysseus para habilitar el agente."));
        return;
    }
    installSessionCookie();

    m_retried = false;
    m_busy = true;
    if (m_sessionId.isEmpty()) {
        ensureModelEndpointThenSession(_prompt);
    } else {
        postChatStream(_prompt);
    }
}

void OdysseusClient::ensureModelEndpointThenSession(const QString& _prompt)
{
    // odysseus marca una sesión como "huérfana" (y rechaza el chat) si su
    // endpoint_url no coincide con ningún ModelEndpoint registrado y habilitado
    // (ver chat_routes._clear_orphaned_session_endpoint). Por eso, antes de crear
    // la sesión registramos el llama-server local como model endpoint. Es
    // idempotente: odysseus normaliza la URL (quita /chat/completions) y deduplica
    // por base_url, así que llamarlo en cada arranque no crea duplicados.
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/model-endpoints")));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(kTimeoutMs);

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("base_url"), m_endpointUrl);
    form.addQueryItem(QStringLiteral("name"), QStringLiteral("Aula 122 (cerebro local)"));
    form.addQueryItem(QStringLiteral("skip_probe"), QStringLiteral("true"));
    form.addQueryItem(QStringLiteral("supports_tools"), QStringLiteral("true"));
    form.addQueryItem(QStringLiteral("shared"), QStringLiteral("true"));
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply* reply = m_net->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, _prompt]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status == 401) {
            m_sessionToken.clear();
            fail(tr("La sesión de odysseus expiró. Vuelve a iniciar sesión como admin."));
            return;
        }
        // Aunque el registro falle por otra causa, intentamos crear la sesión:
        // puede que el endpoint ya estuviera registrado.
        createSessionThenChat(_prompt);
    });
}

void OdysseusClient::createSessionThenChat(const QString& _prompt)
{
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/session")));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(kTimeoutMs);

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("name"), QStringLiteral("Aula 122"));
    form.addQueryItem(QStringLiteral("endpoint_url"), m_endpointUrl);
    form.addQueryItem(QStringLiteral("model"), m_model);
    // El endpoint OpenAI-compat de Ollama no responde al probe de validación
    // como odysseus espera; saltarlo (la conexión se ejerce en el primer chat).
    form.addQueryItem(QStringLiteral("skip_validation"), QStringLiteral("true"));
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply* reply = m_net->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, _prompt]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (status == 401) {
                m_sessionToken.clear();
                fail(tr("La sesión de odysseus expiró. Vuelve a iniciar sesión como admin."));
                return;
            }
            QString detail = QString::fromUtf8(data);
            const QJsonObject o = QJsonDocument::fromJson(data).object();
            if (o.contains(QStringLiteral("detail"))) {
                detail = o.value(QStringLiteral("detail")).toString();
            }
            fail(tr("No se pudo crear la sesión en odysseus: %1")
                     .arg(detail.isEmpty() ? reply->errorString() : detail));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        m_sessionId = o.value(QStringLiteral("id")).toString();
        m_contextSent = false;
        if (m_sessionId.isEmpty()) {
            fail(tr("odysseus no devolvió un id de sesión."));
            return;
        }
        postChatStream(_prompt);
    });
}

void OdysseusClient::postChatStream(const QString& _prompt)
{
    // Anteponer el contexto del guion una vez por sesión.
    QString message = _prompt;
    if (!m_contextSent && !m_screenplayContext.isEmpty()) {
        message = QStringLiteral("[Contexto del guion]\n%1\n\n[Mensaje del usuario]\n%2")
                      .arg(m_screenplayContext, _prompt);
    }

    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/chat_stream")));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(kTimeoutMs);

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("message"), message);
    form.addQueryItem(QStringLiteral("session"), m_sessionId);
    form.addQueryItem(QStringLiteral("mode"), QStringLiteral("agent"));
    // Sin shell: el agente sólo necesita las tools MCP de Aula 122.
    form.addQueryItem(QStringLiteral("allow_bash"), QStringLiteral("false"));
    // Perfil "solo-MCP": odysseus desactiva las tools nativas y deja sólo las del
    // servidor aula122-mcp, para que el modelo local elija de un menú chico y fiable.
    form.addQueryItem(QStringLiteral("mcp_only"), QStringLiteral("true"));
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply* reply = m_net->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, _prompt]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            const QString detail = QString::fromUtf8(data);
            if (status == 401) {
                m_sessionToken.clear();
                fail(tr("La sesión de odysseus expiró. Vuelve a iniciar sesión como admin."));
                return;
            }
            // Sesión perdida en odysseus: recrearla y reintentar una vez.
            const bool sessionLost = (status == 404)
                || detail.contains(QStringLiteral("not found"), Qt::CaseInsensitive);
            if (sessionLost) {
                m_sessionId.clear();
                if (!m_retried) {
                    m_retried = true;
                    createSessionThenChat(_prompt);
                    return;
                }
            }
            fail(tr("odysseus: %1").arg(detail.isEmpty() ? reply->errorString() : detail));
            return;
        }

        // Respuesta en streaming SSE: líneas "data: {json}" (+ "event: error").
        // Acumular los fragmentos {"delta": "..."}; capturar errores en banda.
        QString answer;
        QString sseError;
        bool nextIsError = false;
        const QList<QByteArray> lines = data.split('\n');
        for (const QByteArray& raw : lines) {
            const QByteArray line = raw.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            if (line.startsWith("event:")) {
                nextIsError = line.contains("error");
                continue;
            }
            if (!line.startsWith("data:")) {
                continue;
            }
            const QByteArray payload = line.mid(5).trimmed();
            if (payload.isEmpty() || payload == "[DONE]") {
                continue;
            }
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
                continue;
            }
            const QJsonObject o = doc.object();
            const QString type = o.value(QStringLiteral("type")).toString();
            const int sstatus = o.value(QStringLiteral("status")).toInt();
            // Al ejecutarse una tool, descartar la narración/plan previo del modelo:
            // la respuesta final es lo que viene DESPUÉS de la última tool.
            if (type == QStringLiteral("tool_start") || type == QStringLiteral("tool_output")) {
                answer.clear();
                continue;
            }
            if (nextIsError || type == QStringLiteral("error") || sstatus == 400) {
                sseError = o.contains(QStringLiteral("text"))
                    ? o.value(QStringLiteral("text")).toString()
                    : QString::fromUtf8(payload);
                nextIsError = false;
                continue;
            }
            const QJsonValue delta = o.value(QStringLiteral("delta"));
            if (delta.isString()) {
                answer += delta.toString();
            }
        }

        m_contextSent = true;
        m_busy = false;

        const QString trimmed = answer.trimmed();
        if (!trimmed.isEmpty()) {
            emit responseReceived(trimmed);
            return;
        }
        if (!sseError.isEmpty()) {
            emit errorOccurred(tr("odysseus (agente): %1").arg(sseError));
            return;
        }
        emit errorOccurred(tr("odysseus no devolvió respuesta (el modelo no produjo texto)."));
    });
}

} // namespace ManagementLayer
