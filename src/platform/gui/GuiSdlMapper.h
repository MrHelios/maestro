#pragma once

// GuiSdlMapper — límite SDL del teclado/mouse GUI (único lugar que nombra
// tipos/constantes SDL: SDL_Keycode, KMOD_*, SDL_BUTTON_*, SDL_MOUSEWHEEL_*).
//
// Convierte `SDL_Keysym -> GuiKeyPress` (vocabulario propio, GuiKey.h) para
// que GuiKeymap y sus tests nunca vean SDL. La conversión píxeles -> CellPos
// (guiCellFromPixels) es pura y sin SDL: vive acá para que el loop y los
// tests la compartan, y existe aun sin HAVE_SDL2. El resto (teclas + botones
// + rueda) solo existe con HAVE_SDL2; sin SDL este header solo aporta la
// conversión de píxeles y GuiKeymap se prueba con GuiKeyPress directo.
//
// Reglas del adaptador (testeadas en tests/unit/test_guisdlmapper.cpp):
//   - CapsLock/NumLock (KMOD_CAPS/KMOD_NUM) se pelan: nunca llegan al keymap.
//   - Shift se conserva (el keymap lo ignora en la resolución, paridad TTY).
//   - AltGr (KMOD_MODE en X11) cuenta como Alt: el KEYDOWN de la letra queda
//     sin enlace y el carácter (@/|/\/{) llega por SDL_TEXTINPUT.
//   - Letras 'A'-'Z' se normalizan a minúscula lógica (KeyA..KeyZ).
//   - Keypad: SDLK_KP_ENTER se traduce siempre; KP_7/KP_1/KP_9/KP_3
//     (Inicio/Fin/RePag/AvPag) solo con NumLock APAGADO —con NumLock
//     producen dígitos por TEXTINPUT y traducirlos secuestraría números—.
//     El resto del KP numérico queda Unknown.
//   - Mouse (testeado en el mismo archivo): puntos de evento -> píxeles de
//     drawable (guiScaleCoord, obligatorio con ALLOW_HIGHDPI) -> CellPos
//     0-based por división entera (col = pxX / cellW, row = pxY / cellH);
//     solo el botón izquierdo produce Press/Release (medio/derecho ->
//     nullopt, paridad TTY donde se traducen a None); el drag se gatea por
//     SDL_BUTTON_LMASK.
//   - Rueda (testeada en el mismo archivo): decisión explícita DISCRETA con
//     acumulación. El Editor mueve ±3 líneas por evento (grilla en celdas:
//     la fracción no tiene destino), pero SDL 2.0.18+ trae `preciseY` y un
//     táctil manda `y == 0` con fracción real: descartarlo rompería el
//     scroll suave. Se acumula (guiWheelDelta + guiConsumeWheelDelta) y cada
//     unidad entera emite un ScrollUp/Down; una fracción sola jamás sale
//     como 3 líneas. Dirección FLIPPED incluida.
//   - Foco: solo SDL_WINDOWEVENT_FOCUS_LOST cierra el gesto (release
//     sintético); LEAVE y el resto no (el arrastre fuera sigue con captura).
#include <optional>

#include "platform/CellPos.h"
#include "platform/InputEvent.h"
#include "platform/WindowSize.h"

// Grilla GUI pura y sin SDL: píxeles del DRAWABLE / celda real -> WindowSize.
// Es la misma fórmula que el loop usa tras SDL_GetRendererOutputSize (nunca
// SDL_GetWindowSize: con ALLOW_HIGHDPI a escala N los puntos son N× menores
// y la grilla quedaría N× chica). `cellW/cellH` son los ya resueltos por el
// loop (fuente real o fallback si no cargó); `pxW/pxH` son del drawable.
// Con celda inválida (<=0) se usa el fallback canónico
// (platform::kFallbackCellW/H, ver WindowSize.h). Filas/cols se clampean a
// >=1 (ventana minimizada nunca da grilla 0x0 al Editor::resize).
// CONTRATO CERRADO (P3): la función no tiene precondiciones; las entradas
// negativas se normalizan (píxeles a 0 = desconocido, igual que
// Editor::resize; celdas al fallback) para que lo guardado nunca contamine
// el escalado puntos<->drawable. pixelW/H y cellW/H salen normalizados.
// CONTRATO HiDPI (tamaño visual estable, P1): `guiGridSize` divide píxeles
// FÍSICOS del drawable por celda FÍSICA. A escala N el drawable es N× la
// ventana en puntos: si la celda se quedara en el fallback, la grilla se
// duplicaría y el texto se vería a mitad de tamaño. Por eso la fuente (y el
// fallback sin fuente) SE ESCALAN con el drawable ANTES de dividir: a 2× la
// celda pasa a ~18×36 y la grilla se mantiene en ~106×33. El escalado usa
// guiScaleSize (redondeo, no truncado) por eje; la fuente usa el eje
// vertical (el tamaño tipográfico es altura).
// Testeada en tests/unit/test_guisdlmapper.cpp (escala 1×, 2× HiDPI,
// fallback, clamp mínimo, normalización de negativos).
inline platform::WindowSize guiGridSize(int pxW, int pxH, int cellW,
                                        int cellH) {
    if (cellW <= 0) cellW = platform::kFallbackCellW;
    if (cellH <= 0) cellH = platform::kFallbackCellH;
    if (pxW < 0) pxW = 0;
    if (pxH < 0) pxH = 0;
    platform::WindowSize s;
    s.pixelW = pxW;
    s.pixelH = pxH;
    s.cellW = cellW;
    s.cellH = cellH;
    s.cols = pxW / cellW;
    s.rows = pxH / cellH;
    if (s.cols < 1) s.cols = 1;
    if (s.rows < 1) s.rows = 1;
    return s;
}

// Escala un tamaño base entre espacios (puntos <-> drawable) con redondeo.
// Pura y sin SDL: `base` en unidades de `fromDim` -> unidades de `toDim`
// (tamaño de fuente y fallback de celda antes de guiGridSize).
// Identidad si base <= 0 o alguna dimensión es inválida (<=0: escala
// desconocida, se asume 1:1 = comportamiento pre-HiDPI).
// Matemática entera con `long long` intermedio (sin float): a 2×
// 16 -> 32 exacto; a 1.5× 16 -> 24 (redondeo, no truncado a 23).
inline int guiScaleSize(int base, int fromDim, int toDim) {
    if (base <= 0 || fromDim <= 0 || toDim <= 0) return base;
    return static_cast<int>((static_cast<long long>(base) * toDim +
                             fromDim / 2) /
                            fromDim);
}

// Tamaño de fuente escalado para el drawable (eje vertical: la talla es
// altura). `basePixels` es la talla a escala 1 (16 en el loop). A 2×
// (ventana 600 pts, drawable 1200 px) 16 -> 32: la celda TTF resultante
// (~18×36) mantiene la grilla visual estable tras guiGridSize.
// NOTA NO-UNIFORME: si la escala horizontal difiriera de la vertical (fuera
// de lo que SDL produce: el factor de backing es único para ambos ejes, el
// código por eje es defensa), la fuente sigue el eje VERTICAL a propósito
// (la talla tipográfica es altura) y el raster TTF escala uniforme con ella.
// Con píxeles anisotrópicos reales no se puede estabilizar filas y columnas
// a la vez sin distorsionar glifos: las filas quedan estables y las columnas
// derivan del eje horizontal (coords, fallback y grilla ya son por eje).
inline int guiScaledFontPixels(int basePixels, int winH, int outH) {
    return guiScaleSize(basePixels, winH, outH);
}

// Drawable efectivo con fallback POR EJE (puro y sin SDL): única fuente del
// fallback que el loop usa tras SDL_GetWindowSize/SDL_GetRendererOutputSize
// (currentSize y mouseCell deben dar el mismo espacio ante las mismas
// medidas; el init solo usa alturas porque la fuente sigue el eje vertical). Cada eje del drawable en <=0 cae a su propio eje de ventana, y
// si tampoco hay ventana a 960×600. Por eje, no conjunto: un eje válido
// nunca se descarta porque el otro degenere (minimizado parcial o driver
// que reporta un eje en 0). Con todo degenerado da 960×600 (escala 1:1
// asumida, comportamiento pre-HiDPI); la grilla además se clampe a >=1 en
// guiGridSize, así al Editor nunca llega 0×0.
struct GuiDrawableSize {
    int w = 0;
    int h = 0;
};
inline GuiDrawableSize guiDrawableOrWindow(int winW, int winH, int outW,
                                           int outH) {
    GuiDrawableSize d;
    d.w = outW > 0 ? outW : (winW > 0 ? winW : 960);
    d.h = outH > 0 ? outH : (winH > 0 ? winH : 600);
    return d;
}

// Puro y sin SDL: píxeles -> celda 0-based (dominio neutro CellPos).
// `cellW/cellH` son los ya resueltos por el loop (con fallback 9x18 si la
// fuente no cargó). Con métrica inválida (<=0) devuelve celda inválida
// {-1,-1}: el Editor la trata como fuera de contenido (click que cancela
// selección / drag ignorado), nunca como {0,0} espurio.
//
// CONTRATO DE ESPACIO (P2-1): NO escala. El caller (GuiRunLoop::mouseCell)
// debe traer AMBOS lados al mismo espacio antes de llamar: evento en puntos
// de ventana -> píxeles de drawable vía guiScaleCoord(pt, win, out), y celda
// base -> celda física vía guiScaleSize(base, win, out). Dividir puntos por
// celda física (o píxeles por celda sin escalar) da la celda N× chica/grande.
// La grilla sigue el mismo par: guiGridSize(outW, outH, celdaFísica).
//
// UNIDADES (HiDPI): la ventana se crea con SDL_WINDOW_ALLOW_HIGHDPI, así que
// hay DOS espacios: puntos de ventana (SDL_GetWindowSize, coordenadas de los
// eventos de mouse) y píxeles del drawable (SDL_GetRendererOutputSize,
// espacio donde el renderer dibuja —sin logical size/scale— y donde vive la
// celda raster de SDL_ttf). A escala 1 coinciden; a escala N difieren en N×.
// Todo lo que combine ambos lados DEBE escalar primero (guiScaleCoord): el
// loop divide eventos ya en píxeles de drawable, y el rect IME vuelve a
// puntos. Dividir puntos por celda directamente es el bug que esto evita.
inline CellPos guiCellFromPixels(int pxX, int pxY, int cellW, int cellH) {
    if (cellW <= 0 || cellH <= 0) return CellPos{};
    if (pxX < 0 || pxY < 0) return CellPos{};
    return CellPos{pxX / cellW, pxY / cellH};
}

// Variante ACOTADA por grilla (P2-2): píxeles del drawable -> celda solo si
// cae DENTRO de la grilla de `cols`×`rows` (las mismas que guiGridSize
// devolvió para este drawable/celda). Fuera de la grilla devuelve inválida,
// NUNCA clampeada a la última columna/fila: el borde exacto ENTRE celdas
// pertenece a la siguiente (9px con celda 9 -> col 1), pero el borde
// EXTERIOR queda fuera (x == cols*cellW -> inválida, aunque la división pura
// daría col == cols "válida"). Son dos condiciones distintas y esta función
// es la que las separa; screenToCursor sigue clampeaando el pasado-EOL
// DENTRO de la grilla a EOL (conducta histórica, ver mouse_map_past_eol_clamps).
// Pura y sin SDL: compara ÍNDICES (col >= cols), nunca anchos en píxeles
// (cols*cellW desbordaría con valores grandes); la división px/celda con
// int es segura (sin overflow). Con celda o grilla inválida (<=0) devuelve
// inválida. El loop la usa en mouseCell con la grilla del currentSize.
inline CellPos guiCellFromPixelsBounded(int pxX, int pxY, int cellW, int cellH,
                                        int cols, int rows) {
    if (cellW <= 0 || cellH <= 0) return CellPos{};
    if (cols <= 0 || rows <= 0) return CellPos{};
    if (pxX < 0 || pxY < 0) return CellPos{};
    const int col = pxX / cellW;
    const int row = pxY / cellH;
    if (col >= cols || row >= rows) return CellPos{};
    return CellPos{col, row};
}

// Escala lineal de una coordenada entre espacios (puntos <-> drawable).
// Pura y sin SDL: `v` en unidades de `fromDim` -> unidades de `toDim`
// (evento a drawable antes de /celda; rect IME de vuelta a puntos).
// Identidad si alguna dimensión es inválida (<=0: escala desconocida, se
// asume 1:1 = comportamiento pre-HiDPI). Sin clamp de negativos: el caller
// decide (guiCellFromPixels los rechaza; el rect IME nunca los produce).
// Matemática entera con `long long` intermedio (sin float): el truncado es
// <1px y nunca cambia la celda (un múltiplo exacto se calcula exacto).
inline int guiScaleCoord(int v, int fromDim, int toDim) {
    if (fromDim <= 0 || toDim <= 0) return v;
    return static_cast<int>(static_cast<long long>(v) * toDim / fromDim);
}

// Rectángulo IME en píxeles del drawable -> puntos de ventana para
// SDL_SetTextInputRect. Puro y sin SDL: congela la dirección (drawable ->
// ventana, por eje) para que no se pueda invertir ni cruzar ejes por
// accidente en el call site (el bug clásico HiDPI: pasar el rect del
// drawable tal cual y que la candidata quede N× desplazada/agigantada).
//
// SUPUESTO DE UNIDADES (P2-IME): SDL2 no documenta con precisión las
// unidades de SDL_SetTextInputRect, pero el rect se usa junto a coordenadas
// de eventos de mouse (puntos de ventana) y SDL3 lo explicita como
// relativo a la ventana: se asumen puntos de ventana. A escala 1 es
// identidad; a escala N se baja N× por eje (inverso de guiScaleCoord en el
// camino evento->drawable). Con medidas inválidas (<=0) es identidad
// (comportamiento pre-HiDPI). El caller conserva la política de rect
// inválido (no tocar SDL); acá solo se convierte.
// Testeado en tests/unit/test_guisdlmapper.cpp (1×, 2×, ida y vuelta).
struct GuiImeRectPx {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};
inline GuiImeRectPx guiImeRectToWindow(GuiImeRectPx r, int winW, int winH,
                                       int outW, int outH) {
    GuiImeRectPx o;
    o.x = guiScaleCoord(r.x, outW, winW);
    o.y = guiScaleCoord(r.y, outH, winH);
    o.w = guiScaleCoord(r.w, outW, winW);
    o.h = guiScaleCoord(r.h, outH, winH);
    return o;
}

// Delta de rueda en unidades de "click" (puro y sin SDL: el loop pasa
// ev.wheel.preciseY / ev.wheel.y / flipped ya extraídos).
// SDL 2.0.18+ llena `precise` (== y en rueda clásica, fracción en táctil):
// manda si es distinto de cero; si no, fallback al entero (drivers sin
// preciso o runtimes viejos). `flipped` niega (trackpads con scroll
// natural). Ambos cero -> 0.0 (nada que hacer).
inline double guiWheelDelta(float precise, int discrete, bool flipped) {
    double d = (precise != 0.0f) ? static_cast<double>(precise)
                                 : static_cast<double>(discrete);
    return flipped ? -d : d;
}

// Acumulador de fracciones de rueda (puro y sin SDL).
// Suma `delta` a `acc` y devuelve las unidades enteras que salen hacia el
// Editor (+ = ScrollUp, - = ScrollDown); el resto queda en `acc` para el
// próximo evento. Truncado hacia cero: direcciones opuestas se cancelan
// (0.5 - 0.5 = 0, sin eventos fantasma) y un táctil necesita juntar 1.0
// para mover: la fracción nunca se convierte sola en ±3 líneas.
inline int guiConsumeWheelDelta(double& acc, double delta) {
    acc += delta;
    const int n = static_cast<int>(acc);
    acc -= n;
    return n;
}

#ifdef HAVE_SDL2

#include <SDL2/SDL.h>

#include "platform/gui/GuiKey.h"

inline GuiKeyPress guiKeyFromSdl(SDL_Keycode sym, Uint16 mod, bool repeat) {
    GuiKeyPress p;
    p.repeat = repeat;
    p.mods.ctrl = (mod & KMOD_CTRL) != 0;
    p.mods.shift = (mod & KMOD_SHIFT) != 0;
    p.mods.alt = (mod & (KMOD_ALT | KMOD_MODE)) != 0;
    switch (sym) {
        case SDLK_ESCAPE: p.sym = GuiKeySym::Escape; break;
        case SDLK_RETURN: p.sym = GuiKeySym::Enter; break;
        case SDLK_KP_ENTER: p.sym = GuiKeySym::KeyPadEnter; break;
        case SDLK_TAB: p.sym = GuiKeySym::Tab; break;
        case SDLK_BACKSPACE: p.sym = GuiKeySym::Backspace; break;
        case SDLK_DELETE: p.sym = GuiKeySym::Delete; break;
        case SDLK_LEFT: p.sym = GuiKeySym::Left; break;
        case SDLK_RIGHT: p.sym = GuiKeySym::Right; break;
        case SDLK_UP: p.sym = GuiKeySym::Up; break;
        case SDLK_DOWN: p.sym = GuiKeySym::Down; break;
        case SDLK_HOME: p.sym = GuiKeySym::Home; break;
        case SDLK_END: p.sym = GuiKeySym::End; break;
        case SDLK_PAGEUP: p.sym = GuiKeySym::PageUp; break;
        case SDLK_PAGEDOWN: p.sym = GuiKeySym::PageDown; break;
        default: break;
    }
    if (p.sym == GuiKeySym::Unknown) {
        // Keypad navegacional SOLO con NumLock apagado: KP_7/KP_1 son
        // Inicio/Fin y KP_9/KP_3 son RePag/AvPag (los sym 1073741919/13/21/15
        // con mod=0x0 del reporte en ventana real). Con NumLock esas mismas
        // teclas producen dígitos por SDL_TEXTINPUT y traducirlas aquí
        // secuestraría la entrada numérica; sin NumLock no generan texto y
        // son navegación pura. El resto del KP numérico sigue en Unknown.
        if ((mod & KMOD_NUM) == 0) {
            switch (sym) {
                case SDLK_KP_7: p.sym = GuiKeySym::Home; break;
                case SDLK_KP_1: p.sym = GuiKeySym::End; break;
                case SDLK_KP_9: p.sym = GuiKeySym::PageUp; break;
                case SDLK_KP_3: p.sym = GuiKeySym::PageDown; break;
                default: break;
            }
        }
    }
    if (p.sym == GuiKeySym::Unknown) {
        int lower = sym;
        if (lower >= 'A' && lower <= 'Z') lower += ('a' - 'A');
        if (lower >= 'a' && lower <= 'z') {
            p.sym = static_cast<GuiKeySym>(
                static_cast<int>(GuiKeySym::KeyA) + (lower - 'a'));
        }
    // NOTA keypad: el resto del KP numérico (KP_0/KP_2/KP_4-6/KP_8, punto…)
    // queda en Unknown; KP_7/KP_1/KP_9/KP_3 solo se traducen sin NumLock
    // (ver bloque de arriba): con NumLock producen dígitos por TEXTINPUT.
    }
    return p;
}

// --- Límite SDL del mouse (solo HAVE_SDL2; testeado igual que el teclado) ---
//
// Paridad TTY (decodeMouseSgr): medio/derecho -> None. Acá se expresa como
// nullopt (el loop no fabrica InputEvent); el Editor ya gatea el drag sin
// press previo (mouseGestureActive_), así que un Motion con LMask suelto
// sin gesto se ignora igual que en TTY.

// SDL_BUTTON_* -> Press: solo el izquierdo arma gesto; el resto nullopt.
inline std::optional<InputEventType> guiPressTypeForButton(Uint8 button) {
    if (button == SDL_BUTTON_LEFT) return InputEventType::MousePress;
    return std::nullopt;
}

// SDL_BUTTON_* -> Release: solo el izquierdo cierra el gesto.
inline std::optional<InputEventType> guiReleaseTypeForButton(Uint8 button) {
    if (button == SDL_BUTTON_LEFT) return InputEventType::MouseRelease;
    return std::nullopt;
}

// Estado de botones de SDL_MOUSEMOTION (ev.motion.state): true si el
// izquierdo va presionado (el loop emite MouseDrag solo en ese caso).
inline bool guiLeftHeld(Uint32 motionState) {
    return (motionState & SDL_BUTTON_LMASK) != 0;
}

// Rueda SDL_MOUSEWHEEL (ev.wheel.y, +arriba/-abajo) -> evento semántico.
// Un InputEvent por unidad: el Editor ya mueve ±3 líneas por evento
// (vista.scroll.arriba/abajo), así que el loop repite según |y|.
inline std::optional<InputEventType> guiScrollTypeForWheel(int y) {
    if (y > 0) return InputEventType::ScrollUp;
    if (y < 0) return InputEventType::ScrollDown;
    return std::nullopt;
}

// SDL_WINDOWEVENT.window.event -> ¿cierra el gesto de mouse?
// Solo FOCUS_LOST: al perder foco la captura se desactiva sola y el
// BUTTONUP puede ir a otra ventana (release perdido), así que el loop
// sintetiza MouseRelease. Cualquier otro evento (incluido LEAVE: el
// arrastre fuera sigue entregándose gracias a la captura) conserva el
// gesto. Testeable sin ventana: solo nombra la constante del evento.
inline bool guiFocusLossEndsGesture(Uint8 windowEventId) {
    return windowEventId == SDL_WINDOWEVENT_FOCUS_LOST;
}

// SDL_WINDOWEVENT.window.event -> ¿hay que re-medir el drawable y la grilla?
// P2 (cambio de monitor): SDL recomienda re-consultar el drawable tras
// eventos importantes porque puede cambiar al mover la ventana entre
// pantallas. Se re-mide ante RESIZED + SIZE_CHANGED + DISPLAY_CHANGED:
// DISPLAY_CHANGED cubre el caso sin cambio de tamaño en puntos (1× -> 2×
// con la misma ventana lógica: el drawable duplica pero ningún resize
// llegaría). MINIMIZED/MAXIMIZED no re-miden: maximizar llega con
// SIZE_CHANGED, y minimizar conserva la última grilla (o la degradada por
// eje de guiDrawableOrWindow); al restaurar, SIZE_CHANGED/DISPLAY_CHANGED
// re-mide. El re-medido (currentSize en el loop) re-consulta
// SDL_GetRendererOutputSize, re-escala la fuente (contrato visual-estable)
// y recalcula rows/cols; el mouse y el rect IME ya miden fresco por
// evento/render, así que también siguen al monitor nuevo sin código extra.
// NOTA DE VERSIÓN: SDL_WINDOWEVENT_DISPLAY_CHANGED existe desde SDL 2.0.9;
// el proyecto asume SDL2 >= 2.0.9 (CI con 2.30). No hay guard de versión:
// referenciar el enum con un SDL más viejo fallaría en compilación a
// propósito (mejor que silenciar el caso 1×->2×).
// Testeable sin ventana: solo nombra constantes del evento.
inline bool guiResizeNeedsRemeasure(Uint8 windowEventId) {
    return windowEventId == SDL_WINDOWEVENT_RESIZED ||
           windowEventId == SDL_WINDOWEVENT_SIZE_CHANGED ||
           windowEventId == SDL_WINDOWEVENT_DISPLAY_CHANGED;
}

#endif  // HAVE_SDL2
