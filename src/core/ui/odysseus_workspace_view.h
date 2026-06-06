#pragma once

#include <QWebEnginePage>
#include <QWidget>

class QTimer;
class QPushButton;
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
    void themeRequested(const QString& _bg, const QString& _fg, const QString& _panel,
                        const QString& _accent, const QString& _error, const QString& _mode);
    /**
     * @brief Aula 122: el SPA cambió su FUENTE de UI; la sincronizamos al nativo (web→nativo).
     *        Verbo de puente: param "font" del /theme (familia CSS).
     */
    void fontRequested(const QString& _family);
    /**
     * @brief El SPA pidió abrir un DOCUMENTO concreto del proyecto por su uuid
     *        (clic en un personaje/locación/subdocumento del árbol de la barra).
     *        Verbo de puente /open/doc/<uuid>.
     */
    void documentRequested(const QString& _uuid);
    /**
     * @brief El SPA pidió añadir un documento al proyecto (verbo /open/add-document):
     *        el shell abre el diálogo nativo "Añadir documento".
     */
    void addDocumentRequested();
    /**
     * @brief El usuario ocultó/mostró el CHAT de Odiseo (verbo /odiseo/chat). true = oculto;
     *        el menú (barra) se queda y el editor nativo gana el espacio.
     */
    void chatCollapseRequested(bool _collapsed);
    /**
     * @brief El SPA abrió/cerró una HERRAMIENTA de Odiseo (verbo /odiseo/expand). on=true → el panel
     *        de Odiseo ocupa TODO el ancho (editor nativo oculto detrás); on=false → no queda ninguna
     *        herramienta → se restaura el reparto previo.
     */
    void expandRequested(bool _on);
    /**
     * @brief Guardar el proyecto (verbo /project/save).
     */
    void saveProjectRequested();
    /**
     * @brief Exportar el documento actual (verbo /project/export).
     */
    void exportProjectRequested();
    /**
     * @brief Aula 122 (B): el usuario picó la tuerca/X del menú → mostrar/ocultar el MENÚ en una
     *        ventana FLOTANTE encima del editor (verbo /odiseo/floatmenu).
     */
    void floatMenuToggleRequested();
    /**
     * @brief Aula 122: el SPA (panel "Aula 122" de ajustes) pide los AJUSTES nativos actuales
     *        (verbo /settings/native/get). El shell responde empujándolos vía applyNativeSettings.
     */
    void nativeSettingsGetRequested();
    /**
     * @brief Aula 122: el SPA pidió CAMBIAR un ajuste nativo (verbo /settings/native/set?key=..&value=..).
     *        El shell lo aplica con la misma lógica que el panel de Ajustes nativo (sin duplicar).
     */
    void nativeSettingChangeRequested(const QString& _key, const QString& _value);
    /**
     * @brief Aula 122: el menú web pidió una ACCIÓN de app que antes solo estaba en el ☰ nativo
     *        (verbo /action/<nombre>): import / save-as / create-project / open-project / fullscreen /
     *        signin / account / assistant / stats / sprint. El shell la enruta al MISMO slot del ☰.
     */
    void appActionRequested(const QString& _action);

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

    /**
     * @brief Aula 122 / UI unificada: aplica al SPA de Odiseo un tema que viene de los
     *        Ajustes nativos (canal nativo→web). Los colores llegan con '#'.
     */
    void applyThemeFromNative(const QString& _bg, const QString& _fg, const QString& _panel,
                              const QString& _accent, const QString& _error, const QString& _mode);

    /**
     * @brief Aula 122: empuja al SPA los AJUSTES nativos (objeto JSON) para su panel "Aula 122"
     *        (canal nativo→web, como el tema). Lo dispara ApplicationManager al recibir
     *        nativeSettingsGetRequested o en loadFinished (timing: el panel pudo pedirlos antes
     *        de que el SPA cargara). _jsonObject es un literal de objeto JS válido, p. ej. "{...}".
     */
    void applyNativeSettings(const QString& _jsonObject);

    /**
     * @brief Aula 122: avisar al SPA qué proyecto (.starc) está abierto en el editor nativo, para
     *        que la barra de Odiseo refleje SU árbol de documentos (y no el más reciente por fecha).
     *        Se llama al abrir/cambiar de proyecto. Canal nativo→web vía runJavaScript.
     */
    void setActiveProject(const QString& _path);

    /**
     * @brief Aula 122: pedir a la barra de Odiseo que recargue su árbol de documentos (p. ej. tras
     *        añadir un documento). Canal nativo→web vía runJavaScript (window.aula122RefreshTree).
     */
    void refreshProjectTree();

    /**
     * @brief Aula 122 / menú nativo: ocultar la barra/menú WEB de Odiseo dejando solo el chat (el
     *        menú pasa a ser el navegador nativo de STARC). Canal nativo→web.
     */
    void setMenuCollapsed(bool _collapsed);

    /**
     * @brief Aula 122: colapsar/mostrar el CHAT web de Odiseo dejando su MENÚ (canal nativo→web). En
     *        la vista de proyecto el chat arranca colapsado → el editor tiene aire; vuelve con el FAB.
     */
    void setChatCollapsed(bool _collapsed);

    /**
     * @brief Aula 122 (B): muestra/oculta el MENÚ de Odiseo en una VENTANA FLOTANTE propia, ENCIMA
     *        del editor de STARC (segunda vista web con la misma sesión). El menú flotante controla
     *        el editor (abrir documentos/secciones) y se oculta al navegar.
     */
    void toggleMenuOverlay();

    /**
     * @brief Aula 122 (B): muestra/oculta la TUERCA (botón nativo, abajo-izquierda sobre el editor)
     *        que invoca el menú flotante. showMenuGear se usa en la vista de PROYECTO; hideMenuGear
     *        en cuenta/onboarding/ajustes.
     */
    void showMenuGear();
    void hideMenuGear();

protected:
    /**
     * @brief Aula 122 (B): mientras el menú flotante está visible, lo reposicionamos cuando la
     *        ventana principal se mueve/redimensiona/cambia de estado → sigue pegado al editor.
     */
    bool eventFilter(QObject* _watched, QEvent* _event) override;

signals:
    /**
     * @brief El SPA pidió abrir una etapa del pipeline nativo
     *        ("guion" / "desglose" / "plan-rodaje" / ...).
     */
    void navigateRequested(const QString& _view);

    /**
     * @brief El SPA pidió abrir un DOCUMENTO concreto del proyecto por uuid
     *        (clic en un personaje/locación/subdocumento del árbol de la barra).
     */
    void documentRequested(const QString& _uuid);

    /**
     * @brief El SPA pidió añadir un documento (abre el diálogo nativo de alta).
     */
    void addDocumentRequested();

    /**
     * @brief El usuario ocultó/mostró el chat de Odiseo (el menú se queda).
     */
    void chatCollapseRequested(bool _collapsed);

    /**
     * @brief El SPA abrió/cerró una herramienta de Odiseo: el panel debe ir a pantalla completa y
     *        luego restaurarse. Reenvío de OdysseusPage::expandRequested.
     */
    void expandRequested(bool _on);

    /**
     * @brief El usuario pidió GUARDAR el proyecto desde la barra de Odiseo (verbo /project/save).
     */
    void saveProjectRequested();

    /**
     * @brief El usuario pidió EXPORTAR el documento actual desde la barra (verbo /project/export).
     */
    void exportProjectRequested();

    /**
     * @brief Aula 122 / UI unificada: el SPA cambió de tema; se reenvía al shell para
     *        sincronizar la DesignSystem nativa (canal web→nativo). Hex sin '#'.
     */
    void themeRequested(const QString& _bg, const QString& _fg, const QString& _panel,
                        const QString& _accent, const QString& _error, const QString& _mode);

    /**
     * @brief Aula 122 / UI unificada: el SPA cambió su FUENTE de UI; se reenvía al shell para
     *        sincronizar la fuente de la DesignSystem nativa (web→nativo).
     */
    void fontRequested(const QString& _family);

    /**
     * @brief Aula 122: el SPA pidió los ajustes nativos actuales (reenvío de OdysseusPage). El shell
     *        responde con applyNativeSettings(...). También se emite en loadFinished para refrescar.
     */
    void nativeSettingsGetRequested();

    /**
     * @brief Aula 122: el SPA pidió cambiar un ajuste nativo (reenvío de OdysseusPage). El shell lo
     *        aplica reusando la lógica del panel de Ajustes nativo.
     */
    void nativeSettingChangeRequested(const QString& _key, const QString& _value);

    /**
     * @brief Aula 122: acción de app pedida desde el menú web (reenvío de OdysseusPage). El shell la
     *        enruta al MISMO slot que el ☰ nativo (import/save-as/fullscreen/cuenta/stats/sprint…).
     */
    void appActionRequested(const QString& _action);

private:
    /**
     * @brief Aula 122: pinta el logo de Aula 122 + "Cargando Odiseo…" DENTRO del webview como HTML
     *        local (PNG embebido), mientras Odiseo arranca — en vez del error del webview.
     */
    void showSplash();

    /**
     * @brief Aula 122 (B): coloca el menú flotante como una franja a la IZQUIERDA del área de
     *        contenido de la ventana (sobre el editor y sus pestañas), de alto completo.
     */
    void positionOverlay();

    QWebEngineView* m_web = nullptr;
    QWidget* m_menuOverlay = nullptr; // Aula 122 (B): ventana flotante del menú, encima del editor.
    QPushButton* m_menuGear = nullptr; // Aula 122 (B): tuerca para invocar el menú flotante.
    QTimer* m_pollTimer = nullptr;
    bool m_loaded = false;
    bool m_spaLoaded = false;
    // Aula 122 (Etapa 1): estado de chat colapsado recordado para RE-APLICARLO cuando el
    // SPA termina de cargar (showProject puede pedirlo ANTES de que el SPA exista → la
    // llamada nativo→web se perdería). m_chatCollapsedSet evita tocar nada si nunca se pidió.
    bool m_chatCollapsed = false;
    bool m_chatCollapsedSet = false;
    // Aula 122 (fix navegación): proyecto activo recordado para RE-APLICARLO en loadFinished.
    // setActiveProject corre al abrir el proyecto, ANTES de que el SPA cargue → la llamada
    // nativo→web se perdía y el SPA quedaba mostrando el proyecto por defecto del backend
    // (otro .starc) → sus uuids no encajaban con el modelo nativo y los docs no abrían.
    QString m_activeProjectPath;
    bool m_activeProjectSet = false;
};

} // namespace Ui
