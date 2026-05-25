#pragma once

#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;


namespace ManagementLayer {

/**
 * @brief Cliente HTTP para la API de Anthropic (Claude).
 *
 * Maneja autenticación (API key desde env var o archivo) y envío de
 * mensajes. Las respuestas se emiten vía signals async.
 *
 * Configuración de la API key (en orden de prioridad):
 *   1. Variable de entorno ANTHROPIC_API_KEY
 *   2. Archivo ~/.config/Aula_122/api_key.txt (primera línea)
 */
class ClaudeClient : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeClient(QObject* _parent = nullptr);
    ~ClaudeClient() override;

    /**
     * @brief ¿Hay API key configurada?
     */
    bool hasApiKey() const;

    /**
     * @brief Ruta esperada del archivo de API key (para mensajes al usuario)
     */
    static QString apiKeyFilePath();

    /**
     * @brief Enviar un mensaje a Claude. Resultado async vía responseReceived / errorOccurred.
     */
    void sendMessage(const QString& _prompt);

signals:
    /**
     * @brief Respuesta recibida exitosamente de Claude
     */
    void responseReceived(const QString& _response);

    /**
     * @brief Error en la llamada (HTTP, JSON, API, etc.)
     */
    void errorOccurred(const QString& _error);

private slots:
    void onReplyFinished();

private:
    QString loadApiKey() const;

    QNetworkAccessManager* m_network;
    QString m_apiKey;
};

} // namespace ManagementLayer
