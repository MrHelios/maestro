#include "Renderer.h"

#include <unistd.h>
#include <sstream>
#include <algorithm>

namespace {

// Cuenta cuantas COLUMNAS VISUALES ocupan los primeros `byteCol` bytes
// de `line`. Es necesario porque un caracter UTF-8 (por ejemplo "—",
// un guion largo) puede ocupar varios bytes en el std::string pero
// UNA sola columna en la terminal. Si contamos bytes en vez de
// columnas, el cursor termina posicionado mas a la derecha de donde
// realmente esta el texto, dejando un hueco visual.
//
// Nota: esto asume caracteres de ancho 1 (correcto para acentos, "—",
// etc). Caracteres de ancho doble (CJK, emojis) quedan fuera del
// alcance de v0.1.
int utf8ColumnOf(const std::string& line, int byteCol) {
    int col = 0;
    int limit = std::min<int>(byteCol, static_cast<int>(line.size()));
    for (int i = 0; i < limit; ++i) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        // Un byte es "inicio de caracter" si NO es un byte de
        // continuacion UTF-8 (los bytes de continuacion tienen el
        // patron 10xxxxxx).
        if ((c & 0xC0) != 0x80) {
            col++;
        }
    }
    return col;
}

// Trunca `line` a lo sumo `maxCols` COLUMNAS VISUALES, sin cortar un
// caracter multibyte por la mitad (lo que generaria bytes invalidos
// y corromperia el resto del render).
std::string utf8Truncate(const std::string& line, int maxCols) {
    int col = 0;
    size_t i = 0;
    while (i < line.size()) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if ((c & 0xC0) != 0x80) {
            if (col >= maxCols) break;
            col++;
        }
        i++;
    }
    return line.substr(0, i);
}

} // namespace

void Renderer::render(const Document& doc,
                       const Cursor& cursor,
                       const Viewport& viewport,
                       const std::string& filename,
                       bool modified,
                       const std::string& statusMessage) {
    // Armamos todo en un unico buffer y lo escribimos de una sola vez
    // para evitar parpadeo.
    std::ostringstream out;

    out << "\x1b[?25l"; // ocultar cursor mientras dibujamos
    out << "\x1b[H";    // mover cursor a home (fila 1, col 1)

    for (int row = 0; row < viewport.height; ++row) {
        int docLine = viewport.top + row;
        out << "\x1b[K"; // limpiar la linea actual

        if (docLine < doc.lineCount()) {
            out << utf8Truncate(doc.lineAt(docLine), viewport.width);
        } else {
            out << "~"; // linea fuera del documento, estilo vim
        }
        out << "\r\n";
    }

    // Barra de estado (ultima fila)
    out << "\x1b[K";
    out << "\x1b[7m"; // video invertido
    std::ostringstream status;
    status << (filename.empty() ? "[sin nombre]" : filename)
           << (modified ? " [modificado]" : "")
           << " -- Ln " << (cursor.line + 1) << ", Col " << (cursor.col + 1)
           << "  |  " << statusMessage;
    out << utf8Truncate(status.str(), viewport.width);
    out << "\x1b[0m"; // reset de estilo

    // Posicionar el cursor real de la terminal donde corresponde.
    // OJO: cursor.col es un offset en BYTES dentro de la linea (asi
    // esta modelado en Document/Cursor). Para la terminal necesitamos
    // la columna VISUAL, asi que la convertimos con utf8ColumnOf en
    // vez de usar cursor.col directamente.
    int visualCol = utf8ColumnOf(doc.lineAt(cursor.line), cursor.col);
    int screenRow = cursor.line - viewport.top + 1; // +1: terminal es 1-indexada
    int screenCol = visualCol + 1;
    out << "\x1b[" << screenRow << ";" << screenCol << "H";

    out << "\x1b[?25h"; // volver a mostrar el cursor

    std::string buffer = out.str();
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}