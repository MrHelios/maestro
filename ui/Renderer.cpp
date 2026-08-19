#include "ui/Renderer.h"

#include <unistd.h>
#include <algorithm>

#include "core/utf8.h"
#include "ui/RenderUtil.h"

namespace {

// Helpers de texto/UTF-8 compartidos con el StatusBar (definidos en
// ui/RenderUtil.h como chrome::*). La barra de estado propia (barra fija +
// fila de mensajes) vive en ui/StatusBar.cpp; aqui solo queda lo que
// pertenece al contenido (gutter, numeros, seleccion).
using namespace chrome;

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

// Arma el StatusBarData del Editor a partir de filename/modified/estado/
// statusMessage/cursor/totalLines. La barra comun no conoce nada de esto;
// solo recibe los datos ya traducidos. (El selector y el explorador arman
// un StatusBarData propio: Buffers/SELECCIONAR/N-total y ruta/ABRIR/N-M.)
StatusBarData editorBarData(const std::string& filename, bool modified,
                            const std::string& estado,
                            const Message& message,
                            const Cursor& cursor, int totalLines) {
    StatusBarData data;
    data.name = baseName(filename);
    if (data.name.empty()) data.name = "[sin nombre]";
    data.path = dirName(filename);
    // v0.6.3: un buffer sin nombre se muestra con su nombre generico
    // ("SinNombre", ...) que no tiene directorio; no tiene sentido mostrar
    // "." como ruta.
    if (data.path == ".") data.path = "";
    data.estado = estado;
    data.modified = modified;
    data.message = message;
    data.cursorLine = cursor.line;
    data.cursorCol = cursor.col;
    data.totalLines = totalLines;
    return data;
}

// La barra de estado fija + fila de mensajes (buildChrome, buildBarLeft,
// BarLeft, y las constantes de la barra) se movio a ui/StatusBar.cpp: el
// Renderer ya no dibuja la barra comun, solo calcula su Layout y arma el
// StatusBarData.

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
//
// Cota de ancho (v1.1, regresion): la celda NUNCA supera `gutterW` columnas,
// aun en una terminal ultra-chica donde el numero es mas ancho que el propio
// gutter (buildScreen recorta gutterW al ancho disponible). Se conserva la
// COLA del numero (se pierde el inicio), igual que se trunca la ruta en la
// barra de estado.
std::string renderGutterCell(int lineNumber1Based, int gutterW,
                             bool /*isCurrentLine*/) {
    std::string numStr = std::to_string(lineNumber1Based);
    const int maxNumCols = std::max(0, gutterW - 1); // 1 columna es el separador
    if (static_cast<int>(numStr.size()) > maxNumCols)
        numStr = numStr.substr(numStr.size() - static_cast<size_t>(maxNumCols));
    const int pad = std::max(0, gutterW - 1 - static_cast<int>(numStr.size()));
    std::string out(pad, ' ');
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
void renderLine(std::string& out,
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
        out += "\x1b[7m";
        for (int i = 0; i < width; ++i) out += ' ';
        out += "\x1b[0m";
        return;
    }

    // Sin seleccion aqui (o seleccion vacia): el texto (truncado a width).
    // Si es la fila actual, se rellena hasta `width` para que el resaltado
    // cubra toda la fila, no solo el texto.
    if (selStartByte < 0 || selEndByte < 0 || selStartByte >= selEndByte) {
        std::string text = utf8::truncate(line, width);
        if (!isCurrentLine) {
            out += text;
            return;
        }
        out += kCurrentLineStyle;
        out += text;
        for (int i = colCount(text); i < width; ++i) out += ' ';
        out += "\x1b[0m";
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

    if (isCurrentLine) out += kCurrentLineStyle;
    out += before;
    if (isCurrentLine) out += "\x1b[0m";
    out += "\x1b[7m";
    out += selected;
    out += "\x1b[0m";
    if (isCurrentLine) out += kCurrentLineStyle;
    out += after;
    if (isCurrentLine) {
        int used = colCount(before) + colCount(selected) + colCount(after);
        for (int i = used; i < width; ++i) out += ' ';
        out += "\x1b[0m";
    }
}

} // namespace

std::string Renderer::buildScreen(const Document& doc,
                                   const Cursor& cursor,
                                   const Viewport& viewport,
                                   const std::string& filename,
                                   bool modified,
                                   const Message& message,
                                   State state,
                                   const std::optional<Selection>& selection) {
    // Armamos todo en un unico string y lo escribimos de una sola vez
    // para evitar parpadeo.
    std::string out;

    // Si hay seleccion, la normalizamos una vez para conocer los limites.
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection)
                                                          : std::nullopt;

    out += "\x1b[?25l"; // ocultar cursor mientras dibujamos
    out += "\x1b[H";    // mover cursor a home (fila 1, col 1)

    // El Renderer calcula el Layout UNA vez: el contenido arriba y la barra
    // comun (fila fija + mensajes) en las 2 filas finales. Ninguna pantalla
    // vuelve a decidir donde termina el contenido.
    Layout layout = calculateLayout(viewport.height, viewport.width);

    // Gutter de numeros de linea (solo el area del documento): se resta del
    // ancho total para el texto. Solo lo usa el contenido; el StatusBar
    // trabaja sobre viewport.width tal cual.
    //
    // Cota de ancho (v1.1, regresion): en una terminal ultra-chica el gutter
    // RARO se recorta al ancho disponible para que ninguna fila del contenido
    // escriba fuera de la terminal (renderGutterCell/Blank ya no la exceden).
    int gutterW = std::min(gutterWidth(doc.lineCount()), viewport.width);

    renderEditorContent(out, doc, cursor, viewport, sel, layout.content,
                        gutterW);
    renderStatusBar(out, layout.statusBar,
                    editorBarData(filename, modified, stateLabel(state),
                                  message, cursor, doc.lineCount()));

    // Posicionar el cursor real de la terminal donde corresponde (v1.0,
    // paso 9: un unico lugar decide la coordenada terminal del cursor).
    // La cadena completo es:
    //   Document cursor.col (BYTES)
    //     -> utf8::columnOf -> columna VISUAL
    //     -> + gutterW      -> + gutter de numeros
    //     -> + layout.content.col + 1 -> + origen del contenido + 1 (1-indexada)
    int visualCol = utf8::columnOf(doc.lineAt(cursor.line), cursor.col);
    int screenRow = cursor.line - viewport.top + 1; // +1: terminal es 1-indexada
    int screenCol = gutterW + visualCol + 1 + layout.content.col;
    moveCursorTo(out, screenRow, screenCol);

    out += "\x1b[?25h"; // volver a mostrar el cursor

    return out;
}

// Posiciona el cursor real de la terminal en la fila/columna 1-indexadas.
// Es el unico lugar del Renderer que emite "\x1b[{r};{c}H" junto con los
// selectores; centraliza la conversion fila/columna -> secuencia ANSI.
void Renderer::moveCursorTo(std::string& out, int row, int col) const {
    out += "\x1b[";
    out += std::to_string(row);
    out += ";";
    out += std::to_string(col);
    out += "H";
}

Layout Renderer::calculateLayout(int contentRows, int width) const {
    // Reconstruye la geometria completa a partir de la altura de contenido
    // que trae el viewport. La unica fuente de la geometria es
    // computeLayout() (ui/Layout.h): si cambia la cantidad de filas del
    // chrome (kStatusBarRows), alcanza con tocar ese archivo.
    return computeLayout(contentRows + kStatusBarRows, width);
}

void Renderer::renderEditorContent(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const Rect& area,
                             int gutterW) const {
    int textWidth = std::max(0, area.width - gutterW);

    for (int row = 0; row < area.height; ++row) {
        int docLine = viewport.top + row;
        out += "\x1b[K"; // limpiar la linea actual

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

            out += renderGutterCell(docLine + 1, gutterW, isCurrentLine);
            renderLine(out, line, textWidth, isCurrentLine, selStart, selEnd,
                       lineBreakSelected);
        } else {
            out += renderGutterBlank(gutterW);
            out += "~"; // linea fuera del documento, estilo vim
        }
        out += "\r\n";
    }
}

void Renderer::renderStatusBar(std::string& out,
                               const Rect& area,
                               const StatusBarData& data) const {
    // La barra NO conoce editor, buffer ni documento: la pantalla produce
    // un StatusBarData y la barra comun (StatusBar) solo lo pinta.
    StatusBar bar;
    out += bar.render(area, data);
}

void Renderer::renderScreen(const Document& doc,
                            const Cursor& cursor,
                            const Viewport& viewport,
                            const std::string& filename,
                            bool modified,
                            const Message& message,
                            State state,
                            const std::optional<Selection>& selection) {
    std::string buffer = buildScreen(doc, cursor, viewport, filename,
                                     modified, message, state, selection);
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}

// v0.6.3: pantalla del selector de buffers. Mantiene el aspecto del editor
// normal: el area de contenido (`height` filas) muestra la lista de buffers
// (seleccionado en video inverso) y las filas vacias su marcador "~". La
// barra ya NO existe aqui: el selector produce datos (Buffers / SELECCIONAR
// / n-total) y se los entrega al StatusBar comun, igual que el editor.
std::string Renderer::buildBufferListScreen(const std::vector<std::string>& names,
                                            int selected,
                                            int width,
                                            int height) {
    std::string out;

    out += "\x1b[?25l";   // ocultar cursor mientras dibujamos
    out += "\x1b[H";      // mover a home
    out += "\x1b[J";      // limpiar todo lo que quede debajo (incl. la
                          // barra de estado y los mensajes de la pantalla
                          // anterior)

    // El Renderer calcula el Layout y delega: el selector solo dibuja su
    // contenido; la barra la dibuja el StatusBar comun.
    Layout layout = calculateLayout(height, width);
    renderBufferListContent(out, names, selected, layout.content);

    // Datos de la barra (paso 6): Buffers | SELECCIONAR | n/total.
    StatusBarData data;
    data.name = "Buffers";
    data.estado = "SELECCIONAR";
    const int total = static_cast<int>(names.size());
    data.right = std::to_string(std::min(selected + 1, total)) + "/" +
                 std::to_string(total);
    renderStatusBar(out, layout.statusBar, data);

    // Cursor real de la terminal sobre la fila seleccionada de la lista.
    // Se clampa a las filas ya dibujadas para no invadir la barra final.
    int rows = std::min(static_cast<int>(names.size()), height);
    int cursorRow = std::max(1, std::min(selected + 1, rows));
    moveCursorTo(out, cursorRow, 1);

    out += "\x1b[?25h"; // volver a mostrar el cursor
    return out;
}

void Renderer::renderBufferListContent(std::string& out,
                                       const std::vector<std::string>& names,
                                       int selected,
                                       const Rect& area) const {
    int rows = 0;
    for (size_t i = 0; i < names.size() && rows < area.height; ++i, ++rows) {
        out += "\x1b[K";
        std::string line = "  " + names[i];
        if (static_cast<int>(i) == selected) {
            out += "\x1b[7m";
            out += utf8::truncate(line, area.width);
            out += "\x1b[0m";
        } else {
            out += utf8::truncate(line, area.width);
        }
        out += "\r\n";
    }
    // Filas vacias: mismo marcador de linea fuera del documento que el editor.
    for (int r = rows; r < area.height; ++r) {
        out += "\x1b[K";
        out += "~";
        out += "\r\n";
    }
}

void Renderer::renderBufferList(const std::vector<std::string>& names,
                                int selected,
                                int width,
                                int height) {
    std::string buffer = buildBufferListScreen(names, selected, width, height);
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}

// v0.6.4: pantalla del explorador de archivos. Mismo contenido que el
// selector de buffers (lista con ventana en video inverso + '~' en filas
// vacias). La barra ya NO existe aqui: el explorador produce datos (ruta /
// ABRIR ARCHIVO / n-m) y se los entrega al StatusBar comun; la fila de
// mensajes lleva la ayuda de navegacion.
std::string Renderer::buildFileListScreen(
        const std::vector<std::string>& names,
        int selected,
        int scroll,
        const std::string& path,
        const Message& message,
        int width,
        int height) {
    std::string out;

    out += "\x1b[?25l";   // ocultar cursor mientras dibujamos
    out += "\x1b[H";      // mover a home
    out += "\x1b[J";      // limpiar el resto

    Layout layout = calculateLayout(height, width);
    renderFileListContent(out, names, selected, scroll, layout.content);

    // Datos de la barra (paso 7): ruta | ABRIR ARCHIVO | n/m + ayuda.
    StatusBarData data;
    data.name = path.empty() ? "/" : path;
    data.estado = "ABRIR ARCHIVO";
    const int total = static_cast<int>(names.size());
    data.right = std::to_string(std::min(selected - scroll + 1, total)) + "/" +
                 std::to_string(total);
    data.message = message;
    renderStatusBar(out, layout.statusBar, data);

    // Cursor real sobre la fila seleccionada de la lista, clampeado a las
    // filas dibujadas para no invadir la barra de estado.
    int rows = std::min(static_cast<int>(names.size() - std::min(scroll, static_cast<int>(names.size()))),
                        height);
    int cursorRow = std::max(1, std::min(selected - scroll + 1, rows));
    moveCursorTo(out, cursorRow, 1);

    out += "\x1b[?25h";
    return out;
}

void Renderer::renderFileListContent(std::string& out,
                                     const std::vector<std::string>& names,
                                     int selected,
                                     int scroll,
                                     const Rect& area) const {
    int rows = 0;
    for (int row = 0; row < area.height; ++row, ++rows) {
        int idx = scroll + row;
        out += "\x1b[K";
        if (idx < static_cast<int>(names.size())) {
            std::string line = "  " + names[static_cast<size_t>(idx)];
            if (idx == selected) {
                out += "\x1b[7m";
                out += utf8::truncate(line, area.width);
                out += "\x1b[0m";
            } else {
                out += utf8::truncate(line, area.width);
            }
        } else {
            out += "~";
        }
        out += "\r\n";
    }
}

void Renderer::renderFileList(const std::vector<std::string>& names,
                              int selected,
                              int scroll,
                              const std::string& path,
                              const Message& message,
                              int width,
                              int height) {
    std::string buffer = buildFileListScreen(names, selected, scroll,
                                             path, message, width, height);
    write(STDOUT_FILENO, buffer.c_str(), buffer.size());
}