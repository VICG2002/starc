#pragma once

#include <QObject>
#include <QScopedPointer>
#include <QString>


namespace ManagementLayer {

/**
 * @brief Gestiona el ciclo de vida del "cerebro" local de Aula 122.
 *
 * Aula 122 es autocontenida: NO depende de servidores externos (Ollama, un
 * odysseus instalado aparte, ni LaunchAgents del sistema). Este manager lanza
 * —con el Python y los binarios EMPAQUETADOS dentro del .app— los procesos del
 * cerebro cuando arranca la app, y los mata cuando se cierra:
 *
 *   1. chromadb      memoria vectorial            (127.0.0.1, loopback)
 *   2. llama-server  runtime de modelo OpenAI-compat (127.0.0.1)  ← reemplaza Ollama
 *   3. odysseus      uvicorn app:app, el agente   (127.0.0.1:7860)
 *
 * Todo en loopback (nunca expuesto a la red). Al arrancar, fija el endpoint del
 * modelo de odysseus al llama-server local (cierra el paso "B3" del empaquetado,
 * ver ai/PACKAGING.md).
 *
 * Layout esperado del bundle (canónico, lo ensambla la Fase D dentro del .app):
 *   <brain>/python/bin/python3      CPython relocatable + deps  (Fase A)
 *   <brain>/llama/bin/llama-server  runtime de modelo           (Fase B)
 *   <brain>/odysseus/app.py         código de odysseus vendorizado
 * donde  <brain> = $AULA122_BRAIN_DIR            (override de desarrollo)
 *                || <.app>/Contents/Resources/brain (release).
 * Los datos mutables (modelos GGUF, datos de chromadb) viven en
 * AppDataLocation/brain (…/Application Support/Diez50/Aula 122/brain), nunca
 * dentro del .app (read-only tras firmar).
 *
 * Si no se resuelve un <brain> válido, el manager queda DESHABILITADO (no-op) y
 * la app sigue funcionando sin cerebro local (degradación elegante — útil
 * mientras la Fase D todavía no ensambla el bundle).
 */
class BrainProcessManager : public QObject
{
    Q_OBJECT

public:
    explicit BrainProcessManager(QObject* _parent = nullptr);
    ~BrainProcessManager() override;

    /**
     * @brief ¿Se resolvió un <brain> válido (python + llama-server + odysseus)?
     */
    bool isEnabled() const;

    /**
     * @brief Lanza los procesos del cerebro. No bloquea: el resultado llega por
     *        ready() (odysseus aceptando conexiones) o failed().
     */
    void startAll();

    /**
     * @brief Detiene todos los procesos (TERM y, si no cooperan, KILL).
     *        Idempotente y seguro de llamar aunque nunca se haya arrancado.
     */
    void stopAll();

signals:
    /** odysseus acepta conexiones en su puerto: el Asistente IA ya puede usarse. */
    void ready();

    /** No se pudo arrancar el cerebro (rutas faltantes, modelo ausente, timeout). */
    void failed(const QString& _reason);

    /** Línea de diagnóstico (stdout/stderr de los procesos o del propio manager). */
    void log(const QString& _line);

private:
    struct Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace ManagementLayer
