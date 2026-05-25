#include "claude_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>


namespace ManagementLayer {

namespace {
const int kTimeoutMs = 120000; // 2 minutos por respuesta

QStringList candidatePaths()
{
    //
    // Ubicaciones típicas del CLI de Claude Code en macOS
    //
    return {
        QDir::homePath() + QStringLiteral("/.local/bin/claude"),
        QStringLiteral("/opt/homebrew/bin/claude"),
        QStringLiteral("/usr/local/bin/claude"),
    };
}
}


ClaudeClient::ClaudeClient(QObject* _parent)
    : QObject(_parent)
{
    m_cliPath = findClaudeCli();
}

ClaudeClient::~ClaudeClient() = default;

bool ClaudeClient::isAvailable() const
{
    return !m_cliPath.isEmpty();
}

QString ClaudeClient::cliPath() const
{
    return m_cliPath;
}

QString ClaudeClient::findClaudeCli() const
{
    //
    // Prioridad 1: rutas conocidas (más rápido que lanzar `which`)
    //
    for (const QString& path : candidatePaths()) {
        if (QFileInfo(path).isExecutable()) {
            return path;
        }
    }

    //
    // Prioridad 2: PATH del entorno via `which claude`
    //
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

void ClaudeClient::sendMessage(const QString& _prompt)
{
    //
    // CLI no disponible
    //
    if (m_cliPath.isEmpty()) {
        emit errorOccurred(
            tr("No se encontró el CLI de Claude Code. "
               "Instálalo desde https://docs.claude.com/claude-code "
               "o verifica que esté en ~/.local/bin/claude."));
        return;
    }

    //
    // Petición concurrente: una a la vez (la nueva se descarta)
    //
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        emit errorOccurred(tr("Ya hay una petición en curso. Espera la respuesta anterior."));
        return;
    }

    //
    // Limpiar proceso anterior si quedó referenciado
    //
    if (m_process != nullptr) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);
    m_process->setProgram(m_cliPath);

    //
    // Argumentos: --print + --output-format json da JSON parseable con
    // {"result": "..."} cuando termina exitoso. Sin --bare porque ese flag
    // bloquea la lectura del keychain donde vive la auth OAuth de Claude Code.
    //
    // Si ya tenemos session_id de un mensaje previo, --resume <id> hace que
    // Claude vea toda la conversación anterior (multi-turn). Si no, primera
    // invocación crea una sesión nueva.
    //
    QStringList args;
    if (!m_sessionId.isEmpty()) {
        args << QStringLiteral("--resume") << m_sessionId;
    }
    args << QStringLiteral("--print")
         << QStringLiteral("--output-format") << QStringLiteral("json")
         << _prompt;
    m_process->setArguments(args);

    //
    // Aislar del entorno: trabajar en HOME, sin proyecto específico (todavía).
    // En iteraciones futuras esto será el directorio del .starc abierto.
    //
    m_process->setWorkingDirectory(QDir::homePath());

    //
    // Mantener PATH del padre para que el CLI encuentre node, etc.
    //
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    m_process->setProcessEnvironment(env);

    //
    // Redirigir stdin a /dev/null. Sin esto, el CLI detecta el pipe vacío
    // y espera 3s emitiendo "no stdin data received" como warning antes
    // de continuar. Equivalente a `claude ... < /dev/null` en shell.
    //
    m_process->setStandardInputFile(QProcess::nullDevice());

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) { handleProcessFinished(); });

    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError _error) {
                const QString msg = m_process != nullptr ? m_process->errorString()
                                                          : tr("Error desconocido");
                emit errorOccurred(tr("Error de proceso (%1): %2")
                                       .arg(static_cast<int>(_error))
                                       .arg(msg));
            });

    m_process->start();

    //
    // Timeout duro: si tarda más de kTimeoutMs, matamos el proceso.
    // (handleProcessFinished disparará el error.)
    //
    QTimer::singleShot(kTimeoutMs, m_process, [this]() {
        if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
    });
}

void ClaudeClient::handleProcessFinished()
{
    if (m_process == nullptr) {
        emit errorOccurred(tr("Proceso nulo (interno)"));
        return;
    }

    const int exitCode = m_process->exitCode();
    const QByteArray stdoutBytes = m_process->readAllStandardOutput();
    const QByteArray stderrBytes = m_process->readAllStandardError();

    //
    // Intentar primero parsear stdout como JSON — el CLI emite JSON estructurado
    // incluso cuando termina con error (is_error=true), y ese JSON tiene el
    // mensaje legible en .result. Preferible al exit code / stderr crudos.
    //
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(stdoutBytes, &parseError);
    const bool jsonOk = parseError.error == QJsonParseError::NoError && doc.isObject();

    if (jsonOk) {
        const QJsonObject root = doc.object();
        const QString resultText = root.value("result").toString();
        const bool isError = root.value("is_error").toBool()
            || root.value("subtype").toString() != QStringLiteral("success");

        if (isError) {
            emit errorOccurred(resultText.isEmpty()
                                   ? tr("Claude reportó error sin detalle")
                                   : resultText);
            return;
        }

        if (resultText.isEmpty()) {
            emit errorOccurred(tr("Respuesta vacía"));
            return;
        }

        //
        // Capturar / refrescar session_id para que el próximo mensaje
        // se envíe con --resume <id> y Claude recuerde la conversación.
        //
        const QString returnedSession = root.value("session_id").toString();
        if (!returnedSession.isEmpty()) {
            m_sessionId = returnedSession;
        }

        emit responseReceived(resultText);
        return;
    }

    //
    // No hubo JSON parseable — fallback a stderr / exit code
    //
    QString errMsg = QString::fromUtf8(stderrBytes).trimmed();
    if (errMsg.isEmpty()) {
        errMsg = QString::fromUtf8(stdoutBytes).trimmed();
    }
    if (errMsg.isEmpty()) {
        errMsg = tr("Claude CLI terminó con código %1 sin mensaje").arg(exitCode);
    }
    emit errorOccurred(tr("Error del CLI: %1").arg(errMsg));
}

void ClaudeClient::resetConversation()
{
    m_sessionId.clear();
}

bool ClaudeClient::hasActiveSession() const
{
    return !m_sessionId.isEmpty();
}

} // namespace ManagementLayer
