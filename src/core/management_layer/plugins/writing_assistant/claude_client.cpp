#include "claude_client.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>


namespace ManagementLayer {

namespace {
const QString kApiEndpoint = QStringLiteral("https://api.anthropic.com/v1/messages");
const QString kAnthropicVersion = QStringLiteral("2023-06-01");
const QString kModel = QStringLiteral("claude-opus-4-7");
const int kMaxTokens = 1024;
const QString kEnvVarName = QStringLiteral("ANTHROPIC_API_KEY");
}


ClaudeClient::ClaudeClient(QObject* _parent)
    : QObject(_parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_apiKey = loadApiKey();
}

ClaudeClient::~ClaudeClient() = default;

bool ClaudeClient::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

QString ClaudeClient::apiKeyFilePath()
{
    return QDir::homePath() + QStringLiteral("/.config/Aula_122/api_key.txt");
}

QString ClaudeClient::loadApiKey() const
{
    //
    // Prioridad 1: variable de entorno ANTHROPIC_API_KEY
    //
    const QByteArray envKey = qgetenv(kEnvVarName.toUtf8().constData());
    if (!envKey.isEmpty()) {
        return QString::fromUtf8(envKey).trimmed();
    }

    //
    // Prioridad 2: archivo ~/.config/Aula_122/api_key.txt (primera línea)
    //
    QFile keyFile(apiKeyFilePath());
    if (keyFile.exists() && keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&keyFile);
        const QString fileKey = stream.readLine().trimmed();
        keyFile.close();
        if (!fileKey.isEmpty()) {
            return fileKey;
        }
    }

    return QString();
}

void ClaudeClient::sendMessage(const QString& _prompt)
{
    //
    // Verificar API key antes de enviar
    //
    if (m_apiKey.isEmpty()) {
        emit errorOccurred(
            tr("API key de Anthropic no configurada. Define la variable de entorno %1 "
               "o crea el archivo %2 con tu key.")
                .arg(kEnvVarName, apiKeyFilePath()));
        return;
    }

    //
    // Construir el JSON request
    //
    QJsonObject messageObj;
    messageObj["role"] = "user";
    messageObj["content"] = _prompt;

    QJsonArray messagesArr;
    messagesArr.append(messageObj);

    QJsonObject bodyObj;
    bodyObj["model"] = kModel;
    bodyObj["max_tokens"] = kMaxTokens;
    bodyObj["messages"] = messagesArr;

    const QByteArray body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    //
    // Configurar request con headers
    // (brace init para evitar most vexing parse)
    //
    const QUrl endpoint(kApiEndpoint);
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", kAnthropicVersion.toUtf8());

    //
    // POST async
    //
    QNetworkReply* reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, &ClaudeClient::onReplyFinished);
}

void ClaudeClient::onReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr) {
        emit errorOccurred(tr("Reply nulo (interno)"));
        return;
    }
    reply->deleteLater();

    //
    // Error de red
    //
    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray errorBody = reply->readAll();
        emit errorOccurred(tr("Error de red: %1\n%2")
                               .arg(reply->errorString(), QString::fromUtf8(errorBody)));
        return;
    }

    //
    // Parsear JSON
    //
    const QByteArray data = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(tr("JSON inválido: %1").arg(parseError.errorString()));
        return;
    }
    if (!doc.isObject()) {
        emit errorOccurred(tr("Respuesta no es objeto JSON"));
        return;
    }

    const QJsonObject root = doc.object();

    //
    // Error de API (la API devuelve {"type":"error","error":{...}})
    //
    if (root.contains("error")) {
        const QJsonObject errObj = root["error"].toObject();
        emit errorOccurred(tr("Error API: %1 — %2")
                               .arg(errObj["type"].toString(), errObj["message"].toString()));
        return;
    }

    //
    // Extraer texto del primer content block
    // Formato: {"content":[{"type":"text","text":"..."}], ...}
    //
    const QJsonArray contentArr = root["content"].toArray();
    if (contentArr.isEmpty()) {
        emit errorOccurred(tr("Respuesta sin contenido"));
        return;
    }

    QString fullText;
    for (const auto& block : contentArr) {
        const QJsonObject blockObj = block.toObject();
        if (blockObj["type"].toString() == "text") {
            if (!fullText.isEmpty()) {
                fullText += "\n";
            }
            fullText += blockObj["text"].toString();
        }
    }

    if (fullText.isEmpty()) {
        emit errorOccurred(tr("Respuesta sin texto extraíble"));
        return;
    }

    emit responseReceived(fullText);
}

} // namespace ManagementLayer
