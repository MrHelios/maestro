#pragma once

#include <string>

#include "app/Message.h"

// ---------------------------------------------------------------------------
// StatusBarData: DTO puro de la barra común. No conoce Theme ni ANSI.
// Vive en rendering/ (zona pura). La codificación ANSI vive en
// rendering/tty/StatusBar (TtyStatusBar) que recibe este DTO.
// ---------------------------------------------------------------------------
struct StatusBarData {
    std::string name;         // nombre del archivo (izquierda)
    std::string path;         // ruta (izquierda); vacia si no aplica
    std::string estado;       // etiqueta de estado (izquierda)
    Message message;          // fila de mensajes (fila propia, coloreada por tipo)
    std::string right;        // override del bloque derecho; vacio = calcular
    bool modified = false;    // indicador [*] junto al nombre
    int cursorLine = 0;       // fila del cursor (0-indexada)
    int cursorCol = 0;        // columna del cursor (0-indexada)
    int totalLines = 0;       // lineas del documento (porcentaje vertical)
};

inline bool operator==(const StatusBarData& a, const StatusBarData& b) {
    return a.name == b.name && a.path == b.path && a.estado == b.estado &&
           a.message.text == b.message.text &&
           a.message.kind == b.message.kind && a.right == b.right && a.modified == b.modified &&
           a.cursorLine == b.cursorLine && a.cursorCol == b.cursorCol && a.totalLines == b.totalLines;
}

inline bool operator!=(const StatusBarData& a, const StatusBarData& b) {
    return !(a == b);
}
