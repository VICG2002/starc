#pragma once

#include <ui/widgets/widget/widget.h>


namespace Ui {

class ThemeSetupView;

/**
 * @brief Представление приложения
 */
class ApplicationView : public Widget
{
    Q_OBJECT

public:
    explicit ApplicationView(QWidget* _parent = nullptr);
    ~ApplicationView() override;

    /**
     * @brief Получить виджет настройки темы
     */
    ThemeSetupView* themeSetupView() const;

    /**
     * @brief Получить виджет основного представления
     */
    QWidget* view() const;

    /**
     * @brief Выдвинуть представление
     * @return Продолжительность анимации
     */
    int slideViewOut();

    /**
     * @brief Задать доступность кнопки скрытия навигационной панели сплитера
     */
    void setHideNavigationButtonAvailable(bool _available);

    /**
     * @brief Сохранить состояние
     */
    QVariantMap saveState() const;

    /**
     * @brief Восстановить состояние
     */
    void restoreState(bool _onboaringPassed, const QVariantMap& _state);

    /**
     * @brief Показать заданный контент
     */
    void showContent(QWidget* _toolbar, QWidget* _navigator, QWidget* _view,
                     bool _showNavigation = true);

    /**
     * @brief Aula 122: instalar la vista de Odiseo en su panel anfitrión (una sola vez)
     */
    void setOdiseoWidget(QWidget* _odiseo);

    /**
     * @brief Aula 122: Odiseo como anfitrión a pantalla completa (oculta el ensamblaje nativo)
     */
    void showOdiseoFull();

    /**
     * @brief Aula 122: Odiseo + el contenido nativo (editor) lado a lado, en la misma ventana.
     * @param _hideNativeNavigator si true (defecto), oculta el navegador nativo porque su función
     *        (árbol de documentos) ya vive en la barra de Odiseo → un solo menú. Ajustes pasa false:
     *        su navegador de SECCIONES no lo replica Odiseo, así que se conserva.
     */
    void showOdiseoBeside(bool _hideNativeNavigator = true);

    /**
     * @brief Aula 122 (B): el EDITOR ocupa TODO el ancho (el menú lateral fijo de Odiseo se oculta).
     *        El menú de Odiseo pasa a invocarse como ventana FLOTANTE encima del editor (la tuerca).
     */
    void showEditorFullWidth();

    /**
     * @brief Aula 122: oculta el navegador nativo (árbol de documentos + barra de proyecto) en la
     *        vista de PROYECTO, porque la navegación vive en la barra de Odiseo (que refleja los
     *        documentos del .starc). Así el editor nativo ocupa todo el ancho del panel nativo, al
     *        lado de Odiseo. (showOdiseoBeside ya lo oculta para TODAS las vistas con Odiseo al
     *        lado; este método se conserva por claridad en la vista de proyecto.)
     */
    void collapseNativeNavigator();

    /**
     * @brief Aula 122: permitir que la PRÓXIMA vista muestre su navegador nativo (desactiva el
     *        force-hide). Se llama ANTES de showContent() en las vistas cuyo navegador NO es
     *        redundante con la barra de Odiseo: cuenta, onboarding y AJUSTES (su navegador de
     *        secciones). En proyecto/proyectos NO se llama → el navegador nativo queda oculto y el
     *        menú de Odiseo es el único en esa pestaña.
     */
    void allowNativeNavigator();

    /**
     * @brief Aula 122 / menú nativo: reparto sensato de la vista de proyecto — Odiseo (chat) angosto
     *        a la izquierda, el navegador NATIVO (menú) ~240px y el editor con el resto. Da ancho al
     *        navegador para que NO quede colapsado (el menú debe verse).
     */
    void applyNativeMenuLayout();

    /**
     * @brief Aula 122: el usuario ocultó/mostró el CHAT de Odiseo desde su barra. Cuando se oculta,
     *        encogemos el panel de Odiseo al ancho de su barra (≈240px) — el MENÚ se queda visible
     *        y el editor nativo gana el espacio del chat. Al mostrar, restauramos el reparto
     *        lado-a-lado. Recuerda el estado para no perderlo al cambiar de vista/proyecto.
     */
    void setOdiseoChatCollapsed(bool _collapsed);

    /**
     * @brief Aula 122: una HERRAMIENTA de Odiseo se abrió/cerró. Al abrir (true) damos TODO el ancho
     *        al panel de Odiseo (ocultando el ensamblaje nativo) → la herramienta, cuyo modal es
     *        position:fixed, cubre toda el área como en Odiseo standalone, con la barra/menú visible.
     *        Al cerrar la última (false) restauramos el reparto previo reaplicando el modo recordado
     *        del chat (240px o 52/48). Sólo aplica con Odiseo al lado.
     */
    void setOdiseoExpanded(bool _on);

    /**
     * @brief Включить/отключить полноэкранный режим
     */
    void toggleFullScreen(bool _isFullScreen);

signals:
    /**
     * @brief Запрос на выход из полноэкранного режима
     */
    void turnOffFullScreenRequested();

    /**
     * @brief Запрос на закрытие приложения
     */
    void closeRequested();

protected:
    /**
     * @brief Переопределяем, чтобы вместо реального закрытия испустить сигнал о данном намерении
     */
    void closeEvent(QCloseEvent* _event) override;

    /**
     * @brief Обновить переводы
     */
    void updateTranslations() override;

    /**
     * @brief Обновляем навигатор при изменении дизайн системы
     */
    void designSystemChangeEvent(DesignSystemChangeEvent* _event) override;

private:
    class Implementation;
    QScopedPointer<Implementation> d;
};

} // namespace Ui
