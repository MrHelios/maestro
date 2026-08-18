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

// ---- Padding de la barra de estado ----
constexpr int kStatusBarPadLeft  = 1;  // espacio inicial antes del nombre
constexpr int kStatusBarPadRight = 3;  // margen derecho: bloque (%, fila,col) no pegado al borde

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
// Piezas del bloque izquierdo de la barra fija: `content` (nombre[ - ruta])
// y `estado`, devueltos POR SEPARADO para poder colorearlos distinto en
// buildChrome (nombre en blanco, estado en negrita dorada). Respeta los
// limites fijos y, ante falta de espacio (terminal chica), sacrifica primero
// la ruta y despues el nombre. `onlyEstado` queda true cuando no hay sitio
// para nombre+ruta: se muestra solo el estado (sin separador) para no
// exceder el presupuesto.
struct BarLeft {
    std::string name;     // nombre[ [modificado]], sin estilo; vacio si onlyEstado
    std::string path;     // ruta, sin estilo; vacia si no cabe / no aplica
    std::string estado;   // etiqueta de estado, sin estilo
    bool onlyEstado;      // true => no hubo lugar para el contenido
};

BarLeft buildBarLeft(const std::string& filename, bool modified,
                     const std::string& estado, int budget) {
    if (budget <= 0) return {"", "", utf8::truncate(estado, 0), true};

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
    const std::string sep = " - ";
    // Reservamos el espacio del estado (a la derecha) y el separador
    // anterior; el resto es para nombre + ruta. Si no cabe ni el separador
    // entero (partsBudget negativo), se muestra solo el estado.
    int partsBudget = budget - estadoW - static_cast<int>(sep.size());
    if (budget <= estadoW || partsBudget < 0)
        return {"", "", utf8::truncate(estado, budget), true};

    int nameW = colCount(name);
    if (nameW >= partsBudget) {
        // El nombre consume el presupuesto entero: se trunca, sin ruta.
        name = utf8::truncate(name, partsBudget);
        return {name, "", estado, false};
    }

    // El nombre cabe; la ruta toma lo que sobra (con su separador). Si no
    // queda sitio, se omite la ruta (solo nombre + estado).
    int pathBudget = partsBudget - nameW - static_cast<int>(sep.size());
    if (path.empty() || pathBudget <= 0) return {name, "", estado, false};
    return {name, utf8TruncateFront(path, pathBudget), estado, false};
}

// Barra de estado fija (fondo gris 60%) + fila de mensajes, tal cual las
// dibuja el editor normal. Se comparte entre buildScreen y el selector de
// buffers para mantener SIEMPRE el mismo aspecto visual. El contenido es
// "  BLANCO[nombre] NEGRO[ - ruta] DORADO[ - comando] relleno  {pct}%".
// Asume que el cursor de la terminal esta al inicio de la fila de la barra.
// `totalLines` es el numero de lineas del documento: sirve para expresar la
// posicion vertical del cursor como porcentaje (0% al inicio, 100% al fin).
std::string buildChrome(const std::string& filename, bool modified,
                        const std::string& estado,
                        const std::string& statusMessage,
                        int width, const Cursor& cursor, int totalLines) {
    std::ostringstream out;
    out << "\x1b[K";
    out << kStatusBarStyle; // base: negro sobre gris 60%

    // Posicion vertical del cursor como porcentaje del archivo: 0% al
    // inicio (linea 0) y 100% al final (ultima linea). Con una sola linea
    // el inicio y el fin coinciden: se muestra 0%.
    int pct = totalLines <= 1 ? 0
                              : (cursor.line * 100) / (totalLines - 1);
    // Bloque derecho con ambos valores: la altura del cursor como
    // porcentaje y luego (fila,columna), anclado a la derecha.
    std::string rightBlock = std::to_string(pct) + "% (" +
                             std::to_string(cursor.line + 1) + "," +
                             std::to_string(cursor.col + 1) + ")";
    int rightW = colCount(rightBlock);

    int leftBudget = std::max(0, width - kStatusBarPadLeft - kStatusBarPadRight -
                                     rightW);
    BarLeft left = buildBarLeft(filename, modified, estado, leftBudget);

    // Ancho VISIBLE (sin ANSI) de todo a la izquierda del relleno, para que
    // el relleno consiga exactamente `width` columnas y el bloque derecho
    // (fila,columnapct%) quede anclado a la derecha.
    const std::string sep = " - ";
    int plainW;
    if (left.onlyEstado) {
        plainW = colCount(left.estado);
    } else {
        // "nombre[ - ruta] - estado": un separador si no hay ruta, dos si la
        // hay, y el texto de nombre + ruta + estado.
        int sepCount = left.path.empty() ? 1 : 2;
        plainW = colCount(left.name) + colCount(left.path) +
                 colCount(left.estado) +
                 sepCount * static_cast<int>(sep.size());
    }

    for (int i = 0; i < kStatusBarPadLeft; ++i) out << ' ';
    if (left.onlyEstado) {
        out << kStatusBarCommand << left.estado << kStatusBarReset;
    } else {
        out << kStatusBarName << left.name << kStatusBarReset;
        if (!left.path.empty()) {
            out << kStatusBarPath << sep << left.path << kStatusBarReset;
        }
        out << kStatusBarCommand << sep << left.estado << kStatusBarReset;
    }

    int fill = std::max(0, width - kStatusBarPadLeft - plainW -
                           kStatusBarPadRight - rightW);
    for (int i = 0; i < fill; ++i) out << ' ';
    for (int i = 0; i < kStatusBarPadRight; ++i) out << ' ';
    out << rightBlock;

    out << "\x1b[0m"; // reset de estilo

    // Fila de mensajes (sin estilo, fila propia). El padding izquierdo y
    // derecho coincide con el de la barra superior para alinear el texto.
    out << "\r\n";
    out << "\x1b[K";
    for (int i = 0; i < kStatusBarPadLeft; ++i) out << ' ';
    out << utf8::truncate(statusMessage,
                          std::max(0, width - kStatusBarPadLeft - kStatusBarPadRight));
    for (int i = 0; i < kStatusBarPadRight; ++i) out << ' ';

    return out.str();
}

// Ancho del gutter de numeros de linea (estilo vim): `d(n)+1` columnas,
// con `n` = cantidad de digitos del numero mas largo del documento, y un
// minimo de 3 (para que no este saltando de ancho con archivos chicos).
// La columna extra es el separador antes del texto.
int gutterWidth(int totalLines) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    return std::max(3, digits + 1); // +1 = separador antes del texto
}

// Celda de numero de linea: numero alineado a la derecha + un espacio de
// separacion. El numero de la fila actual deja la rama lista para el color
// real (TODO(colores)).
std::string renderGutterCell(int lineNumber1Based, int gutterW,
                             bool /*isCurrentLine*/) {
    std::string numStr = std::to_string(lineNumber1Based);
    int pad = gutterW - 1 - static_cast<int>(numStr.size());
    std::string out(std::max(0, pad), ' ');
    out += numStr;
    out += ' ';
    return out;
}

// Celda de gutter para una fila fuera del documento ("~"): en blanco,
// mismo ancho, sin numero.
std::string renderGutterBlank(int gutterW) {
    return std::string(gutterW, ' ');
}

// Devuelve los bytes de `line` cuyas COLUMNAS VISUALES caen dentro de
// [fromCol, toCol). No corta caracteres multibyte por la mitad.
// (Implementado y testado en utf8.h como utf8::range.)

// Renderiza una sola linea del documento, aplicando video inverso a los
// bytes dentro de [selStartByte, selEndByte) si la linea esta seleccionada.
// selStartByte/selEndByte -1 significa "sin seleccion en esta linea".
//
// `isCurrentLine` resalta la fila del cursor con kCurrentLineStyle: el
// resaltado cubre TODA la fila (incluido el relleno hasta `width`, no solo
// el texto), y la seleccion siempre gana sobre el (el tramo seleccionado se
// pinta en video inverso y el resto de la fila lleva el estilo de linea).
//
// `lineBreakSelected` marca el caso de una fila VACIA atravesada por la
// seleccion: su unico "contenido" es el salto de linea, y al estar
// seleccionado se pinta la fila entera en video inverso, sin ningun
// simbolo (si no, la fila quedaria en blanco y no se veria que se la
// selecciono).
void renderLine(std::ostringstream& out,
                const std::string& line,
                int width,
                bool isCurrentLine,
                int selStartByte = -1,
                int selEndByte = -1,
                bool lineBreakSelected = false) {
    // Fila vacia con su salto de linea seleccionado: la fila completa se
    // pinta en video inverso (sin simbolo) para que se vea que quedo
    // seleccionada.
    if (line.empty() && lineBreakSelected) {
        out << "\x1b[7m";
        for (int i = 0; i < width; ++i) out << ' ';
        out << "\x1b[0m";
        return;
    }

    // Sin seleccion aqui (o seleccion vacia): el texto (truncado a width).
    // Si es la fila actual, se rellena hasta `width` para que el resaltado
    // cubra toda la fila, no solo el texto.
    if (selStartByte < 0 || selEndByte < 0 || selStartByte >= selEndByte) {
        std::string text = utf8::truncate(line, width);
        if (!isCurrentLine) {
            out << text;
            return;
        }
        out << kCurrentLineStyle << text;
        for (int i = colCount(text); i < width; ++i) out << ' ';
        out << "\x1b[0m";
        return;
    }

    // Limitar la seleccion a lo que existe en la linea (si el fin cae
    // mas alla del largo de la linea, por ejemplo en la ultima).
    int endByte = std::min<int>(selEndByte, static_cast<int>(line.size()));
    int startByte = std::min<int>(selStartByte, endByte);

    int startCol = utf8::columnOf(line, startByte);
    int endCol = utf8::columnOf(line, endByte);

    // Parte antes de la seleccion, en estilo de linea actual si corresponde.
    std::string before = utf8::range(line, 0, std::min(startCol, width));
    // Parte seleccionada, en video inverso (siempre gana).
    std::string selected = utf8::range(line, std::min(startCol, width),
                                       std::min(endCol, width));
    // Parte despues de la seleccion (si queda espacio).
    std::string after = utf8::range(line, std::min(endCol, width), width);

    if (isCurrentLine) out << kCurrentLineStyle;
    out << before;
    if (isCurrentLine) out << "\x1b[0m";
    out << "\x1b[7m" << selected << "\x1b[0m";
    if (isCurrentLine) out << kCurrentLineStyle;
    out << after;
    if (isCurrentLine) {
        int used = colCount(before) + colCount(selected) + colCount(after);
        for (int i = used; i < width; ++i) out << ' ';
        out << "\x1b[0m";
    }
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

    // Gutter de numeros de linea (solo el area del documento): se resta del
    // ancho total para el texto. El resto del chrome (barra de estado,
    // selectores) sigue usando viewport.width tal cual.
    int gutterW = gutterWidth(doc.lineCount());
    int textWidth = std::max(0, viewport.width - gutterW);

    for (int row = 0; row < viewport.height; ++row) {
        int docLine = viewport.top + row;
        out << "\x1b[K"; // limpiar la linea actual

        if (docLine < doc.lineCount()) {
            const std::string& line = doc.lineAt(docLine);
            bool isCurrentLine = (docLine == cursor.line);
            int selStart = -1, selEnd = -1;
            bool lineBreakSelected = false;

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

            // Una fila VACIA dentro de la seleccion "toma" su salto de linea:
            // se marca para que se vea que quedo seleccionada. No se marca
            // cuando la seleccion es de una sola linea colapsada (nada
            // seleccionado) ni cuando termina exactamente en el inicio de esa
            // fila (su salto no quedo incluido).
            if (line.empty() && sel.has_value() && docLine >= sel->start.line &&
                docLine <= sel->end.line) {
                bool singleLine = (sel->start.line == sel->end.line);
                bool endsAtStart =
                    (docLine == sel->end.line && sel->end.col == 0);
                lineBreakSelected = !singleLine && !endsAtStart;
            }

            out << renderGutterCell(docLine + 1, gutterW, isCurrentLine);
            renderLine(out, line, textWidth, isCurrentLine, selStart, selEnd,
                       lineBreakSelected);
        } else {
            out << renderGutterBlank(gutterW);
            out << "~"; // linea fuera del documento, estilo vim
        }
        out << "\r\n";
    }

    out << buildChrome(filename, modified, stateLabel(state), statusMessage,
                       viewport.width, cursor, doc.lineCount());

    // Posicionar el cursor real de la terminal donde corresponde.
    // OJO: cursor.col es un offset en BYTES dentro de la linea (asi
    // esta modelado en Document/Cursor). Para la terminal necesitamos
    // la columna VISUAL, asi que la convertimos con utf8::columnOf en
    // vez de usar cursor.col directamente.
    int visualCol = utf8::columnOf(doc.lineAt(cursor.line), cursor.col);
    int screenRow = cursor.line - viewport.top + 1; // +1: terminal es 1-indexada
    // +gutterW: la columna visual 0 del texto hoy no es la columna 1 de la
    // terminal, sino la primera tras el gutter de numeros de linea.
    int screenCol = gutterW + visualCol + 1;
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

    // Fila de mensajes (height+2). Mismo padding izquierdo/derecho que la
    // barra de estado para alinear el texto.
    out << "\r\n";
    out << "\x1b[K";
    for (int i = 0; i < kStatusBarPadLeft; ++i) out << ' ';
    out << utf8::truncate(statusMessage,
                          std::max(0, width - kStatusBarPadLeft - kStatusBarPadRight));
    for (int i = 0; i < kStatusBarPadRight; ++i) out << ' ';

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