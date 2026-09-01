#pragma once

#include <string>
#include <string_view>
#include "core/utf8.h"

// Helpers de texto/UTF-8 compartidos entre el Renderer (que arma el frame)
// y el StatusBar (que arma la barra comun). Viven en un header para poder
// usarse desde ambos .cpp sin duplicar codigo.
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

} // namespace chrome