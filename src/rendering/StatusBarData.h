#pragma once

#include <string>

// ---------------------------------------------------------------------------
// StatusBarData: DTO puro de la fila superior del chrome (StatusBar). No conoce
// TtyTheme ni ANSI. Vive en rendering/ (zona pura). La codificacion ANSI
// vive en rendering/tty/TtyChrome que recibe el ChromeData completo.
// La fila inferior (MessageBar) viaja como `Message` dentro de ChromeData.
// ---------------------------------------------------------------------------
struct StatusBarData {
    std::string name;         // nombre del archivo (izquierda, StatusBar)
    std::string path;         // ruta (izquierda); vacia si no aplica
    std::string estado;       // etiqueta de estado (izquierda)
    std::string right;        // override del bloque derecho; vacio = calcular
    bool modified = false;    // indicador [*] junto al nombre
    int cursorLine = 0;       // fila del cursor (0-indexada)
    int cursorCol = 0;        // columna del cursor (0-indexada)
    int totalLines = 0;       // lineas del documento (porcentaje vertical)
};

inline bool operator==(const StatusBarData& a, const StatusBarData& b) {
    return a.name == b.name && a.path == b.path && a.estado == b.estado &&
           a.right == b.right && a.modified == b.modified &&
           a.cursorLine == b.cursorLine && a.cursorCol == b.cursorCol && a.totalLines == b.totalLines;
}

inline bool operator!=(const StatusBarData& a, const StatusBarData& b) {
    return !(a == b);
}
