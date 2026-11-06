#pragma once

#include <string>

#include "app/EditorState.h"
#include "app/Message.h"
#include "rendering/ChromeRequest.h"
#include "rendering/Style.h"
#include "rendering/frame/Frame.h"

inline StyleRole accentRoleFor(State state) {
    switch (state) {
        case State::Navegacion:        return StyleRole::AccentNavegacion;
        case State::Interaccion:       return StyleRole::AccentInteraccion;
        case State::Seleccion:         return StyleRole::AccentSeleccion;
        case State::Prefix:            return StyleRole::AccentComando;
        case State::BufferSelector:    return StyleRole::AccentBuffers;
        case State::FileBrowser:       return StyleRole::AccentAbrir;
        case State::IrAFila:           return StyleRole::AccentNavegacion;
        case State::Busqueda:          
        case State::SaveAsFileBrowser: 
        case State::Renombrar:         return StyleRole::AccentGuardar;
    }
    return StyleRole::StatusAccentDefault;
}

// Interacción parpadea en el contenido.
// MessageBar (Búsqueda / IrAFila / Guardar como / Renombrar) parpadean.
// El resto es bloque fijo.
inline FrameCursorShape cursorShapeFor(State state) {    
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

// Visibilidad aportada por el modo (independiente del viewport): 
// todos los modos muestran cursor (los prompts lo muestran en el MessageBar). 
// El viewport puede ocultarlo igual si queda fuera.
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
        case State::Navegacion:        return "NAVEGACION";
        case State::Interaccion:       return "INTERACCION";
        case State::Seleccion:         return "SELECCION";
        case State::Prefix:            return "COMANDO";
        case State::BufferSelector:    return "BUFFERS";
        case State::FileBrowser:       return "ABRIR";
        case State::Busqueda:          return "BUSQUEDA";
        case State::IrAFila:           return "IR A FILA";
        case State::SaveAsFileBrowser: return "GUARDAR COMO";
        case State::Renombrar:         return "RENOMBRAR";
    }
    return "";
}

// Message del Editor (texto + tipo + expiry)
// (texto + tipo + cursor de edición)
// El vencimiento temporal no es estado visual y nunca viaja al rendering.
inline MessageBarData toMessageBar(const Message& m) {
    MessageBarData b;
    b.text = m.text;
    b.kind = m.kind;
    b.cursor = m.cursor;
    b.boldPrefix = m.boldPrefix;
    return b;
}

// Traducción completa (Message, State) -> presentación pura. 
// app/ la invoca antes de llamar al rendering 
// asi ningún header de rendering/ necesita conocer app/.
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
