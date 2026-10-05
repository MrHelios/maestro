#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include "base/utf8.h"
#include "layout/Layout.h"
#include "platform/CellPos.h"
#include "rendering/ChromeData.h"

// Helpers puros compartidos entre el Renderer (que arma el frame) y el
// TtyChrome (que arma el chrome: StatusBar + MessageBar). Viven en un
// header para poder usarse desde ambos .cpp sin duplicar codigo.
namespace chrome {

// Devuelve la COLA de `line` de a lo sumo `maxTailCols` columnas visuales
// (el INICIO es el que se sacrifica). No corta caracteres multibyte.
inline std::string utf8Tail(const std::string& line, int maxTailCols) {
    int total = utf8::columnOf(line, static_cast<int>(line.size()));
    if (maxTailCols <= 0 || total <= maxTailCols) return line;
    return std::string(utf8::range(line, total - maxTailCols, total));
}

// Trunca manteniendo el INICIO (los primeros `maxCols` visibles), con ".."
//.. al frente. No corta caracteres multibyte por la mitad.
inline std::string utf8TruncateFront(const std::string& line, int maxCols) {
    if (maxCols <= 0) return line;
    if (utf8::columnOf(line, static_cast<int>(line.size())) <= maxCols) return line;
    const std::string ellipsis = "...";
    if (maxCols <= static_cast<int>(ellipsis.size()))
        return utf8::truncate(ellipsis, maxCols);
    return ellipsis + utf8Tail(line, maxCols - static_cast<int>(ellipsis.size()));
}

// Columnas visuales de `s` (ignora los bytes de continuacion UTF-8).
inline int colCount(std::string_view s) {
    return utf8::columnOf(s, static_cast<int>(s.size()));
}

// Nombre del archivo (la parte tras el ultimo '/' ; o el mismo si no
// tiene directorio).
inline std::string baseName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// Directorio del archivo (la parte antes del ultimo '/').
inline std::string dirName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

// Solo para MOSTRAR en la barra de estado: reemplaza el home del usuario
// por "~" al inicio de la ruta (estilo shell). NO toca filename real.
// Fuente única (antes duplicado en FrameBuilder.cpp y Renderer.cpp).
inline std::string collapseHome(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return path;
    std::string h(home);
    if (path.size() < h.size() || path.compare(0, h.size(), h) != 0)
        return path;
    if (path.size() > h.size() && path[h.size()] != '/')
        return path;
    return "~" + path.substr(h.size());
}

// Celda 0-based del cursor de edición del MessageBar. Replica el
// truncado/padding de TtyChrome::appendMessageBar para que el CUP caiga
// sobre lo pintado. `chromeArea` es layout.chrome.
// Requiere height>=2 (si no hay MessageBar devuelve CellPos inválida).
// Con msg.cursor (byte offset al final del input, antes de decoraciones
// como " - not found" o " (Control+S...)") el cursor va ahí; sin él, al
// final del texto visible. Siempre clampado a lo visible y a maxCol.
inline CellPos messageBarCursorCell(const Rect& chromeArea,
                                    const MessageBarData& msg) {
    if (chromeArea.height < 2 || chromeArea.width <= 0) return CellPos{};
    const int width = chromeArea.width;
    const int padL =
        kMessageBarPadLeft < width ? kMessageBarPadLeft : width;
    const int padR =
        kMessageBarPadRight < (width - padL) ? kMessageBarPadRight
                                            : (width - padL > 0 ? width - padL : 0);
    const int avail = width - padL - padR > 0 ? width - padL - padR : 0;
    int wantCols;
    if (msg.cursor.has_value()) {
        int off = *msg.cursor;
        if (off < 0) off = 0;
        if (off > static_cast<int>(msg.text.size())) off = static_cast<int>(msg.text.size());
        const std::string_view pre(msg.text.data(), static_cast<size_t>(off));
        const int preCols = utf8::columnOf(pre, static_cast<int>(pre.size()));
        wantCols = preCols < avail ? preCols : avail;
    } else {
        const std::string vis = utf8::truncate(msg.text, avail);
        wantCols = utf8::columnOf(vis, static_cast<int>(vis.size()));
    }
    int col = chromeArea.col + padL + wantCols;
    const int maxCol = chromeArea.col + width - 1;
    if (col > maxCol) col = maxCol;
    const int row = chromeArea.row + 1;
    return CellPos(col, row);
}

} // namespace chrome
