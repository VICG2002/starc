# Mapa de componentes HTML ↔ Qt (puente de diseño Aula 122)

Este archivo es el **diccionario de traducción** entre el wireframe HTML (lo que Victor
aprueba) y el código Qt Widgets (lo que se implementa). Mantiene el port **mecánico**: por
cada clase CSS de `aula122-tokens.css` hay un widget real del kit `Ui::` del fork.

- Kit Qt: `src/corelib/ui/widgets/` (38 widgets) y `src/corelib/ui/modules/` (13 módulos).
- Tokens: colores/tipografía/spacing vía `Ui::DesignSystem::color()`, `::font()`, `::layout()`.
- Regla: si en un wireframe aparece un patrón sin fila aquí, **se agrega la fila** antes de portar.

## Tabla de traducción

| Rol en pantalla | Clase CSS (wireframe) | Widget Qt (`Ui::`) | Header / notas de port |
|---|---|---|---|
| Barra superior | `.a122-appbar` | `Ui::AppBar` | `widgets/app_bar/app_bar.h` |
| Acción principal | `.a122-btn` | `Ui::Button` (contained) | `widgets/button/button.h`; `setText`, `setContained(true)` |
| Acción secundaria | `.a122-btn--text` | `Ui::Button` (flat) | mismo widget, `setContained(false)` |
| Botón con borde | `.a122-btn--outlined` | `Ui::Button` (outlined) | mismo widget |
| Botón de icono | `.a122-iconbtn` | `Ui::IconButton` | `widgets/icon_button/`; usa glyph Material (U+Fxxxx), no emoji |
| Campo de texto | `.a122-field` + `input` | `Ui::TextField` | `widgets/text_field/`; `setLabel`, `setHelper`, `setError` |
| Área de texto | `.a122-field` + `textarea` | `Ui::TextField` | `setMultiline(true)` (o `Ui::TextEdit` si es rico) |
| Desplegable | `.a122-field` + `select` | `Ui::ComboBox` | `widgets/combo_box/`; modelo `QAbstractItemModel` |
| Casilla | `.a122-check` | `Ui::CheckBox` | `widgets/check_box/` |
| Opción única | `.a122-check` (radio) | `Ui::RadioButton` | `widgets/radio_button/` |
| Interruptor on/off | `.a122-check` (toggle) | `Ui::Toggle` | `widgets/toggle/` |
| Panel / tarjeta | `.a122-card` | `Ui::Card` | `widgets/card/`; o `QWidget` con `DesignSystem::color().surface()` |
| Pestañas | `.a122-tabbar` + `.a122-tab` | `Ui::TabBar` | `widgets/tab_bar/`; señal `currentIndexChanged` |
| Tabla / lista de datos | `.a122-table` | `Ui::Tree` **o** `QTableWidget` | `widgets/tree/`; los plugins existentes usan `QTableWidget` |
| Etiqueta de estado (color-coded) | `.a122-chip` | `QLabel` estilizado | igual que categorías del breakdown (Bloque 5.C) |
| Texto (h4–caption) | `.t-h5`, `.t-body2`… | `Ui::Label` | `widgets/label/`; o `font()` directo: `DesignSystem::font().h5()` |
| Panel lateral | `.a122-drawer` | `Ui::Drawer` | `widgets/drawer/` |
| Diálogo modal | (overlay HTML) | `Ui::Dialog` / `AbstractDialog` | `widgets/dialog/` |
| Selector de fecha | `input[type=date]` | `Ui::DatePicker` | `widgets/date_picker/` |
| Deslizador | `input[type=range]` | `Ui::Slider` | `widgets/slider/` |
| Progreso | `.a122-progress` | `Ui::ProgressBar` / `CircularProgressBar` | `widgets/progress_bar/` |
| Chat (asistente) | (burbujas HTML) | `Ui::Chat` + módulo `ai_assistant` | `widgets/chat/`, `modules/ai_assistant/` |
| Pantalla (contenedor) | `.a122-screen` (1024px) | la vista del plugin (`IDocumentView`) | ancho lo fija el `QStackedWidget` del shell, no el plugin |

## Tokens (no hardcodear: leer del DesignSystem)

| CSS | Qt | Valor (tema claro) |
|---|---|---|
| `--accent` | `DesignSystem::color().accent()` | `#448aff` |
| `--surface` | `DesignSystem::color().surface()` | `#f3f3f3` |
| `--on-surface` | `DesignSystem::color().onSurface()` | `#000000` |
| `--divider` | onSurface @ 12% | `rgba(0,0,0,.12)` |
| `--px16` | `DesignSystem::layout().px16()` | 16 |
| `.t-h5` | `DesignSystem::font().h5()` | 24px |
| `.t-body2` | `DesignSystem::font().body2()` | 14px |

## Documentos tamaño carta (vistas que se EXPORTAN a PDF)

El call sheet (y la lista de crew, breakdown, presupuesto) son **documentos**, no paneles. Se
diseñan tamaño **carta** (8.5×11 in; horizontal = la misma hoja volteada) y en Qt NO se
construyen como widgets sino como **layout de impresión**.

| Rol | Clase CSS (wireframe) | Qt | Notas de port |
|---|---|---|---|
| Hoja carta (preview = PDF) | `.doc-letter` (816×1056) | `QTextDocument` + `QPrinter` | `QPageSize(QPageSize::Letter)`, márgenes 0.5in |
| Hoja horizontal | `.doc-letter--landscape` (1056×816) | íd. | `QPageLayout::Landscape` |
| Banda de encabezado | `.doc-head` | bloque del `QTextDocument` | logo + título + día |
| Bloques (contactos/schedule/clima) | `.doc-block` en `.doc-grid` | celdas del `QTextDocument` | |
| Tablas del documento | `.doc-table` / `.doc-deptrow` | tablas del `QTextDocument` | densas (10–11px) |
| Pie de página | `.doc-foot` | footer del `QPrinter` | walkie + contactos en cada hoja |
| Sello BORRADOR | `.doc-draft` | marca de agua condicional | si faltan campos requeridos |
| Marcador "Falta"/"Auto" | `.needs`/`.tag-falta`/`.tag-auto` | **solo UI de edición** | NO va al PDF (`@media print` los oculta) |

> Regla: lo que el editor muestra (la hoja) **es** lo que se exporta. La barra superior (día,
> Autocompletar, Exportar) es chrome de la app (`.no-print`). Un documento incompleto **sí** se
> exporta, marcado BORRADOR.

## Lo que NO traduce (cuidado al portar)

- **Responsive / breakpoints / mobile** del wireframe → **ignorar**. Aula 122 es ventana de
  escritorio; el layout se adapta con `QLayout` (stretch/spacers), no con media queries.
- **Sombras CSS** → en Qt las da el propio widget (`Ui::Card` ya tiene elevación); no replicar a mano.
- **Glyphs**: en HTML van emoji/placeholder; en Qt son iconos Material por codepoint
  (`DesignSystem::font().iconsMid()` + carácter U+Fxxxx).
- **Reactividad**: el wireframe es estático; en Qt los datos vienen del modelo
  (`corelib/business_layer/model/...`) vía señales.
