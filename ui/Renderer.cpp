#include "ui/Renderer.h"

#include <unistd.h>
#include <sstream>
#include <algorithm>

#include "core/utf8.h"

namespace {

// ---- Limites fijos de la barra de estado (bloque izquierdo) ----
constexpr int kNameMax    = 30;   // columnas maximas del nombre
constexpr int kPathMax    = 40;   // columnas maximas de la ruta
constexpr int kNamePathMax = 60;  // tope combinado nombre + ruta

std::string utf8Tail(const std::string& line, int maxTailCols) {
    int total = utf8::columnOf(line, static_cast<int>(line.size()));
    if (maxTailCols <= 0 || total <= maxTailCols) return line;
    return utf8::range(line, total - maxTailCols, total);
}

// Trunca manteniendo el INICIO (los primeros `maxCols` visibles).
// No corta caracteres multibyte por la mitad.
std::string utf8TruncateFront(const std::string& line, int maxCols) {
    if (maxCols <= 0) return line;
    if (utf8::columnOf(line, static_cast<int>(line.size())) <= maxCols) return line;
    const std::string ellipsis = "...";
    if (maxCols <= static_cast<int>(ellipsis.size()))
        return utf8::truncate(ellipsis, maxCols);
    return ellipsis + utf8Tail(line, maxCols - static_cast<int>(ellipsis.size()));
}

int colCount(const std::string& s) {
    return utf8::columnOf(s, static_cast<int>(s.size()));
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
        case State::Navegacion: return "NAVEGACION";
        case State::Interaccion: return "INTERACCION";
        case State::Seleccion: return "SELECCION";
        case State::Prefix: return "COMANDO";
        case State::BufferSelector: return "BUFFERS";
        case State::SaveAs: return "GUARDAR";
        case State::FileBrowser: return "ABRIR";
    }
    return "";
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
    if (nameW >= budget) return utf8::truncate(name, budget);

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
    const std::string modificado = " [modificado]";

    std::string path = filename.empty() ? "" : dirName(filename);
    // v0.6.3: un buffer sin nombre se muestra con su nombre generico
    // ("SinNombre", ...) que no tiene directorio; no tiene sentido
    // mostrar "." como ruta.
    if (path == ".") path = "";

    // Limites fijos (columnas visuales). El sufijo [modificado] se
    // RESERVA entero: se trunca el nombre (no el indicador) para que
    // jamás se pierda la señal de "cambios sin guardar" en pantalla.
    int nameBudget = kNameMax - (modified ? colCount(modificado) : 0);
    name = utf8::truncate(name, nameBudget);
    if (modified) name += modificado;
    // La ruta se acorta por la izquierda: se pierde el inicio cuando
    // excede, manteniendo la cola (donde esta el nombre de archivo).
    if (colCount(path) > kPathMax)
        path = utf8TruncateFront(path, kPathMax);
    if (colCount(name) + colCount(path) > kNamePathMax)
        path = utf8TruncateFront(path, std::max(0, kNamePathMax - colCount(name)));

    int estadoW = colCount(estado);
    if (budget <= estadoW) return utf8::truncate(estado, budget);

    // Reservamos el espacio del estado (a la derecha) y el separador
    // anterior; el resto es para nombre + ruta. Si aun no cabe el
    // separador entero (partsBudget negativo), no hay lugar para ningun
    // nombre: mostramos solo el estado, sin usar el separador, para no
    // exceder `budget` (y de paso viewport.width).
    const std::string sep = " - ";
    int partsBudget = budget - estadoW - static_cast<int>(sep.size());
    if (partsBudget < 0) return utf8::truncate(estado, budget);
    std::string izquierda = buildLeftParts(partsBudget, name, path);
    return izquierda + sep + estado;
}

// Barra de estado fija (fila en video inverso) + fila de mensajes, tal
// cual las dibuja el editor normal. Se comparte entre buildScreen y el
// selector de buffers para mantener SIEMPRE el mismo aspecto visual: el
// bloque izquierdo (nombre - ruta - ESTADO), el bloque derecho anclado
// a la derecha (Linea/Col), y abajo la fila de mensajes. Asume que el
// cursor de la terminal esta al inicio de la fila de la barra.
std::string buildChrome(const std::string& filename, bool modified,
                        const std::string& estado,
                        const std::string& statusMessage,
                        int width, const Cursor& cursor) {
    std::ostringstream out;
    out << "\x1b[K";
    out << "\x1b[7m"; // video inverso

    std::string rightBlock = "Linea: " + std::to_string(cursor.line + 1) +
                             " Col: " + std::to_string(cursor.col + 1);
    int rightW = colCount(rightBlock);

    int leftBudget = std::max(0, width - rightW - 1);
    std::string leftBlock = buildLeftBlock(filename, modified, estado,
                                           leftBudget);

    out << leftBlock;
    int fill = width - colCount(leftBlock) - rightW;
    for (int i = 0; i < fill; ++i) out << ' ';
    out << rightBlock;

    out << "\x1b[0m"; // reset de estilo

    // Fila de mensajes (sin inverso, fila propia).
    out << "\r\n";
    out << "\x1b[K";
    out << utf8::truncate(statusMessage, width);

    return out.str();
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta caracteres multibyte por la mitad.
// (Implementado y testado en utf8.h como utf8::range.)

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
        out << utf8::truncate(line, width);
        return;
    }

    // Limitar la seleccion a lo que existe en la linea (si el fin cae
    // mas alla del largo de la linea, por ejemplo en la ultima).
    int endByte = std::min<int>(selEndByte, static_cast<int>(line.size()));
    int startByte = std::min<int>(selStartByte, endByte);

    int startCol = utf8::columnOf(line, startByte);
    int endCol = utf8::columnOf(line, endByte);

    // Parte antes de la seleccion.
    out << utf8::range(line, 0, std::min(startCol, width));

    // Parte seleccionada, en video inverso.
    out << "\x1b[7m";
    out << utf8::range(line, std::min(startCol, width), std::min(endCol, width));
    out << "\x1b[0m";

    // Parte despues de la seleccion (si queda espacio).
    out << utf8::range(line, std::min(endCol, width), width);
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

    out << buildChrome(filename, modified, stateLabel(state), statusMessage,
                       viewport.width, cursor);

    // Posicionar el cursor real de la terminal donde corresponde.
    // OJO: cursor.col es un offset en BYTES dentro de la linea (asi
    // esta modelado en Document/Cursor). Para la terminal necesitamos
    // la columna VISUAL, asi que la convertimos con utf8::columnOf en
    // vez de usar cursor.col directamente.
    int visualCol = utf8::columnOf(doc.lineAt(cursor.line), cursor.col);
    int screenRow = cursor.line - viewport.top + 1; // +1: terminal es 1-indexada
    int screenCol = visualCol + 1;
    out << "\x1b[" << screenRow << ";" << screenCol << "H";

    out << "\x1b[?25h"; // volver a mostrar el cursor

    return out.str();
}

void Renderer::renderScreen(const Document& doc,
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

// v0.6.3: pantalla del selector de buffers. Mantiene el aspecto del editor
// normal: el area principal (filas 1..height) muestra la lista de buffers
// (seleccionado en video inverso) y las filas vacias su marcador "~". En la
// fila de la barra de estado (height+1) se dibuja una barra en video inverso
// que solo dice MULTIBUFFER, y la fila de mensajes (height+2) se limpia: en
// el selector no se muestra ruta, ni Linea/Col, ni mensajes.
std::string Renderer::buildBufferListScreen(const std::vector<std::string>& names,
                                            int selected,
                                            int width,
                                            int height) {
    std::ostringstream out;

    out << "\x1b[?25l";   // ocultar cursor mientras dibujamos
    out << "\x1b[H";      // mover a home
    out << "\x1b[J";      // limpiar todo lo que quede debajo (incl. la
                          // barra de estado y los mensajes de la pantalla
                          // anterior)

    int rows = 0;
    for (size_t i = 0; i < names.size() && rows < height; ++i, ++rows) {
        out << "\x1b[K";
        std::string line = "  " + names[i];
        if (static_cast<int>(i) == selected) {
            out << "\x1b[7m" << utf8::truncate(line, width) << "\x1b[0m";
        } else {
            out << utf8::truncate(line, width);
        }
        out << "\r\n";
    }
    // Filas vacias: mismo marcador de linea fuera del documento que el editor.
    for (int r = rows; r < height; ++r) {
        out << "\x1b[K";
        out << "~";
        out << "\r\n";
    }

    // Fila de la barra de estado (height+1): solo el modo, en video inverso.
    out << "\x1b[K";
    out << "\x1b[7m";
    const std::string label = "MULTIBUFFER";
    out << label;
    for (int i = static_cast<int>(label.size()); i < width; ++i) out << ' ';
    out << "\x1b[0m";

    // Fila de mensajes (height+2): en el selector no hay, se limpia.
    out << "\r\n";
    out << "\x1b[K";

    // Cursor real de la terminal sobre la fila seleccionada de la lista.
    // Se clampa a las filas ya dibujadas para no invadir la barra final.
    int cursorRow = std::max(1, std::min(selected + 1, rows));
    out << "\x1b[" << cursorRow << ";1H";

    out << "\x1b[?25h"; // volver a mostrar el cursor
    return out.str();
}

void Renderer::renderBufferList(const std::vector<std::string>& names,
                                int selected,
                                int width,
                                int height) {
    std::string buffer = buildBufferListScreen(names, selected, width, height);
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}

// v0.6.4: pantalla del explorador de archivos. Mismo aspecto que el
// selector de buffers (lista en video inverso + '~' en filas vacias), pero
// la barra de estado muestra la ruta actual (`path`) con la etiqueta
// ABRIR ARCHIVO a la derecha, y la fila de mensajes lleva la ayuda.
std::string Renderer::buildFileListScreen(
        const std::vector<std::string>& names,
        int selected,
        int scroll,
        const std::string& path,
        const std::string& statusMessage,
        int width,
        int height) {
    std::ostringstream out;

    out << "\x1b[?25l";   // ocultar cursor mientras dibujamos
    out << "\x1b[H";      // mover a home
    out << "\x1b[J";      // limpiar el resto

    // Lista con ventana (scroll): cada fila es names[scroll + row].
    int rows = 0;
    for (int row = 0; row < height; ++row, ++rows) {
        int idx = scroll + row;
        out << "\x1b[K";
        if (idx < static_cast<int>(names.size())) {
            std::string line = "  " + names[static_cast<size_t>(idx)];
            if (idx == selected) {
                out << "\x1b[7m" << utf8::truncate(line, width) << "\x1b[0m";
            } else {
                out << utf8::truncate(line, width);
            }
        } else {
            out << "~";
        }
        out << "\r\n";
    }

    // Barra de estado (fila height+1): ruta a la izquierda, modo a la derecha.
    out << "\x1b[K";
    out << "\x1b[7m";
    const std::string right = "ABRIR ARCHIVO";
    int rightW = colCount(right);
    int leftBudget = std::max(0, width - rightW - 1);
    std::string left = path.empty() ? "/" : path;
    if (colCount(left) > leftBudget) left = utf8TruncateFront(left, leftBudget);
    out << left;
    int fill = width - colCount(left) - rightW;
    for (int i = 0; i < fill; ++i) out << ' ';
    out << right;
    out << "\x1b[0m";

    // Fila de mensajes (height+2).
    out << "\r\n";
    out << "\x1b[K";
    out << utf8::truncate(statusMessage, width);

    // Cursor real sobre la fila seleccionada de la lista, clampeado a las
    // filas dibujadas para no invadir la barra de estado.
    int cursorRow = std::max(1, std::min(selected - scroll + 1, rows));
    out << "\x1b[" << cursorRow << ";1H";

    out << "\x1b[?25h";
    return out.str();
}

void Renderer::renderFileList(const std::vector<std::string>& names,
                              int selected,
                              int scroll,
                              const std::string& path,
                              const std::string& statusMessage,
                              int width,
                              int height) {
    std::string buffer = buildFileListScreen(names, selected, scroll, path,
                                             statusMessage, width, height);
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}