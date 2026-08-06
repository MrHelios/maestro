#include "Renderer.h"

#include <unistd.h>
#include <sstream>

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
            std::string text = doc.lineAt(docLine);
            if (static_cast<int>(text.size()) > viewport.width) {
                text = text.substr(0, viewport.width);
            }
            out << text;
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
    std::string statusStr = status.str();
    if (static_cast<int>(statusStr.size()) > viewport.width) {
        statusStr = statusStr.substr(0, viewport.width);
    }
    out << statusStr;
    out << "\x1b[0m"; // reset de estilo

    // Posicionar el cursor real de la terminal donde corresponde.
    int screenRow = cursor.line - viewport.top + 1; // +1: terminal es 1-indexada
    int screenCol = cursor.col + 1;
    out << "\x1b[" << screenRow << ";" << screenCol << "H";

    out << "\x1b[?25h"; // volver a mostrar el cursor

    std::string buffer = out.str();
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}
