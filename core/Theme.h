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
inline constexpr const char* kCurrentLineStyle    = "\x1b[100m";                 // gris
inline constexpr const char* kListSelectedStyle   = "\x1b[100m";                 // gris (activo)
inline constexpr const char* kSelectionStyle      = "\x1b[7m";                   // video inverso
inline constexpr const char* kLineNumberStyle     = "\x1b[90m";                  // gris tenue
inline constexpr const char* kGutterCurrentStyle  = "\x1b[1m\x1b[37;100m";       // negrita blanca sobre gris
inline constexpr const char* kMarkerStyle         = "\x1b[2m";                   // dim
inline constexpr const char* kStatusBarStyle      = "\x1b[30m\x1b[48;2;102;102;102m";
inline constexpr const char* kStatusBarName       = "\x1b[37m";                  // blanco
inline constexpr const char* kStatusBarPath       = "\x1b[30m";                  // negro
inline constexpr const char* kStatusBarCommand    = "\x1b[1m\x1b[38;5;178m";     // bold + dorado
inline constexpr const char* kStatusBarModified   = "\x1b[1;38;5;221m";          // bold + amarillo claro
inline constexpr const char* kPromptStyle         = "\x1b[1m";                   // negrita
inline constexpr const char* kMessageSuccess      = "\x1b[32m";                  // verde
inline constexpr const char* kMessageWarning      = "\x1b[33m";                  // amarillo
inline constexpr const char* kMessageError        = "\x1b[31m";                  // rojo
inline constexpr const char* kMessageReset        = "\x1b[0m";

// Accents por estado activo (negrita + color 256 suave).
inline constexpr const char* kAccentNavegacion   = "\x1b[1m\x1b[38;5;178m";      // dorado
inline constexpr const char* kAccentInteraccion  = "\x1b[1m\x1b[38;5;178m";
inline constexpr const char* kAccentSeleccion    = "\x1b[1m\x1b[38;5;178m";
inline constexpr const char* kAccentComando      = "\x1b[1m\x1b[38;5;178m";
inline constexpr const char* kAccentBuffers      = "\x1b[1m\x1b[38;5;178m";
inline constexpr const char* kAccentGuardar      = "\x1b[1m\x1b[38;5;221m";      // amarillo suave
inline constexpr const char* kAccentAbrir        = "\x1b[1m\x1b[38;5;178m";      

// El Theme por defecto (mismo aspecto que v1.1/v1.2 en los campos que ya
// existian; los nuevos extienden el esquema con el lenguaje v1.3).
inline Theme defaultTheme() {
    Theme t;
    t.background     = "";                     // hereda el fondo de la terminal
    t.foreground     = "";                     // hereda el color de la terminal
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
    t.message        = "";                     // Info sin color
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
