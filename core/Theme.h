#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Tema de colores de la interfaz (v1.2).
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
// La paleta por defecto (defaultTheme) reproduce EXACTAMENTE los colores
// que estaban hardcodeados antes de la extraccion; el comportamiento
// visual es identico.
// ---------------------------------------------------------------------------
struct Theme {
    std::string background;       // fondo del area de texto y relleno de filas "~"
    std::string foreground;       // texto normal del documento
    std::string lineNumber;       // numeros de linea del gutter
    std::string currentLine;      // resaltado de la fila del cursor
    std::string selection;        // texto seleccionado (video inverso)
    std::string statusBar;        // fondo/base de la barra de estado
    std::string statusBarName;    // nombre de archivo en la barra
    std::string statusBarPath;    // ruta en la barra
    std::string statusBarAccent;  // comando/estado destacado en la barra
    std::string message;          // fila de mensajes (Info/ayuda, sin color)
    std::string success;          // mensajes de exito (verde)
    std::string warning;          // mensajes de aviso (amarillo)
    std::string error;            // mensajes de error (rojo)
    std::string reset;            // vuelve al estado base
};

// ---------------------------------------------------------------------------
// Paleta por defecto: los colores que estaban hardcodeados en v1.1.
//   - currentLine  : gris (placeholder dim) sobre la fila del cursor.
//   - selection    : video inverso.
//   - statusBar    : texto negro sobre fondo gris 60%
//                    (RGB(102,102,102) en truecolor, 0.4*255).
//   - name         : blanco;   path : negro;  accent (comando) : bold dorado
//                    (paleta 256, 38;5;178).
//   - success      : verde;  warning : amarillo;  error : rojo.
// ---------------------------------------------------------------------------
inline constexpr const char* kCurrentLineStyle = "\x1b[100m";                 // gris
inline constexpr const char* kSelectionStyle    = "\x1b[7m";                  // video inverso
inline constexpr const char* kStatusBarStyle     = "\x1b[30m\x1b[48;2;102;102;102m";
inline constexpr const char* kStatusBarName      = "\x1b[37m";                // blanco
inline constexpr const char* kStatusBarPath      = "\x1b[30m";                // negro
inline constexpr const char* kStatusBarCommand   = "\x1b[1m\x1b[38;5;178m";   // bold + dorado
inline constexpr const char* kMessageSuccess     = "\x1b[32m";                // verde
inline constexpr const char* kMessageWarning     = "\x1b[33m";                // amarillo
inline constexpr const char* kMessageError       = "\x1b[31m";                // rojo
inline constexpr const char* kMessageReset       = "\x1b[0m";

// El Theme por defecto (mismo aspecto que v1.1: todos los colores se
// centralizan aqui).
inline Theme defaultTheme() {
    Theme t;
    t.background     = "";                     // hereda el fondo de la terminal
    t.foreground     = "";                     // hereda el color de la terminal
    t.lineNumber     = "";                     // sin estilo
    t.currentLine    = kCurrentLineStyle;
    t.selection      = kSelectionStyle;
    t.statusBar      = kStatusBarStyle;
    t.statusBarName  = kStatusBarName;
    t.statusBarPath  = kStatusBarPath;
    t.statusBarAccent = kStatusBarCommand;
    t.message        = "";                     // Info sin color
    t.success        = kMessageSuccess;
    t.warning        = kMessageWarning;
    t.error          = kMessageError;
    t.reset          = kMessageReset;
    return t;
}