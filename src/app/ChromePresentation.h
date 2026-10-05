#pragma once

#include <string>

#include "app/EditorState.h"
#include "app/Message.h"
#include "rendering/ChromeRequest.h"
#include "rendering/Style.h"
#include "rendering/frame/Frame.h"

// ---------------------------------------------------------------------------
// ChromePresentation: punto común de traducción de `State` a presentación.
//
// Traduce el modo del Editor (State) a primitivas puras que el rendering
// consume ya resueltas (ver rendering/ChromeRequest.h):
//
//   State -> etiqueta de la StatusBar (stateLabelForPresentation)
//   State -> rol de acento de la etiqueta (accentRoleFor)
//   State -> forma del cursor (cursorShapeFor; Interacción + 4 prompts parpadean)
//   State -> visibilidad por modo (cursorVisibleForMode; siempre visible)
//   State -> cursor en MessageBar (cursorInMessageBarFor; 4 prompts con input)
//   Message -> MessageBarData (toMessageBar; el expiry no viaja al rendering)
//   (Message, State) -> ChromeRequest (makeChromeRequest; lo usa app/ antes
//   de invocar a ScreenRenderer/FrameBuilder)
//
// REGLA DE CAPAS: este header vive en app/ porque el modo es concepto del
// Editor. rendering/frame/Frame.h, rendering/ChromeData.h,
// rendering/ChromeRequest.h y rendering/Style.h no incluyen nada de app/ y
// nunca traducen State: reciben la presentación ya resuelta. Es el punto
// común de traducción que app/ usa antes de invocar al rendering.
// ---------------------------------------------------------------------------

inline StyleRole accentRoleFor(State state) {
    // Búsqueda comparte GUARDAR e IrAFila comparte NAVEGACIÓN por diseño
    // (igual que el viejo statePresentation).
    switch (state) {
        case State::Navegacion:     return StyleRole::AccentNavegacion;
        case State::Interaccion:    return StyleRole::AccentInteraccion;
        case State::Seleccion:      return StyleRole::AccentSeleccion;
        case State::Prefix:         return StyleRole::AccentComando;
        case State::BufferSelector: return StyleRole::AccentBuffers;
        case State::FileBrowser:    return StyleRole::AccentAbrir;
        case State::Busqueda:       return StyleRole::AccentGuardar;
        case State::IrAFila:        return StyleRole::AccentNavegacion;
        case State::SaveAsFileBrowser: return StyleRole::AccentGuardar;
        case State::Renombrar: return StyleRole::AccentGuardar;
    }
    return StyleRole::StatusAccentDefault;
}

inline FrameCursorShape cursorShapeFor(State state) {
    // Interacción parpadea en el contenido; los 4 prompts con input en el
    // MessageBar (Búsqueda / IrAFila / Guardar como / Renombrar) parpadean
    // en el MessageBar. El resto es bloque fijo.
    switch (state) {
        case State::Interaccion:
        case State::Busqueda:
        case State::IrAFila:
        case State::SaveAsFileBrowser:
        case State::Renombrar:
            return FrameCursorShape::Bar;
        default:
            return FrameCursorShape::Block;
    }
}

// Visibilidad aportada por el modo (independiente del viewport): todos los
// modos muestran cursor (los prompts lo muestran en el MessageBar, ver
// cursorInMessageBarFor). El viewport puede ocultarlo igual si queda fuera.
inline bool cursorVisibleForMode(State state) {
    (void)state;
    return true;
}

// true si el cursor edita el MessageBar (se desactiva el del contenido).
inline bool cursorInMessageBarFor(State state) {
    return state == State::Busqueda || state == State::IrAFila ||
           state == State::SaveAsFileBrowser || state == State::Renombrar;
}

inline std::string stateLabelForPresentation(State state) {
    switch (state) {
        case State::Navegacion:     return "NAVEGACION";
        case State::Interaccion:    return "INTERACCION";
        case State::Seleccion:      return "SELECCION";
        case State::Prefix:         return "COMANDO";
        case State::BufferSelector: return "BUFFERS";
        case State::FileBrowser:    return "ABRIR";
        case State::Busqueda:       return "BUSQUEDA";
        case State::IrAFila:        return "IR A FILA";
        case State::SaveAsFileBrowser: return "GUARDAR COMO";
        case State::Renombrar: return "RENOMBRAR";
    }
    return "";
}

// Message del Editor (texto + tipo + expiry) -> DTO puro del MessageBar
// (texto + tipo + cursor de edición). El vencimiento temporal no es estado
// visual y nunca viaja al rendering.
inline MessageBarData toMessageBar(const Message& m) {
    MessageBarData b;
    b.text = m.text;
    b.kind = m.kind;
    b.cursor = m.cursor;
    return b;
}

// Traducción completa (Message, State) -> presentación pura. app/ la invoca
// antes de llamar al rendering; asi ningún header de rendering/ necesita
// conocer app/.
inline ChromeRequest makeChromeRequest(const Message& m, State s) {
    ChromeRequest r;
    r.message = toMessageBar(m);
    r.estado = stateLabelForPresentation(s);
    r.accent = accentRoleFor(s);
    r.cursorShape = cursorShapeFor(s);
    r.cursorVisibleByMode = cursorVisibleForMode(s);
    r.cursorInMessageBar = cursorInMessageBarFor(s);
    return r;
}
