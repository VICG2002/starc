#pragma once

#include <QObject>
#include <QString>

class QProcess;


namespace ManagementLayer {

/**
 * @brief Cliente para Claude via Claude Code CLI (no API de pago).
 *
 * Invoca el binario `claude` como subproceso usando QProcess en modo
 * non-interactive (`-p --output-format json`). Reutiliza la sesión de
 * Claude Code del usuario — sin costo adicional, sin API key separada.
 *
 * Requisitos:
 *   - `claude` accesible (busca en ~/.local/bin y PATH).
 *   - Usuario logueado a Claude Code.
 */
class ClaudeClient : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeClient(QObject* _parent = nullptr);
    ~ClaudeClient() override;

    /**
     * @brief ¿Está disponible el CLI de Claude?
     */
    bool isAvailable() const;

    /**
     * @brief Ruta resuelta al binario `claude` (vacío si no se encontró).
     */
    QString cliPath() const;

    /**
     * @brief Enviar un mensaje. Resultado async vía responseReceived / errorOccurred.
     *        Si ya hay una petición en curso, se ignora la nueva con error.
     *
     * Multi-turn: el primer mensaje crea una sesión nueva y captura su session_id.
     * Los siguientes mensajes se envían con --resume <session_id>, así Claude
     * recuerda lo conversado. Para empezar de cero, llamar resetConversation().
     */
    void sendMessage(const QString& _prompt);

    /**
     * @brief Olvidar la sesión actual. El próximo sendMessage abre una nueva conversación.
     */
    void resetConversation();

    /**
     * @brief ¿Hay una sesión activa con historial?
     */
    bool hasActiveSession() const;

signals:
    void responseReceived(const QString& _response);
    void errorOccurred(const QString& _error);

private:
    QString findClaudeCli() const;
    void handleProcessFinished();

    QString m_cliPath;
    QProcess* m_process = nullptr;
    QString m_sessionId; // vacío hasta primera respuesta exitosa
};

} // namespace ManagementLayer
