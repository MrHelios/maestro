#pragma once

#include <string>

struct Theme {
    int id;
    std::string background;        // fondo del area de texto y relleno de filas "~"
    std::string foreground;        // texto normal del documento
    std::string lineNumber;        // numeros de linea del gutter (gris tenue)
    std::string gutterCurrent;     // numero de linea de la fila del cursor (negrita blanca sobre gris)
    std::string marker;            // filas de relleno fuera del documento "~" (dim)
    std::string currentLine;       // resaltado de la fila del cursor (fondo gris)
    std::string selection;         // texto seleccionado (video inverso)
    std::string listSelected;      // item activo de las listas (mismo gris que currentLine)
    std::string statusBar;         // fondo/base de la barra de estado
    std::string statusBarName;     // nombre de archivo en la barra
    std::string statusBarPath;     // ruta en la barra
    std::string statusBarAccent;   // etiqueta de estado por defecto (fallback de estadoAccent)
    std::string statusBarModified; // indicador "[*]" en la barra (v1.4: antes "[modificado]")
    std::string message;           // fila de mensajes (Info/ayuda, sin color)
    std::string prompt;            // prompts de entrada (p.ej. "Guardar archivo:")
    std::string success;           // mensajes de exito
    std::string warning;           // mensajes de aviso
    std::string error;             // mensajes de error
    std::string reset;             // vuelve al estado base

    // (NAVEGACION/INTERACCION/SELECCION/COMANDO/BUFFERS/GUARDAR/ABRIR).
    std::string accentNavegacion;
    std::string accentInteraccion;
    std::string accentSeleccion;
    std::string accentComando;
    std::string accentBuffers;
    std::string accentGuardar;
    std::string accentAbrir;
    std::string syntaxKeyword;
    std::string syntaxType;
    std::string syntaxPreprocessor;
    std::string syntaxString;
    std::string syntaxCharacter;
    std::string syntaxNumber;
    std::string syntaxComment;
    std::string bracketMatch;
};

inline Theme darkTheme() {
    constexpr const char* kAccentDark = "\x1b[1m\x1b[38;5;81m"; // azul clarito
    constexpr const char* kMessage    = "\x1b[38;5;250m"; // mismo color para todos los mensajes
    constexpr const char* kStatusBar  = "\x1b[38;2;140;140;140m";

    Theme t;
    t.id = 0;
    t.background        = "\x1b[48;2;18;19;20m";   // #121314 alias
    t.foreground        = "";
    t.lineNumber        = "\x1b[38;5;242m";
    t.gutterCurrent     = "\x1b[1m\x1b[38;5;81;48;5;237m";
    t.marker            = "\x1b[38;5;65m";
    t.currentLine       = "\x1b[48;5;237m";
    t.selection         = "\x1b[48;5;60m";
    t.listSelected      = "\x1b[48;5;237m";
    t.statusBar         = "\x1b[38;2;140;140;140m\x1b[48;2;25;26;27m";
    t.statusBarName     = kStatusBar;
    t.statusBarPath     = kStatusBar;
    t.statusBarAccent   = "\x1b[1m\x1b[38;5;178m";
    t.statusBarModified = "\x1b[1;38;5;221m";
    t.message           = "";
    t.prompt            = "\x1b[1m"; // negrita
    t.success           = kMessage;
    t.warning           = kMessage;
    t.error             = kMessage;
    t.reset             = "\x1b[0m\x1b[48;2;18;19;20m"; // reset + restaura fondo #121314
    t.accentNavegacion  = kAccentDark;
    t.accentInteraccion = kAccentDark;
    t.accentSeleccion   = kAccentDark;
    t.accentComando     = kAccentDark;
    t.accentBuffers     = kAccentDark;
    t.accentGuardar     = kAccentDark;
    t.accentAbrir       = kAccentDark;
    t.syntaxKeyword      = "\x1b[38;5;81m";
    t.syntaxType         = "\x1b[38;5;141m";
    t.syntaxPreprocessor = "\x1b[38;5;208m";
    t.syntaxString       = "\x1b[38;5;114m";
    t.syntaxCharacter    = "\x1b[38;5;114m";
    t.syntaxNumber       = "\x1b[38;5;214m";
    t.syntaxComment      = "\x1b[38;5;242m";
    t.bracketMatch       = "\x1b[48;5;221m\x1b[38;5;235m";
    return t;
}

inline Theme lightTheme() {
    constexpr const char* kLightMessage  = "\x1b[38;5;240m";
    constexpr const char* kLightAccent   = "\x1b[1m\x1b[38;5;25m";

    Theme t;
    t.id = 1;
    t.background        = "\x1b[48;5;255m";
    t.foreground        = "\x1b[38;5;235m";
    t.lineNumber        = "\x1b[38;5;245m";
    t.gutterCurrent     = "\x1b[1m\x1b[38;5;240;48;5;254m";
    t.marker            = "\x1b[38;5;102m";
    t.currentLine       = "\x1b[48;5;254m";
    t.selection         = "\x1b[48;5;189m";
    t.listSelected      = "\x1b[48;5;253m";
    t.statusBar         = "\x1b[38;5;235m\x1b[48;5;252m";
    t.statusBarName     = "\x1b[1m\x1b[38;5;235m";
    t.statusBarPath     = "\x1b[38;5;243m";
    t.statusBarAccent   = "\x1b[1m\x1b[38;5;25m";
    t.statusBarModified = "\x1b[1;38;5;160m";
    t.message           = "";
    t.prompt            = "\x1b[1m";
    t.success           = kLightMessage;
    t.warning           = kLightMessage;
    t.error             = kLightMessage;
    t.reset             = "\x1b[0m\x1b[48;5;255m\x1b[38;5;235m";
    t.accentNavegacion  = kLightAccent;
    t.accentInteraccion = kLightAccent;
    t.accentSeleccion   = kLightAccent;
    t.accentComando     = kLightAccent;
    t.accentBuffers     = kLightAccent;
    t.accentGuardar     = kLightAccent;
    t.accentAbrir       = kLightAccent;
    t.syntaxKeyword     = "\x1b[38;5;25m";
    t.syntaxType        = "\x1b[38;5;55m";
    t.syntaxPreprocessor= "\x1b[38;5;130m";
    t.syntaxString      = "\x1b[38;5;22m";
    t.syntaxCharacter   = "\x1b[38;5;22m";
    t.syntaxNumber      = "\x1b[38;5;166m";
    t.syntaxComment     = "\x1b[38;5;242m";
    t.bracketMatch      = "\x1b[48;5;221m\x1b[38;5;235m";
    return t;
}

inline Theme defaultTheme() {
    return darkTheme();
}

inline bool operator==(const Theme& a, const Theme& b) {
    return a.id == b.id;
}

inline bool operator!=(const Theme& a, const Theme& b) {
    return !(a == b); 
}
