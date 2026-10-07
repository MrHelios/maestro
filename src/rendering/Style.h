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

// ---------------------------------------------------------------------------
// Política compartida de fondos de fila (ÚNICA fuente).
//
// Responde "¿de qué familia es el fondo de este segmento?" sin saber de
// ANSI ni de RGB: cada backend mapea la familia a su representación
// (TtyTheme como tabla ANSI en TtyEncoder, GuiColor en GuiChrome).
// TtyEncoder conserva sus ramas por rol congeladas byte a byte (el theme
// combina fg+bg en un solo código para varios roles y no se puede
// reexpresar sin cambiar bytes); el test de paridad TTY↔GUI congela que
// ambas implementaciones deciden lo mismo para cada (rol, línea actual).
//
// Precedencia (igual que el merge de FrameBuilder, que fusiona selección
// y highlight de búsqueda en Selection): Selection > BracketMatch >
// ListSelected > CurrentLine > Content. Default y sintaxis en la línea
// actual toman el fondo de línea actual; el resto va sobre el fondo de
// contenido. Los roles de chrome (Status*/Accent*/Msg*) nunca aparecen en
// filas de contenido y caen en Content por construcción.
// ---------------------------------------------------------------------------
enum class RowBgKind {
    Content,      // fondo base del área de texto
    CurrentLine,  // resaltado de la fila del cursor (incluye ListSelected)
    Selection,    // selección o highlight de búsqueda (mismo rol)
    Bracket,      // paréntesis/corchete apareado
    List,         // ítem activo de listas modales
};

inline bool isSyntaxRole(StyleRole r) {
    switch (r) {
        case StyleRole::SyntaxKeyword:
        case StyleRole::SyntaxType:
        case StyleRole::SyntaxPreprocessor:
        case StyleRole::SyntaxString:
        case StyleRole::SyntaxCharacter:
        case StyleRole::SyntaxNumber:
        case StyleRole::SyntaxComment:
            return true;
        default:
            return false;
    }
}

inline RowBgKind rowBgKindFor(StyleRole role, bool isCurrentLine) {
    switch (role) {
        case StyleRole::Selection:
            return RowBgKind::Selection;
        case StyleRole::BracketMatch:
            return RowBgKind::Bracket;
        case StyleRole::ListSelected:
            return RowBgKind::List;
        case StyleRole::CurrentLine:
        case StyleRole::GutterCurrent:
            return RowBgKind::CurrentLine;
        default:
            if (isCurrentLine && isSyntaxRole(role))
                return RowBgKind::CurrentLine;
            return RowBgKind::Content;
    }
}
