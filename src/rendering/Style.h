#pragma once

#include "app/EditorState.h"

// ---------------------------------------------------------------------------
// Roles semánticos de estilo (Fase B/C-1).
//
// REGLA ARQUITECTÓNICA: el Frame usa estos roles, nunca secuencias ANSI/CSI.
// Cada backend decide cómo se representa un rol:
//   - TtyEncoder  : rol -> secuencia ANSI (lee el Theme como tabla ANSI).
//   - GuiPainter  : rol -> color/fuente Qt (futuro).
//
// Este header es puro: no incluye Theme.h ni emite "\x1b". El Theme sigue
// siendo la tabla ANSI del backend TTY (ver rendering/tty/TtyEncoder).
// ---------------------------------------------------------------------------
enum class StyleRole {
    Default,            // texto plano sin estilo

    Gutter,             // número de línea inactiva
    GutterCurrent,      // número de línea del cursor
    GutterBlank,        // gutter de filas fuera del documento (espacios)
    Marker,             // "~" de relleno fuera del documento

    CurrentLine,        // resaltado de la fila del cursor
    Selection,          // texto seleccionado (gana sobre syntax/bracket)
    BracketMatch,       // paréntesis/corchete apareado
    ListSelected,       // ítem activo de listas (selector/explorador)

    StatusBase,         // fondo/base de la barra de estado
    StatusName,         // nombre de archivo en la barra
    StatusPath,         // ruta en la barra
    StatusModified,     // indicador [*]
    StatusAccentDefault,// etiqueta de estado (fallback)
    AccentNavegacion,
    AccentInteraccion,
    AccentSeleccion,
    AccentComando,
    AccentBuffers,
    AccentGuardar,
    AccentAbrir,

    MsgInfo,            // fila de mensajes: tipos por MessageKind
    MsgSuccess,
    MsgWarning,
    MsgError,
    MsgPrompt,

    SyntaxKeyword,      // 1:1 con SyntaxToken (ver syntaxRoleFor)
    SyntaxType,
    SyntaxPreprocessor,
    SyntaxString,
    SyntaxCharacter,
    SyntaxNumber,
    SyntaxComment,
};

// Accent por estado activo. Búsqueda comparte GUARDAR e IrAFila comparte
// NAVEGACIÓN por diseño (igual que el viejo statePresentation).
inline StyleRole accentRoleFor(State state) {
    switch (state) {
        case State::Navegacion:     return StyleRole::AccentNavegacion;
        case State::Interaccion:    return StyleRole::AccentInteraccion;
        case State::Seleccion:      return StyleRole::AccentSeleccion;
        case State::Prefix:         return StyleRole::AccentComando;
        case State::BufferSelector: return StyleRole::AccentBuffers;
        case State::SaveAs:         return StyleRole::AccentGuardar;
        case State::FileBrowser:    return StyleRole::AccentAbrir;
        case State::Busqueda:       return StyleRole::AccentGuardar;
        case State::IrAFila:        return StyleRole::AccentNavegacion;
    }
    return StyleRole::StatusAccentDefault;
}
