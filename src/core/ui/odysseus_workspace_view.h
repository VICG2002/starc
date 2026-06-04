#pragma once

#include <QWebEnginePage>
#include <QWidget>

class QWebEngineView;


namespace Ui {

/**
 * @brief QWebEnginePage que intercepta el puente "Aula 122". Cuando el SPA de
 *        Odiseo navega a http://aula122.bridge/open/<etapa> (clic en una etapa
 *        del pipeline en su sidebar), emite pipelineRequested(<etapa>) y CANCELA
 *        la navegación. Es un canal JS->C++ de una vía, sin depender de
 *        qwebchannel.js (que no viene como recurso accesible en este Qt).
 */
class OdysseusPage : public QWebEnginePage
{
    Q_OBJECT

public:
    using QWebEnginePage::QWebEnginePage;

signals:
    void pipelineRequested(const QString& _view);

protected:
    bool acceptNavigationRequest(const QUrl& _url, NavigationType _type,
                                 bool _isMainFrame) override;
};


/**
 * @brief Vista que embebe el workspace COMPLETO de Odiseo (odysseus) dentro de
 *        Aula 122 mediante QtWebEngine, apuntando al cerebro local
 *        (127.0.0.1:7860) con auto-login por cookie cacheada.
 *
 * Es la base de la "UI de Odiseo como shell de Aula 122": expone el potencial
 * completo del asistente (modo agente, presets, RAG, documentos, deep research,
 * ajustes, temas) y, desde su sidebar (grupo "Aula 122"), permite saltar a los
 * módulos NATIVOS del pipeline de producción vía el puente de navegación.
 */
class OdysseusWorkspaceView : public QWidget
{
    Q_OBJECT

public:
    explicit OdysseusWorkspaceView(QWidget* _parent = nullptr);
    ~OdysseusWorkspaceView() override;

    /**
     * @brief (Re)carga el workspace, inyectando la cookie de sesión admin si
     *        existe para abrir autenticado (sin pantalla de login). Idempotente.
     */
    void loadWorkspace();

    /**
     * @brief Re-lee el token y recarga. Para usar cuando el cerebro queda listo
     *        tras el arranque (la 1ª carga pudo fallar con el server 7860 abajo).
     */
    void reload();

signals:
    /**
     * @brief El SPA pidió abrir una etapa del pipeline nativo
     *        ("guion" / "desglose" / "plan-rodaje" / ...).
     */
    void navigateRequested(const QString& _view);

private:
    QWebEngineView* m_web = nullptr;
    bool m_loaded = false;
};

} // namespace Ui
