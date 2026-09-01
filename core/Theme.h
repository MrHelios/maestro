#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Tema de colores de la interfaz (v1.3).
//
// Concentra TODAS las secuencias ANSI de la interfaz que antes vivian
// hardcodeadas y repartidas entre el Renderer y el StatusBar. Cada
// componente UI recibe/usa un Theme y deja de interpretar colores: asi
// cambiar el esquema de color (u ofrecer varios temas) es tocar un solo
// lugar.
//
// Cada campo es una secuencia SGR COMPLETA (aunque incluye "\x1b["... y
// termina en "m") que activa ese estilo sobre el texto siguiente. Vacia
// ("" == sin estilo, hereda el color por defecto de la terminal). `reset`
// devuelve el texto al estado base.
//
// Lenguaje visual (v1.3): dos niveles bien diferenciados.
//   - ACTIVO   = la fila que el cursor/la lista marca como actual
//                (fila del cursor en el Editor, item seleccionado en el
//                selector/explorador): fondo gris (listSelected ==
//                currentLine). Es el estado de foco, no de texto.
//   - SELECCION = el rango de TEXTO marcado (video inverso): gana sobre el
//                ACTIVO y sobre el contenido normal.
// Los numeros de linea, el marcador "~" y las etiquetas de estado siguen
// jerarquias propias (ver cada campo).
//
// La paleta por defecto (defaultTheme) reproduce EXACTAMENTE los colores
// que estaban hardcodeados antes de la extraccion (v1.1/v1.2); los campos
// nuevos (gutterCurrent, marker, listSelected, prompt, statusBarModified y
// los accents por estado) extienden el esquema sin cambiar lo existente.
// ---------------------------------------------------------------------------
struct Theme {
    std::string background;       // fondo del area de texto y relleno de filas "~"
    std::string foreground;       // texto normal del documento
    std::string lineNumber;       // numeros de linea del gutter (gris tenue)
    std::string gutterCurrent;    // numero de linea de la fila del cursor (negrita blanca sobre gris)
    std::string marker;           // filas de relleno fuera del documento "~" (dim)
    std::string currentLine;      // resaltado de la fila del cursor (fondo gris)
    std::string selection;        // texto seleccionado (video inverso)
    std::string listSelected;     // item activo de las listas (mismo gris que currentLine)
    std::string statusBar;        // fondo/base de la barra de estado
    std::string statusBarName;    // nombre de archivo en la barra
    std::string statusBarPath;    // ruta en la barra
    std::string statusBarAccent;  // etiqueta de estado por defecto (fallback de estadoAccent)
    std::string statusBarModified;// indicador "[modificado]" en la barra
    std::string message;          // fila de mensajes (Info/ayuda, sin color)
    std::string prompt;           // prompts de entrada (p.ej. "Guardar archivo:")
    std::string success;          // mensajes de exito (verde)
    std::string warning;          // mensajes de aviso (amarillo)
    std::string error;            // mensajes de error (rojo)
    std::string reset;            // vuelve al estado base
    // Accents por estado activo de la barra (v1.3): cada modo de la
    // maquina de estados tiene su propio color para la etiqueta de estado
    // (NAVEGACION/INTERACCION/SELECCION/COMANDO/BUFFERS/GUARDAR/ABRIR).
    std::string accentNavegacion;
    std::string accentInteraccion;
    std::string accentSeleccion;
    std::string accentComando;
    std::string accentBuffers;
    std::string accentGuardar;
    std::string accentAbrir;
};

// ---------------------------------------------------------------------------
// Paleta por defecto: los colores que estaban hardcodeados en v1.1/v1.2
// mas los estilos nuevos del lenguaje visual v1.3.
//   - currentLine / listSelected : fondo gris (placeholder dim) sobre la
//     fila activa (cursor o item de lista). listSelected == currentLine a
//     proposito: ACTIVO significa lo mismo en el editor y en las listas.
//   - gutterCurrent   : numero de la fila activa, negrita blanca sobre el
//     mismo gris de currentLine (conecta el numero con el resaltado).
//   - lineNumber      : numeros normales en gris tenue (90).
//   - marker          : "~" en dim, mas tenue que los numeros.
//   - selection       : video inverso (siempre gana sobre lo demas).
//   - statusBar       : texto negro sobre fondo gris 60%
//                       (RGB(102,102,102) en truecolor, 0.4*255).
//   - name            : blanco;   path : negro;  accent (fallback) : bold
//                       dorado (paleta 256, 38;5;178).
//   - statusBarModified: "[modificado]" en negrita amarillo claro (aviso
//                       de cambios sin guardar, nunca se trunca).
//   - prompt          : prompts de entrada en negrita.
//   - accents         : negrita + color 256 suave, legible sobre el gris 60%
//                       de la barra: azul (navegacion), verde (interaccion),
//                       magenta (seleccion), dorado (comando), cyan
//                       (buffers/abrir), amarillo (guardar).
//   - success         : verde;  warning : amarillo;  error : rojo.
// ---------------------------------------------------------------------------
inline constexpr const char* kCurrentLineStyle    = "\x1b[48;5;237m";             // gris muy sutil
inline constexpr const char* kListSelectedStyle   = "\x1b[48;5;237m";             // gris muy sutil (activo)
inline constexpr const char* kSelectionStyle      = "\x1b[48;5;60m";              // azul grisáceo
inline constexpr const char* kLineNumberStyle     = "\x1b[38;5;242m";             // gris apagado
inline constexpr const char* kGutterCurrentStyle  = "\x1b[1m\x1b[38;5;81;48;5;237m"; // azul/gray sobre gris sutil
inline constexpr const char* kMarkerStyle         = "\x1b[38;5;65m";              // verde apagado
inline constexpr const char* kStatusBarStyle      = "\x1b[30m\x1b[48;2;102;102;102m";
inline constexpr const char* kStatusBarName       = "\x1b[37m";                  // blanco
inline constexpr const char* kStatusBarPath       = "\x1b[30m";                  // negro
inline constexpr const char* kStatusBarCommand    = "\x1b[1m\x1b[38;5;178m";     // bold + dorado
inline constexpr const char* kStatusBarModified   = "\x1b[1;38;5;221m";          // bold + amarillo claro
inline constexpr const char* kPromptStyle         = "\x1b[1m";                   // negrita
inline constexpr const char* kMessageSuccess      = "\x1b[38;5;250m";            // mismo color para todos los mensajes
inline constexpr const char* kMessageWarning      = "\x1b[38;5;250m";
inline constexpr const char* kMessageError        = "\x1b[38;5;250m";
inline constexpr const char* kMessageReset        = "\x1b[0m";

// Accents por estado activo - dark: todos igual a navegacion
inline constexpr const char* kAccentNavegacion   = "\x1b[1m\x1b[38;5;81m";       // azul clarito
inline constexpr const char* kAccentInteraccion  = "\x1b[1m\x1b[38;5;81m";
inline constexpr const char* kAccentSeleccion    = "\x1b[1m\x1b[38;5;81m";
inline constexpr const char* kAccentComando      = "\x1b[1m\x1b[38;5;81m";
inline constexpr const char* kAccentBuffers      = "\x1b[1m\x1b[38;5;81m";
inline constexpr const char* kAccentGuardar      = "\x1b[1m\x1b[38;5;81m";
inline constexpr const char* kAccentAbrir        = "\x1b[1m\x1b[38;5;81m";

inline constexpr const char* kLightBackground        = "\x1b[48;5;255m";
inline constexpr const char* kLightForeground        = "\x1b[38;5;235m";
inline constexpr const char* kLightReset             = "\x1b[0m\x1b[48;5;255m\x1b[38;5;235m";
inline constexpr const char* kLightCurrentLineStyle  = "\x1b[48;5;254m";
inline constexpr const char* kLightListSelectedStyle = "\x1b[48;5;253m";
inline constexpr const char* kLightSelectionStyle    = "\x1b[48;5;189m";
inline constexpr const char* kLightLineNumberStyle   = "\x1b[38;5;245m";
inline constexpr const char* kLightGutterCurrentStyle= "\x1b[1m\x1b[38;5;240;48;5;254m";
inline constexpr const char* kLightMarkerStyle       = "\x1b[38;5;102m";
inline constexpr const char* kLightStatusBarStyle    = "\x1b[38;5;235m\x1b[48;5;252m";
inline constexpr const char* kLightStatusBarName     = "\x1b[1m\x1b[38;5;235m";
inline constexpr const char* kLightStatusBarPath     = "\x1b[38;5;243m";
inline constexpr const char* kLightStatusBarCommand  = "\x1b[1m\x1b[38;5;25m";
inline constexpr const char* kLightStatusBarModified = "\x1b[1;38;5;160m";
inline constexpr const char* kLightPromptStyle       = "\x1b[1m";
inline constexpr const char* kLightMessageSuccess    = "\x1b[38;5;240m";
inline constexpr const char* kLightMessageWarning    = "\x1b[38;5;240m";
inline constexpr const char* kLightMessageError      = "\x1b[38;5;240m";
inline constexpr const char* kLightAccentNavegacion  = "\x1b[1m\x1b[38;5;25m";
inline constexpr const char* kLightAccentInteraccion = "\x1b[1m\x1b[38;5;25m";
inline constexpr const char* kLightAccentSeleccion   = "\x1b[1m\x1b[38;5;25m";
inline constexpr const char* kLightAccentComando     = "\x1b[1m\x1b[38;5;25m";
inline constexpr const char* kLightAccentBuffers     = "\x1b[1m\x1b[38;5;25m";
inline constexpr const char* kLightAccentGuardar     = "\x1b[1m\x1b[38;5;25m";
inline constexpr const char* kLightAccentAbrir       = "\x1b[1m\x1b[38;5;25m";

inline Theme darkTheme() {
    Theme t;
    t.background     = "";
    t.foreground     = "";
    t.lineNumber     = kLineNumberStyle;
    t.gutterCurrent  = kGutterCurrentStyle;
    t.marker         = kMarkerStyle;
    t.currentLine    = kCurrentLineStyle;
    t.selection      = kSelectionStyle;
    t.listSelected   = kListSelectedStyle;
    t.statusBar      = kStatusBarStyle;
    t.statusBarName  = kStatusBarName;
    t.statusBarPath  = kStatusBarPath;
    t.statusBarAccent = kStatusBarCommand;
    t.statusBarModified = kStatusBarModified;
    t.message        = "";
    t.prompt         = kPromptStyle;
    t.success        = kMessageSuccess;
    t.warning        = kMessageWarning;
    t.error          = kMessageError;
    t.reset          = kMessageReset;
    t.accentNavegacion  = kAccentNavegacion;
    t.accentInteraccion = kAccentInteraccion;
    t.accentSeleccion   = kAccentSeleccion;
    t.accentComando     = kAccentComando;
    t.accentBuffers     = kAccentBuffers;
    t.accentGuardar     = kAccentGuardar;
    t.accentAbrir       = kAccentAbrir;
    return t;
}

inline Theme lightTheme() {
    Theme t;
    t.background     = kLightBackground;
    t.foreground     = kLightForeground;
    t.lineNumber     = kLightLineNumberStyle;
    t.gutterCurrent  = kLightGutterCurrentStyle;
    t.marker         = kLightMarkerStyle;
    t.currentLine    = kLightCurrentLineStyle;
    t.selection      = kLightSelectionStyle;
    t.listSelected   = kLightListSelectedStyle;
    t.statusBar      = kLightStatusBarStyle;
    t.statusBarName  = kLightStatusBarName;
    t.statusBarPath  = kLightStatusBarPath;
    t.statusBarAccent = kLightStatusBarCommand;
    t.statusBarModified = kLightStatusBarModified;
    t.message        = "";
    t.prompt         = kLightPromptStyle;
    t.success        = kLightMessageSuccess;
    t.warning        = kLightMessageWarning;
    t.error          = kLightMessageError;
    t.reset          = kLightReset;
    t.accentNavegacion  = kLightAccentNavegacion;
    t.accentInteraccion = kLightAccentInteraccion;
    t.accentSeleccion   = kLightAccentSeleccion;
    t.accentComando     = kLightAccentComando;
    t.accentBuffers     = kLightAccentBuffers;
    t.accentGuardar     = kLightAccentGuardar;
    t.accentAbrir       = kLightAccentAbrir;
    return t;
}

inline Theme defaultTheme() {
    return darkTheme();
}

inline bool operator==(const Theme& a, const Theme& b) {
    return a.background == b.background && a.foreground == b.foreground &&
           a.lineNumber == b.lineNumber && a.gutterCurrent == b.gutterCurrent &&
           a.marker == b.marker && a.currentLine == b.currentLine &&
           a.selection == b.selection && a.listSelected == b.listSelected &&
           a.statusBar == b.statusBar && a.statusBarName == b.statusBarName &&
           a.statusBarPath == b.statusBarPath && a.statusBarAccent == b.statusBarAccent &&
           a.statusBarModified == b.statusBarModified && a.message == b.message &&
           a.prompt == b.prompt && a.success == b.success && a.warning == b.warning &&
           a.error == b.error && a.reset == b.reset &&
           a.accentNavegacion == b.accentNavegacion && a.accentInteraccion == b.accentInteraccion &&
           a.accentSeleccion == b.accentSeleccion && a.accentComando == b.accentComando &&
           a.accentBuffers == b.accentBuffers && a.accentGuardar == b.accentGuardar &&
           a.accentAbrir == b.accentAbrir;
}

inline bool operator!=(const Theme& a, const Theme& b) { return !(a == b); }
