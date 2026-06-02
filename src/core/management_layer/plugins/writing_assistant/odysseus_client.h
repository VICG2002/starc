#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;


namespace ManagementLayer {

/**
 * @brief Cliente para el "cerebro" de Aula 122: odysseus (self-hosted, local).
 *
 * Reemplaza a ClaudeClient (CLI) como backend del Asistente IA. Habla con la
 * API HTTP local de odysseus (http://127.0.0.1:7860) en **modo agente**, de modo
 * que el cerebro puede invocar las tools MCP de Aula 122 (servidor `aula122-mcp`)
 * para leer el proyecto real.
 *
 * AUTENTICACIÓN (AI-3): el modo agente de odysseus sólo entrega el toolset
 * completo (incl. MCP) a la **sesión admin**. El cliente usa una sesión admin
 * cacheada (cookie `odysseus_session`) guardada fuera del repo en
 *   ~/Library/Application Support/Diez50/Aula 122/odysseus_session   (chmod 600)
 * La cookie se instala en el **cookie jar** de QNetworkAccessManager (no por
 * header manual, que el jar puede ignorar). El token se relee en cada mensaje
 * (puede re-cachearse) y, si la sesión se pierde, se recrea y reintenta una vez.
 *
 * Endpoint + modelo del LLM se leen de
 *   ~/Library/Application Support/Diez50/Aula 122/odysseus_model.json
 * (por defecto: Ollama OpenAI-compat `/v1/chat/completions` + qwen2.5:14b).
 *
 * Misma interfaz pública que ClaudeClient para ser drop-in:
 *   - sendMessage() -> responseReceived()/errorOccurred() (async)
 *   - multi-turn: mantiene un session_id de odysseus
 *   - setScreenplayContext(): contexto del guion (se antepone al 1er mensaje)
 */
class OdysseusClient : public QObject
{
    Q_OBJECT

public:
    explicit OdysseusClient(QObject* _parent = nullptr);
    ~OdysseusClient() override;

    /**
     * @brief ¿Está configurado? (existe la sesión admin cacheada).
     */
    bool isAvailable() const;

    /**
     * @brief URL base de odysseus (compat con la interfaz previa).
     */
    QString cliPath() const;

    /**
     * @brief Enviar un mensaje. Resultado async vía responseReceived / errorOccurred.
     */
    void sendMessage(const QString& _prompt);

    /**
     * @brief Olvidar la sesión actual (próximo sendMessage abre una nueva).
     */
    void resetConversation();

    /**
     * @brief ¿Hay una sesión activa de odysseus?
     */
    bool hasActiveSession() const;

    /**
     * @brief Inyectar contexto del guion (se antepone al primer mensaje de la sesión).
     */
    void setScreenplayContext(const QString& _context);

    /**
     * @brief Contexto actualmente cargado (vacío si no hay).
     */
    QString screenplayContext() const;

signals:
    void responseReceived(const QString& _response);
    void errorOccurred(const QString& _error);

private:
    QString sessionFilePath() const;
    QString modelConfigPath() const;
    QString loadSessionToken() const;
    void installSessionCookie(); // mete odysseus_session en el cookie jar del m_net

    // Cadena async: registrar el endpoint del modelo -> asegurar sesión -> chatear (agente, SSE).
    void ensureModelEndpointThenSession(const QString& _prompt);
    void createSessionThenChat(const QString& _prompt);
    void postChatStream(const QString& _prompt);

    void fail(const QString& _error);

    QNetworkAccessManager* m_net = nullptr;
    QString m_baseUrl = QStringLiteral("http://127.0.0.1:7860");
    QString m_sessionToken;       // cookie odysseus_session (admin), cacheada
    QString m_sessionId;          // sesión de chat de odysseus (vacía hasta crearla)
    QString m_screenplayContext;  // contexto del guion
    bool m_contextSent = false;   // si ya se antepuso el contexto en esta sesión
    bool m_busy = false;          // una petición a la vez
    bool m_retried = false;       // reintento único tras perder la sesión

    // LLM (leídos de odysseus_model.json; defaults Ollama OpenAI-compat + 14b).
    QString m_endpointUrl = QStringLiteral("http://localhost:11434/v1/chat/completions");
    QString m_model = QStringLiteral("qwen2.5:14b");
};

} // namespace ManagementLayer
