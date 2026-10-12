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
//   - Keypad mínimo: solo SDLK_KP_ENTER se traduce; el KP numérico queda
//     Unknown para no secuestrar dígitos con NumLock.
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

// Puro y sin SDL: píxeles -> celda 0-based (dominio neutro CellPos).
// `cellW/cellH` son los ya resueltos por el loop (con fallback 9x18 si la
// fuente no cargó). Con métrica inválida (<=0) devuelve celda inválida
// {-1,-1}: el Editor la trata como fuera de contenido (click que cancela
// selección / drag ignorado), nunca como {0,0} espurio.
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
        int lower = sym;
        if (lower >= 'A' && lower <= 'Z') lower += ('a' - 'A');
        if (lower >= 'a' && lower <= 'z') {
            p.sym = static_cast<GuiKeySym>(
                static_cast<int>(GuiKeySym::KeyA) + (lower - 'a'));
        }
    // NOTA keypad: SDLK_KP_1..SDLK_KP_9 (y el resto del KP numérico)
    // quedan a propósito en Unknown para no secuestrar dígitos con
    // NumLock: solo KeyPadEnter está mapeado (alcance mínimo).
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

#endif  // HAVE_SDL2
