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
//   State -> forma del cursor (cursorShapeFor)
//   State -> visibilidad por modo (cursorVisibleForMode; Busqueda oculta)
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
    return state == State::Interaccion ? FrameCursorShape::Bar
                                       : FrameCursorShape::Block;
}

// Visibilidad aportada por el modo (independiente del viewport): Busqueda
// oculta el cursor; el resto lo muestra (si el viewport lo contiene).
inline bool cursorVisibleForMode(State state) {
    return state != State::Busqueda;
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
// (texto + tipo). El vencimiento temporal no es estado visual y nunca viaja
// al rendering.
inline MessageBarData toMessageBar(const Message& m) {
    MessageBarData b;
    b.text = m.text;
    b.kind = m.kind;
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
    return r;
}
