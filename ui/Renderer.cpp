#include "ui/Renderer.h"

#include <cassert>
#include <cerrno>
#include <cstdlib>
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
        case State::Busqueda: return "BUSQUEDA";
    }
    return "";
}

// Accent de la etiqueta de estado segun el modo activo (v1.3): cada estado
// de la maquina tiene su propio color en el Theme. `estadoAccent` se pasa
// en el StatusBarData y la barra lo usa en vez del fallback statusBarAccent.
std::string stateAccent(const Theme& T, State state) {
    switch (state) {
        case State::Navegacion:    return T.accentNavegacion;
        case State::Interaccion:   return T.accentInteraccion;
        case State::Seleccion:     return T.accentSeleccion;
        case State::Prefix:        return T.accentComando;
        case State::BufferSelector: return T.accentBuffers;
        case State::SaveAs:        return T.accentGuardar;
        case State::FileBrowser:   return T.accentAbrir;
        case State::Busqueda:      return T.accentGuardar;
    }
    return T.statusBarAccent;
}

// Solo para MOSTRAR en la barra de estado: reemplaza el home del usuario
// por "~" al inicio de la ruta (estilo shell). NO toca filename real: esa
// sigue siendo la ruta absoluta que usan save()/openFileToBuffer() para
// guardar y detectar duplicados. Si $HOME no esta seteado, devuelve la
// ruta sin cambios.
std::string collapseHome(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return path;
    std::string h(home);
    // Evita cortar a mitad de nombre: "/home/usuario2" no debe volverse
    // "~2" cuando HOME es "/home/usuario". Solo colapsa si coincide entero
    // o coincide seguido de un separador "/".
    if (path.size() < h.size() || path.compare(0, h.size(), h) != 0)
        return path;
    if (path.size() > h.size() && path[h.size()] != '/')
        return path;
    return "~" + path.substr(h.size());
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
    data.path = collapseHome(dirName(filename));   // <-- antes: data.path = dirName(filename);
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

// Escribe una fila de texto de ancho fijo `width`, truncando si excede y
// rellenando con espacios si sobra, para que el fondo (si se pasa uno)
// cubra SIEMPRE las `width` columnas completas y no solo el texto.
// `bgStyle` vacio ("") = sin fondo, se escribe el texto truncado tal cual
// (mismo comportamiento que antes para las filas no-activas).
void renderFilledRow(std::string& out, std::string_view text, int width,
                     const std::string& bgStyle, const std::string& reset) {
    std::string truncated = utf8::truncate(text, width);
    if (bgStyle.empty()) {
        out += truncated;
        return;
    }
    out += bgStyle;
    out += truncated;
    for (int c = colCount(truncated); c < width; ++c) out += ' ';
    out += reset;
}

// Celda de numero de linea: numero alineado a la derecha + un espacio de
// separacion. La fila del cursor (isCurrentLine) lleva el estilo propio
// del numero activo (gutterCurrent: negrita blanca sobre el mismo gris de
// currentLine, conectando el numero con el resaltado de la fila); el resto
// de los numeros va en gris tenue (lineNumber).
//
// Cota de ancho (v1.1, regresion): la celda NUNCA supera `gutterW` columnas,
// aun en una terminal ultra-chica donde el numero es mas ancho que el propio
// gutter (buildScreen recorta gutterW al ancho disponible). Se conserva la
// COLA del numero (se pierde el inicio), igual que se trunca la ruta en la
// barra de estado.
std::string renderGutterCell(const Theme& T, int lineNumber1Based, int gutterW,
                             bool isCurrentLine) {
    std::string numStr = std::to_string(lineNumber1Based);
    const int maxNumCols = std::max(0, gutterW - 1); // 1 columna es el separador
    if (static_cast<int>(numStr.size()) > maxNumCols)
        numStr = numStr.substr(numStr.size() - static_cast<size_t>(maxNumCols));
    const int pad = std::max(0, gutterW - 1 - static_cast<int>(numStr.size()));
    std::string out;
    out += isCurrentLine ? T.gutterCurrent : T.lineNumber;
    out.append(pad, ' ');
    out += numStr;
    out += ' ';
    out += T.reset;
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
// `isCurrentLine` resalta la fila del cursor con el estilo del Theme
// (theme_.currentLine): el resaltado cubre TODA la fila (incluido el
// relleno hasta `width`, no solo el texto), y la seleccion siempre gana
// sobre el (el tramo seleccionado se pinta en video inverso y el resto de
// la fila lleva el estilo de linea).
//
// `lineBreakSelected` marca el caso de una fila VACIA atravesada por la
// seleccion: su unico "contenido" es el salto de linea, y al estar
// seleccionado se pinta la fila entera en video inverso, sin ningun
// simbolo (si no, la fila quedaria en blanco y no se veria que se la
// selecciono).
void renderLine(std::string& out,
                const Theme& T,
                std::string_view line,
                int width,
                bool isCurrentLine,
                int selStartByte = -1,
                int selEndByte = -1,
                bool lineBreakSelected = false) {
    // Fila vacia con su salto de linea seleccionado: la fila completa se
    // pinta en video inverso (sin simbolo) para que se vea que quedo
    // seleccionada.
    if (line.empty() && lineBreakSelected) {
        out += T.selection;
        for (int i = 0; i < width; ++i) out += ' ';
        out += T.reset;
        return;
    }

    // Sin seleccion aqui (o seleccion vacia): el texto (truncado a width).
    // Si es la fila actual, se rellena hasta `width` para que el resaltado
    // cubra toda la fila, no solo el texto.
    if (selStartByte < 0 || selEndByte < 0 || selStartByte >= selEndByte) {
        renderFilledRow(out, line, width, isCurrentLine ? T.currentLine : "", T.reset);
        return;
    }

    // Limitar la seleccion a lo que existe en la linea (si el fin cae
    // mas alla del largo de la linea, por ejemplo en la ultima).
    int endByte = std::min<int>(selEndByte, static_cast<int>(line.size()));
    int startByte = std::min<int>(selStartByte, endByte);

    int startCol = utf8::columnOf(line, startByte);
    int endCol = utf8::columnOf(line, endByte);

    // Parte antes de la seleccion, en estilo de linea actual si corresponde.
    std::string_view before = utf8::range(line, 0, std::min(startCol, width));
    // Parte seleccionada, en video inverso (siempre gana).
    std::string_view selected = utf8::range(line, std::min(startCol, width),
                                       std::min(endCol, width));
    // Parte despues de la seleccion (si queda espacio).
    std::string_view after = utf8::range(line, std::min(endCol, width), width);

    if (isCurrentLine) out += T.currentLine;
    out += before;
    if (isCurrentLine) out += T.reset;
    out += T.selection;
    out += selected;
    out += T.reset;
    if (isCurrentLine) out += T.currentLine;
    out += after;
    if (isCurrentLine) {
        int used = colCount(before) + colCount(selected) + colCount(after);
        for (int i = used; i < width; ++i) out += ' ';
        out += T.reset;
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
                                   const std::optional<Selection>& selection,
                                   const std::optional<Selection>& searchHighlight) {
    std::string out;
    beginFrame(out);
    out += buildEditorBody(doc, cursor, viewport, filename, modified, message,
                           state, selection, searchHighlight);
    if (state == State::Busqueda) {
        return out;
    }
    int curRow = 1, curCol = 1;
    editorCursorPos(doc, cursor, viewport, curRow, curCol);
    moveCursorTo(out, curRow, curCol);
    setCursorStyle(out, state);
    endFrame(out);
    return out;
}

Renderer::EditorGeometry Renderer::editorGeometry(const Document& doc,
                                                    const Viewport& viewport) const {
    EditorGeometry g;
    g.layout = calculateLayout(viewport.height, viewport.width);
    g.gutterW = std::min(gutterWidth(doc.lineCount()), viewport.width);
    return g;
}

std::string Renderer::buildEditorBody(const Document& doc,
                                       const Cursor& cursor,
                                       const Viewport& viewport,
                                       const std::string& filename,
                                       bool modified,
                                       const Message& message,
                                       State state,
                                       const std::optional<Selection>& selection,
                                       const std::optional<Selection>& searchHighlight) const {
    std::string out;
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection)
                                                          : std::nullopt;
    std::optional<Normalized> searchSel = searchHighlight.has_value() ? normalize(*searchHighlight)
                                                                      : std::nullopt;
    const EditorGeometry g = editorGeometry(doc, viewport);
    renderEditorContent(out, doc, cursor, viewport, sel, searchSel, g.layout.content,
                        g.gutterW);
    StatusBarData data =
        editorBarData(filename, modified, stateLabel(state), message, cursor,
                      doc.lineCount());
    data.estadoAccent = stateAccent(theme_, state);
    renderStatusBar(out, g.layout.statusBar, data);
    return out;
}

void Renderer::editorCursorPos(const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               int& outRow, int& outCol) const {
    const EditorGeometry g = editorGeometry(doc, viewport);
    outRow = cursor.line - viewport.top + 1;
    int absoluteCol = utf8::columnOf(doc.lineAt(cursor.line), cursor.col);
    int visibleCol = absoluteCol - viewport.left;
    outCol = g.gutterW + visibleCol + 1 + g.layout.content.col;
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

void Renderer::hideCursor(std::string& out) const { out += "\x1b[?25l"; }
void Renderer::showCursor(std::string& out) const { out += "\x1b[?25h"; }
void Renderer::setCursorStyle(std::string& out, State state) const {
    if (state == State::Interaccion) out += "\x1b[1 q";
    else out += "\x1b[2 q";
}

void Renderer::beginFrame(std::string& out) const {
    hideCursor(out);
    if (!theme_.background.empty()) out += theme_.background;
    if (!theme_.foreground.empty()) out += theme_.foreground;
    out += "\x1b[2J";
    out += "\x1b[H";
}

void Renderer::endFrame(std::string& out) const {
    showCursor(out);
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
    renderEditorContent(out, doc, cursor, viewport, sel, std::nullopt, area, gutterW);
}

void Renderer::renderEditorRow(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const std::optional<Normalized>& searchSel,
                             int docLine,
                             int gutterW,
                             int textWidth) const {
    if (docLine < doc.lineCount()) {
        const std::string& line = doc.lineAt(docLine);
        bool isCurrentLine = (docLine == cursor.line);

        std::vector<std::pair<int,int>> byteIntervals;
        bool lineBreakSelected = false;

        auto addInterval = [&](const std::optional<Normalized>& n) {
            if (!n.has_value()) return;
            if (docLine < n->start.line || docLine > n->end.line) return;
            if (n->start.line == n->end.line) {
                byteIntervals.emplace_back(n->start.col, n->end.col);
            } else if (docLine == n->start.line) {
                byteIntervals.emplace_back(n->start.col, static_cast<int>(line.size()));
            } else if (docLine == n->end.line) {
                byteIntervals.emplace_back(0, n->end.col);
            } else {
                byteIntervals.emplace_back(0, static_cast<int>(line.size()));
            }
        };
        addInterval(sel);
        addInterval(searchSel);

        auto isLineBreak = [&](const std::optional<Normalized>& n) -> bool {
            if (!n.has_value() || !line.empty()) return false;
            if (docLine < n->start.line || docLine > n->end.line) return false;
            bool singleLine = (n->start.line == n->end.line);
            bool endsAtStart = (docLine == n->end.line && n->end.col == 0);
            return !singleLine && !endsAtStart;
        };
        if (isLineBreak(sel) || isLineBreak(searchSel)) lineBreakSelected = true;

        if (line.empty() && lineBreakSelected) {
            out += renderGutterCell(theme_, docLine + 1, gutterW, isCurrentLine);
            out += theme_.selection;
            for (int i = 0; i < textWidth; ++i) out += ' ';
            out += theme_.reset;
            return;
        }

        std::sort(byteIntervals.begin(), byteIntervals.end());
        std::vector<std::pair<int,int>> merged;
        for (auto &p : byteIntervals) {
            if (p.first >= p.second) continue;
            if (merged.empty() || p.first > merged.back().second) merged.push_back(p);
            else merged.back().second = std::max(merged.back().second, p.second);
        }

        out += renderGutterCell(theme_, docLine + 1, gutterW, isCurrentLine);

        int absoluteVisStart = viewport.left;
        int absoluteVisEnd = absoluteVisStart + textWidth;
        std::string_view visible = utf8::range(line, absoluteVisStart, absoluteVisEnd);

        if (merged.empty()) {
            renderLine(out, theme_, visible, textWidth, isCurrentLine, -1, -1, false);
            return;
        }

        std::vector<std::pair<int,int>> visibleIntervals;
        for (auto &p : merged) {
            int absoluteSc = utf8::columnOf(line, p.first);
            int absoluteEc = utf8::columnOf(line, p.second);
            if (absoluteEc <= absoluteVisStart || absoluteSc >= absoluteVisEnd) continue;
            int visibleSc = std::max(absoluteSc, absoluteVisStart) - absoluteVisStart;
            int visibleEc = std::min(absoluteEc, absoluteVisEnd) - absoluteVisStart;
            if (visibleSc < visibleEc) visibleIntervals.emplace_back(visibleSc, visibleEc);
        }
        if (visibleIntervals.empty()) {
            renderLine(out, theme_, visible, textWidth, isCurrentLine, -1, -1, false);
            return;
        }

        int visibleCur = 0;
        int used = 0;
        for (size_t i = 0; i < visibleIntervals.size(); ++i) {
            int visibleSc = visibleIntervals[i].first;
            int visibleEc = visibleIntervals[i].second;
            if (visibleCur < visibleSc) {
                std::string_view seg = utf8::range(visible, visibleCur, visibleSc);
                if (isCurrentLine) out += theme_.currentLine;
                out += seg;
                if (isCurrentLine) out += theme_.reset;
                used += colCount(seg);
            }
            {
                std::string_view seg = utf8::range(visible, visibleSc, visibleEc);
                out += theme_.selection;
                out += seg;
                out += theme_.reset;
                used += colCount(seg);
            }
            visibleCur = visibleEc;
        }
        if (visibleCur < textWidth) {
            std::string_view tail = utf8::range(visible, visibleCur, textWidth);
            if (isCurrentLine) out += theme_.currentLine;
            out += tail;
            if (isCurrentLine) {
                used += colCount(tail);
                for (int c = used; c < textWidth; ++c) out += ' ';
                out += theme_.reset;
            }
        } else if (isCurrentLine) {
            for (int c = used; c < textWidth; ++c) out += ' ';
            out += theme_.reset;
        }
        return;
    }
    out += renderGutterBlank(gutterW);
    out += theme_.marker;
    out += "~";
    out += theme_.reset;
}

void Renderer::renderEditorContent(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const std::optional<Normalized>& searchSel,
                             const Rect& area,
                             int gutterW) const {
    int textWidth = std::max(0, area.width - gutterW);
    for (int row = 0; row < area.height; ++row) {
        int docLine = viewport.top + row;
        out += "\x1b[K";
        renderEditorRow(out, doc, cursor, viewport, sel, searchSel, docLine, gutterW, textWidth);
        out += "\r\n";
    }
}

void Renderer::renderStatusBar(std::string& out,
                                const Rect& area,
                                const StatusBarData& data) const {
    // La barra NO conoce editor, buffer ni documento: la pantalla produce
    // un StatusBarData y la barra comun (StatusBar) solo lo pinta. El Theme
    // del Renderer se propaga a la barra: es un unico esquema de color.
    StatusBar bar;
    bar.setTheme(theme_);
    out += bar.render(area, data);
}

bool Renderer::patchContentRow(std::string& out, const Document& doc, const Cursor& cursor,
                               const Viewport& viewport, const std::optional<Normalized>& sel,
                               const std::optional<Normalized>& searchSel, int docLine,
                               int gutterW, int textWidth, int contentH) {
    const int row = docLine - viewport.top;
    if (row < 0 || row >= contentH || row >= static_cast<int>(rowCache_.size())) return false;
    std::string full;
    full += "\x1b[K";
    renderEditorRow(full, doc, cursor, viewport, sel, searchSel, docLine, gutterW, textWidth);
    if (rowCache_[static_cast<size_t>(row)] == full) return false;
    moveCursorTo(out, row + 1, 1);
    out += theme_.reset;
    out += full;
    rowCache_[static_cast<size_t>(row)] = std::move(full);
    return true;
}

void Renderer::patchStatusBar(std::string& out, const Document& doc, const Cursor& cursor,
                              const std::string& filename, bool modified, const Message& message,
                              State state, const Layout& layout, int contentH) {
    StatusBarData data = editorBarData(filename, modified, stateLabel(state), message, cursor, doc.lineCount());
    data.estadoAccent = stateAccent(theme_, state);
    if (hasLastStatusData_ && data.name == lastStatusData_.name && data.path == lastStatusData_.path &&
        data.estado == lastStatusData_.estado && data.estadoAccent == lastStatusData_.estadoAccent &&
        data.message.text == lastStatusData_.message.text && data.message.kind == lastStatusData_.message.kind &&
        data.right == lastStatusData_.right && data.modified == lastStatusData_.modified &&
        data.cursorLine == lastStatusData_.cursorLine && data.cursorCol == lastStatusData_.cursorCol &&
        data.totalLines == lastStatusData_.totalLines) {
        return;
    }
    std::string statusBody;
    renderStatusBar(statusBody, layout.statusBar, data);
    if (statusBody == statusCache_) {
        lastStatusData_ = data;
        hasLastStatusData_ = true;
        return;
    }

    std::vector<std::string_view> oldRows, newRows;
    splitRows(statusCache_, &oldRows);
    splitRows(statusBody, &newRows);
    const size_t n = std::max(oldRows.size(), newRows.size());
    for (size_t i = 0; i < n; ++i) {
        std::string_view oldRow = i < oldRows.size() ? oldRows[i] : std::string_view{};
        std::string_view newRow = i < newRows.size() ? newRows[i] : std::string_view{};
        if (oldRow == newRow) continue;
        moveCursorTo(out, contentH + static_cast<int>(i) + 1, 1);
        out += theme_.reset;
        out += "\x1b[K";
        out.append(newRow.data(), newRow.size());
    }
    statusCache_ = std::move(statusBody);
    lastStatusData_ = data;
    hasLastStatusData_ = true;
}

void Renderer::rebuildCache(const Document& doc, const Cursor& cursor, const Viewport& viewport,
                            const std::string& filename, bool modified, const Message& message,
                            State state, const std::optional<Selection>& selection,
                            const std::optional<Selection>& searchHighlight) {
    const Layout layout = calculateLayout(viewport.height, viewport.width);
    const int contentH = layout.content.height;
    const EditorGeometry g = editorGeometry(doc, viewport);
    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, layout.content.width - gutterW);
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection) : std::nullopt;
    std::optional<Normalized> searchSel = searchHighlight.has_value() ? normalize(*searchHighlight) : std::nullopt;

    rowCache_.clear();
    for (int row = 0; row < contentH; ++row) {
        std::string full = "\x1b[K";
        renderEditorRow(full, doc, cursor, viewport, sel, searchSel,
                        viewport.top + row, gutterW, textWidth);
        rowCache_.push_back(std::move(full));
    }

    StatusBarData data = editorBarData(filename, modified, stateLabel(state), message, cursor, doc.lineCount());
    data.estadoAccent = stateAccent(theme_, state);
    statusCache_.clear();
    renderStatusBar(statusCache_, layout.statusBar, data);
    lastStatusData_ = data;
    hasLastStatusData_ = true;

    hasCache_ = true;
    cachedContentH_ = contentH;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
}

std::string Renderer::buildCursorMoveFrame(const Document& doc, const Cursor& cursor,
                                           const Viewport& viewport, const std::string& filename,
                                           bool modified, const Message& message, State state,
                                           const std::optional<Selection>& selection,
                                           const std::optional<Selection>& searchHighlight) {
    const EditorGeometry g = editorGeometry(doc, viewport);
    const Layout& layout = g.layout;
    const int contentH = layout.content.height;
    if (contentH <= 0 || static_cast<int>(rowCache_.size()) != contentH) return "";

    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, layout.content.width - gutterW);
    const std::optional<Normalized> sel = std::nullopt;
    const std::optional<Normalized> searchSel = std::nullopt;
    (void)selection;
    (void)searchHighlight;

    std::string out;
    hideCursor(out);
    if (lastCursorLine_ == cursor.line) {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, cursor.line, gutterW, textWidth, contentH);
    } else {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, lastCursorLine_, gutterW, textWidth, contentH);
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, cursor.line, gutterW, textWidth, contentH);
    }
    patchStatusBar(out, doc, cursor, filename, modified, message, state, layout, contentH);

    if (state != State::Busqueda) {
        int curRow = 1, curCol = 1;
        editorCursorPos(doc, cursor, viewport, curRow, curCol);
        moveCursorTo(out, curRow, curCol);
        setCursorStyle(out, state);
        showCursor(out);
    }
    lastCursorLine_ = cursor.line;
    lastCursorCol_ = cursor.col;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
    return out;
}

static bool writeAll(int fd, const std::string& s) {
    const char* p = s.c_str();
    std::size_t remaining = s.size();
    while (remaining > 0) {
        ssize_t n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        p += static_cast<std::size_t>(n);
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

void Renderer::renderScreen(const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::string& filename,
                             bool modified,
                             const Message& message,
                             State state,
                             const std::optional<Selection>& selection,
                             const std::optional<Selection>& searchHighlight) {
    std::string buffer = buildScreen(doc, cursor, viewport, filename,
                                     modified, message, state, selection, searchHighlight);
    writeAll(STDOUT_FILENO, buffer);
}

void Renderer::splitRows(const std::string& body, std::vector<std::string_view>* rows) {
    rows->clear();
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t sep = body.find("\r\n", start);
        if (sep == std::string::npos) {
            rows->emplace_back(body.data() + start, body.size() - start);
            break;
        }
        rows->emplace_back(body.data() + start, sep - start);
        start = sep + 2;
    }
}

void Renderer::renderScreenDiff(const Document& doc,
                                  const Cursor& cursor,
                                  const Viewport& viewport,
                                  const std::string& filename,
                                  bool modified,
                                  const Message& message,
                                  State state,
                                  const std::optional<Selection>& selection,
                                  const std::optional<Selection>& searchHighlight) {
    const std::string out = buildDiffFrame(doc, cursor, viewport, filename,
                                             modified, message, state, selection, searchHighlight);
    if (!writeAll(STDOUT_FILENO, out)) hasCache_ = false;
}

std::string Renderer::buildScrollFrame(const Document& doc, const Cursor& cursor,
                                        const Viewport& viewport, const std::string& filename,
                                        bool modified, const Message& message, State state,
                                        const std::optional<Selection>& selection,
                                        const std::optional<Selection>& searchHighlight,
                                        int deltaTop) {
    const Layout layout = calculateLayout(viewport.height, viewport.width);
    const int contentH = layout.content.height;
    const int absDelta = std::abs(deltaTop);
    if (contentH <= 0 || absDelta == 0 || absDelta >= contentH) return "";
    if (static_cast<int>(rowCache_.size()) != contentH) return "";

    const EditorGeometry g = editorGeometry(doc, viewport);
    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, layout.content.width - gutterW);
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection) : std::nullopt;
    std::optional<Normalized> searchSel = searchHighlight.has_value() ? normalize(*searchHighlight) : std::nullopt;

    std::string out;
    hideCursor(out);
    out += "\x1b[1;";
    out += std::to_string(contentH);
    out += "r";
    out += "\x1b[";
    out += std::to_string(absDelta);
    out += (deltaTop > 0) ? "S" : "T";
    out += "\x1b[r";

    if (deltaTop > 0) {
        for (int i = 0; i < absDelta; ++i) rowCache_.pop_front();
        for (int i = 0; i < absDelta; ++i) {
            const int docLine = viewport.top + contentH - absDelta + i;
            std::string full = "\x1b[K";
            renderEditorRow(full, doc, cursor, viewport, sel, searchSel, docLine, gutterW, textWidth);
            rowCache_.push_back(std::move(full));
        }
        for (int i = 0; i < absDelta; ++i) {
            const int row = contentH - absDelta + i;
            moveCursorTo(out, row + 1, 1);
            out += theme_.reset;
            out += rowCache_[static_cast<size_t>(row)];
        }
    } else {
        for (int i = 0; i < absDelta; ++i) rowCache_.pop_back();
        for (int i = 0; i < absDelta; ++i) {
            const int docLine = viewport.top + absDelta - 1 - i;
            std::string full = "\x1b[K";
            renderEditorRow(full, doc, cursor, viewport, sel, searchSel, docLine, gutterW, textWidth);
            rowCache_.push_front(std::move(full));
        }
        for (int i = 0; i < absDelta; ++i) {
            moveCursorTo(out, i + 1, 1);
            out += theme_.reset;
            out += rowCache_[static_cast<size_t>(i)];
        }
    }

    if (lastCursorLine_ == cursor.line) {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, cursor.line, gutterW, textWidth, contentH);
    } else {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, lastCursorLine_, gutterW, textWidth, contentH);
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, cursor.line, gutterW, textWidth, contentH);
    }
    patchStatusBar(out, doc, cursor, filename, modified, message, state, layout, contentH);

    if (state != State::Busqueda) {
        int curRow = 1, curCol = 1;
        editorCursorPos(doc, cursor, viewport, curRow, curCol);
        moveCursorTo(out, curRow, curCol);
        setCursorStyle(out, state);
        showCursor(out);
    }

    lastViewportTop_ = viewport.top;
    lastViewportLeft_ = viewport.left;
    lastCursorLine_ = cursor.line;
    lastCursorCol_ = cursor.col;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
    return out;
}

std::string Renderer::buildDiffFrame(const Document& doc,
                                       const Cursor& cursor,
                                       const Viewport& viewport,
                                       const std::string& filename,
                                       bool modified,
                                       const Message& message,
                                       State state,
                                       const std::optional<Selection>& selection,
                                       const std::optional<Selection>& searchHighlight) {
    const Layout layout = calculateLayout(viewport.height, viewport.width);
    const int contentH = layout.content.height;

    if (!hasCache_ || viewport.width != lastViewportW_ || viewport.height != lastViewportH_
        || cachedContentH_ != contentH) {
        rebuildCache(doc, cursor, viewport, filename, modified, message, state, selection, searchHighlight);
        lastViewportW_ = viewport.width;
        lastViewportH_ = viewport.height;
        lastViewportTop_ = viewport.top;
        lastViewportLeft_ = viewport.left;
        lastCursorLine_ = cursor.line;
        lastCursorCol_ = cursor.col;
        lastVersion_ = doc.version();
        lastLineCount_ = doc.lineCount();
        std::string out;
        beginFrame(out);
        for (auto& row : rowCache_) { out += row; out += "\r\n"; }
        out += statusCache_;
        if (state == State::Busqueda) return out;
        int curRow = 1, curCol = 1;
        editorCursorPos(doc, cursor, viewport, curRow, curCol);
        moveCursorTo(out, curRow, curCol);
        setCursorStyle(out, state);
        endFrame(out);
        return out;
    }

    const int deltaTop = viewport.top - lastViewportTop_;
    const int deltaLeft = viewport.left - lastViewportLeft_;
    const bool noHighlight = !selection.has_value() && !searchHighlight.has_value();

    // Only use scroll frame for single-line scrolls (matching original behavior).
    // Multi-line scrolls fall through to slow path which does row-by-row diff.
    if ((deltaTop == 1 || deltaTop == -1) && deltaLeft == 0 && noHighlight) {
        const std::string scrollFrame = buildScrollFrame(doc, cursor, viewport, filename,
                                                         modified, message, state,
                                                         selection, searchHighlight,
                                                         deltaTop);
        if (!scrollFrame.empty()) return scrollFrame;
    }

    if (deltaTop == 0 && deltaLeft == 0 && noHighlight) {
        const bool sameLineEdit = doc.version() != lastVersion_ && cursor.line == lastCursorLine_ &&
                                  cursor.col != lastCursorCol_ && doc.lineCount() == lastLineCount_;
        const bool pureCursorMove = doc.version() == lastVersion_ &&
                                    (cursor.line != lastCursorLine_ || cursor.col != lastCursorCol_);
        if (sameLineEdit || pureCursorMove) {
            const std::string moveFrame = buildCursorMoveFrame(doc, cursor, viewport, filename,
                                                                modified, message, state,
                                                                selection, searchHighlight);
            if (!moveFrame.empty()) return moveFrame;
        }
    }

    // Camino lento: selección/búsqueda activa, o los caminos rápidos declinaron.
    const std::string fresh = buildEditorBody(doc, cursor, viewport, filename,
                                              modified, message, state,
                                              selection, searchHighlight);
    std::vector<std::string_view> newRows;
    splitRows(fresh, &newRows);
    std::vector<std::string_view> oldStatusRows;
    splitRows(statusCache_, &oldStatusRows);
    std::string out;
    hideCursor(out);
    for (size_t i = 0; i < newRows.size(); ++i) {
        std::string_view oldRow;
        if (i < static_cast<size_t>(contentH)) {
            if (i < rowCache_.size()) oldRow = rowCache_[i];
        } else if (i - static_cast<size_t>(contentH) < oldStatusRows.size()) {
            oldRow = oldStatusRows[i - static_cast<size_t>(contentH)];
        }
        if (oldRow == newRows[i]) continue;
        moveCursorTo(out, static_cast<int>(i) + 1, 1);
        out += theme_.reset;
        out += "\x1b[K";
        out.append(newRows[static_cast<size_t>(i)].data(), newRows[static_cast<size_t>(i)].size());
    }

    rowCache_.clear();
    for (int i = 0; i < contentH && static_cast<size_t>(i) < newRows.size(); ++i)
        rowCache_.emplace_back(newRows[static_cast<size_t>(i)]);
    statusCache_.clear();
    for (size_t i = static_cast<size_t>(contentH); i < newRows.size(); ++i) {
        if (i > static_cast<size_t>(contentH)) statusCache_ += "\r\n";
        statusCache_.append(newRows[i].data(), newRows[i].size());
    }
    {
        StatusBarData data = editorBarData(filename, modified, stateLabel(state), message, cursor, doc.lineCount());
        data.estadoAccent = stateAccent(theme_, state);
        lastStatusData_ = data;
        hasLastStatusData_ = true;
    }

    if (state == State::Busqueda) {
        lastViewportTop_ = viewport.top;
        lastViewportLeft_ = viewport.left;
        lastCursorLine_ = cursor.line;
        lastCursorCol_ = cursor.col;
        lastVersion_ = doc.version();
        lastLineCount_ = doc.lineCount();
        return out;
    }
    int curRow = 1, curCol = 1;
    editorCursorPos(doc, cursor, viewport, curRow, curCol);
    moveCursorTo(out, curRow, curCol);
    setCursorStyle(out, state);
    showCursor(out);

    lastViewportTop_ = viewport.top;
    lastViewportLeft_ = viewport.left;
    lastCursorLine_ = cursor.line;
    lastCursorCol_ = cursor.col;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
    return out;
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

    // Ciclo de vida del frame global (ocultar cursor / home / limpiar).
    beginFrame(out);

    // El Renderer calcula el Layout y delega: el selector solo dibuja su
    // contenido; la barra la dibuja el StatusBar comun.
    Layout layout = calculateLayout(height, width);
    renderBufferListContent(out, names, selected, layout.content);

    // Datos de la barra (paso 6): Buffers | SELECCIONAR | n/total.
    StatusBarData data;
    data.name = "Buffers";
    data.estado = "SELECCIONAR";
    data.estadoAccent = theme_.accentBuffers;
    const int total = static_cast<int>(names.size());
    data.right = std::to_string(std::min(selected + 1, total)) + "/" +
                 std::to_string(total);
    renderStatusBar(out, layout.statusBar, data);

    // Cursor real de la terminal sobre la fila seleccionada de la lista.
    // Se clampa a las filas ya dibujadas para no invadir la barra final.
    int rows = std::min(static_cast<int>(names.size()), height);
    int cursorRow = std::max(1, std::min(selected + 1, rows));
    moveCursorTo(out, cursorRow, 1);

    endFrame(out);
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
        bool isSelected = (static_cast<int>(i) == selected);
        renderFilledRow(out, line, area.width,
                isSelected ? theme_.listSelected : "", theme_.reset);
        out += "\r\n";
    }
    // Filas vacias: marcador del editor ("~") alineado con las entradas
    // (misma indentacion de 2 espacios), con el estilo del Theme.
    for (int r = rows; r < area.height; ++r) {
        out += "\x1b[K";
        out += "  ";
        out += theme_.marker;
        out += "~";
        out += theme_.reset;
        out += "\r\n";
    }
}

void Renderer::renderBufferList(const std::vector<std::string>& names,
                                 int selected,
                                 int width,
                                 int height) {
    hasCache_ = false;
    std::string buffer = buildBufferListScreen(names, selected, width, height);
    writeAll(STDOUT_FILENO, buffer);
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

    // Ciclo de vida del frame global (ocultar cursor / home / limpiar).
    beginFrame(out);

    Layout layout = calculateLayout(height, width);
    renderFileListContent(out, names, selected, scroll, layout.content);

    // Datos de la barra (paso 7): ruta | ABRIR ARCHIVO | n/m + ayuda.
    StatusBarData data;
    data.name = path.empty() ? "/" : collapseHome(path);
    data.estado = "ABRIR ARCHIVO";
    data.estadoAccent = theme_.accentAbrir;
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

    endFrame(out);
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

            bool isSelected = (idx == selected);
            renderFilledRow(out, line, area.width,
                isSelected ? theme_.listSelected : "", theme_.reset);
        } else {
            // Filas vacias: marcador "~" alineado con las entradas.
            out += "  ";
            out += theme_.marker;
            out += "~";
            out += theme_.reset;
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
    hasCache_ = false;
    std::string buffer = buildFileListScreen(names, selected, scroll,
                                              path, message, width, height);
    writeAll(STDOUT_FILENO, buffer);
}