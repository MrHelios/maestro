#pragma once

// ---------------------------------------------------------------------------
// Roles semánticos de estilo (Fase B/C-1).
//
// REGLA ARQUITECTÓNICA: el Frame usa estos roles, nunca secuencias ANSI/CSI.
// Cada backend decide cómo se representa un rol:
//   - TtyEncoder  : rol -> secuencia ANSI (lee el TtyTheme como tabla ANSI).
//   - GuiPainter  : rol -> color/fuente Qt (futuro).
//
// Este header es puro: no incluye el TtyTheme ni emite secuencias de escape,
// y no incluye nada de app/. El mapeo modo -> accent
// (State -> StyleRole) vive en el adaptador app/ChromePresentation.h, que sí
// conoce ambos lados.
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

    MsgInfo,            // MessageBar: tipos por MessageKind
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
