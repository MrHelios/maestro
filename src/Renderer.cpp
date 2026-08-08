#include "Renderer.h"

// Estado de la maquina de estados (Editor.h). Renderer solo lo usa como
// etiqueta visual, nunca lo modifica.
#include "Editor.h"

#include <unistd.h>
#include <sstream>
#include <algorithm>

namespace {

// ---- Limites fijos de la barra de estado (bloque izquierdo) ----
constexpr int kNameMax    = 30;   // columnas maximas del nombre
constexpr int kPathMax    = 40;   // columnas maximas de la ruta
constexpr int kNamePathMax = 60;  // tope combinado nombre + ruta

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

// Ancho (columnas visuales) de una cadena completa.
std::string utf8Range(const std::string& line, int fromCol, int toCol);

std::string utf8Tail(const std::string& line, int maxTailCols) {
    int total = utf8ColumnOf(line, static_cast<int>(line.size()));
    if (maxTailCols <= 0 || total <= maxTailCols) return line;
    return utf8Range(line, total - maxTailCols, total);
}

// Trunca manteniendo el INICIO (los primeros `maxCols` visibles).
// No corta caracteres multibyte por la mitad.
std::string utf8TruncateFront(const std::string& line, int maxCols) {
    if (maxCols <= 0) return line;
    if (utf8ColumnOf(line, static_cast<int>(line.size())) <= maxCols) return line;
    const std::string ellipsis = "...";
    if (maxCols <= static_cast<int>(ellipsis.size()))
        return utf8Truncate(ellipsis, maxCols);
    return ellipsis + utf8Tail(line, maxCols - static_cast<int>(ellipsis.size()));
}

int colCount(const std::string& s) {
    return utf8ColumnOf(s, static_cast<int>(s.size()));
}

// Nombre del archivo (la parte tras el ultimo '/' ; o el mismo si no
// tiene directorio).
std::string baseName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// Directorio del archivo (la parte antes del ultimo '/').
std::string dirName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

// Etiqueta de estado, mapeada 1 a 1 con State.
std::string stateLabel(State state) {
    switch (state) {
        case State::Select: return "SELECCION";
        case State::Prefix: return "COMANDO";
        case State::Normal: default: return "NORMAL";
    }
}

// Une `name SEP path` dentro de `budget` columnas, respetando la
// prioridad de sacrificio: la ruta se agota primero (truncada por la
// IZQUIERDA, con "..." al inicio) y el nombre se toca solo como ultimo
// recurso. Para eso se RESERVA el nombre (fijo, sin truncarlo si se
// puede evitar), se resta del presupuesto y el resto entero se da a la
// ruta. Devuelve la parte que cabe del bloque (sin la etiqueta de
// estado).
std::string buildLeftParts(int budget, const std::string& name,
                           const std::string& path) {
    if (budget <= 0) return "";

    int nameW = colCount(name);
    // Si el nombre ya ocupa el presupuesto entero (o mas), se trunca el
    // nombre: no queda lugar para la ruta ni el separador.
    if (nameW >= budget) return utf8Truncate(name, budget);

    const std::string sep = " - ";
    int sepW = static_cast<int>(sep.size());

    // Resto del presupuesto para la ruta (separiendo tambien el separador).
    int pathBudget = budget - nameW - sepW;

    std::string out = name;
    if (!path.empty() && pathBudget > 0) {
        out += sep;
        out += utf8TruncateFront(path, pathBudget);
    }
    return out;
}

// Construye el bloque izquierdo de la barra fija:
//   `name SEP path SEP estado`
// respetando los limites fijos y, ante falta de espacio (terminal
// chica), sacrificando primero la ruta y despues el nombre. La etiqueta
// de estado y el bloque Ln/Col nunca se truncan.
std::string buildLeftBlock(const std::string& filename, bool modified,
                           const std::string& estado, int budget) {
    if (budget <= 0) return "";

    std::string name = baseName(filename);
    if (name.empty()) name = "[sin nombre]";
    if (modified) name += " [modificado]";

    std::string path = filename.empty() ? "" : dirName(filename);

    // Limites fijos (columnas visuales).
    name = utf8Truncate(name, kNameMax);
    // La ruta se acorta por la izquierda: se pierde el inicio cuando
    // excede, manteniendo la cola (donde esta el nombre de archivo).
    if (colCount(path) > kPathMax)
        path = utf8TruncateFront(path, kPathMax);
    if (colCount(name) + colCount(path) > kNamePathMax)
        path = utf8TruncateFront(path, std::max(0, kNamePathMax - colCount(name)));

    int estadoW = colCount(estado);
    if (budget <= estadoW) return utf8Truncate(estado, budget);

    // Reservamos el espacio del estado (a la derecha) y el separador
    // anterior; el resto es para nombre + ruta.
    const std::string sep = " - ";
    int partsBudget = budget - estadoW - static_cast<int>(sep.size());
    std::string izquierda = buildLeftParts(partsBudget, name, path);
    return izquierda + sep + estado;
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta caracteres multibyte por la mitad.
std::string utf8Range(const std::string& line, int fromCol, int toCol) {
    int col = 0;
    size_t startByte = line.size();
    size_t i = 0;
    while (i < line.size()) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if ((c & 0xC0) != 0x80) {
            if (col == fromCol) startByte = i;
            if (col >= toCol) break;
            col++;
        }
        i++;
    }
    if (col >= toCol) return line.substr(startByte, i - startByte);
    return line.substr(startByte); // hasta el final de la linea
}

// Renderiza una sola linea del documento, aplicando video inverso a los
// bytes dentro de [selStartByte, selEndByte) si la linea esta seleccionada.
// selStartByte/selEndByte -1 significa "sin seleccion en esta linea".
void renderLine(std::ostringstream& out,
                const std::string& line,
                int width,
                int selStartByte = -1,
                int selEndByte = -1) {
    if (selStartByte < 0 || selEndByte < 0 || selStartByte >= selEndByte) {
        // Sin seleccion aqui (o seleccion vacia): caso normal.
        out << utf8Truncate(line, width);
        return;
    }

    // Limitar la seleccion a lo que existe en la linea (si el fin cae
    // mas alla del largo de la linea, por ejemplo en la ultima).
    int endByte = std::min<int>(selEndByte, static_cast<int>(line.size()));
    int startByte = std::min<int>(selStartByte, endByte);

    int startCol = utf8ColumnOf(line, startByte);
    int endCol = utf8ColumnOf(line, endByte);

    // Parte antes de la seleccion.
    out << utf8Range(line, 0, std::min(startCol, width));

    // Parte seleccionada, en video inverso.
    out << "\x1b[7m";
    out << utf8Range(line, std::min(startCol, width), std::min(endCol, width));
    out << "\x1b[0m";

    // Parte despues de la seleccion (si queda espacio).
    out << utf8Range(line, std::min(endCol, width), width);
}

} // namespace

std::string Renderer::buildScreen(const Document& doc,
                                   const Cursor& cursor,
                                   const Viewport& viewport,
                                   const std::string& filename,
                                   bool modified,
                                   const std::string& statusMessage,
                                   State state,
                                   const std::optional<Selection>& selection) {
    // Armamos todo en un unico buffer y lo escribimos de una sola vez
    // para evitar parpadeo.
    std::ostringstream out;

    // Si hay seleccion, la normalizamos una vez para conocer los limites.
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection)
                                                          : std::nullopt;

    out << "\x1b[?25l"; // ocultar cursor mientras dibujamos
    out << "\x1b[H";    // mover cursor a home (fila 1, col 1)

    for (int row = 0; row < viewport.height; ++row) {
        int docLine = viewport.top + row;
        out << "\x1b[K"; // limpiar la linea actual

        if (docLine < doc.lineCount()) {
            const std::string& line = doc.lineAt(docLine);
            int selStart = -1, selEnd = -1;

            if (sel.has_value() && docLine >= sel->start.line && docLine <= sel->end.line) {
                if (sel->start.line == sel->end.line) {
                    // Seleccion en una sola linea.
                    selStart = sel->start.col;
                    selEnd = sel->end.col;
                } else if (docLine == sel->start.line) {
                    // Primera linea: desde la seleccion hasta el final.
                    selStart = sel->start.col;
                    selEnd = static_cast<int>(line.size());
                } else if (docLine == sel->end.line) {
                    // Ultima linea: hasta la seleccion.
                    selStart = 0;
                    selEnd = sel->end.col;
                } else {
                    // Linea intermedia: toda la linea.
                    selStart = 0;
                    selEnd = static_cast<int>(line.size());
                }
            }

            renderLine(out, line, viewport.width, selStart, selEnd);
        } else {
            out << "~"; // linea fuera del documento, estilo vim
        }
        out << "\r\n";
    }

// ---- Fila 1: barra de estado "fija" (video inverso), ancho total ----
    //
    // Bloque derecho: Ln/Col, siempre visible y anclado a la derecha,
    // nunca se trunca. Bloque izquierdo: nombre - ruta - ESTADO, con los
    // limites fijos y, ante terminal chica, sacrificando primero la ruta.
    out << "\x1b[K";
    out << "\x1b[7m"; // video inverso

    std::string rightBlock = "Linea: " + std::to_string(cursor.line + 1) +
                             " Col: " + std::to_string(cursor.col + 1);
    int rightW = colCount(rightBlock);

    int leftBudget = std::max(0, viewport.width - rightW - 1);
    std::string leftBlock = buildLeftBlock(filename, modified,
                                           stateLabel(state), leftBudget);

    out << leftBlock;
    int fill = viewport.width - colCount(leftBlock) - rightW;
    for (int i = 0; i < fill; ++i) out << ' ';
    out << rightBlock;

    out << "\x1b[0m"; // reset de estilo

    // ---- Fila 2: mensajes de estado (sin inverso, fila propia) ----
    out << "\r\n";
    out << "\x1b[K";
    out << utf8Truncate(statusMessage, viewport.width);

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

    return out.str();
}

void Renderer::render(const Document& doc,
                       const Cursor& cursor,
                       const Viewport& viewport,
                       const std::string& filename,
                       bool modified,
                       const std::string& statusMessage,
                       State state,
                       const std::optional<Selection>& selection) {
    std::string buffer = buildScreen(doc, cursor, viewport, filename,
                                     modified, statusMessage, state, selection);
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}