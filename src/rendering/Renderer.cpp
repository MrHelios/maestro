#include "rendering/Renderer.h"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <unistd.h>
#include <algorithm>

#include "diagnostics/Instrument.h"
#include "base/utf8.h"
#include "rendering/RenderUtil.h"
#include "syntax/SyntaxLanguage.h"

namespace {

// Helpers de texto/UTF-8 compartidos con el StatusBar (definidos en
// ui/RenderUtil.h como chrome::*). La barra de estado propia (barra fija +
// fila de mensajes) vive en ui/StatusBar.cpp; aqui solo queda lo que
// pertenece al contenido (gutter, numeros, seleccion).
using namespace chrome;

struct StatePresentation {
    std::string label;
    std::string accent;
};

// Presentación unificada de estado (label + accent). Busqueda comparte el
// accent de Guardar e IrAFila el de Navegación por diseño; el resto tiene
// color propio.
StatePresentation statePresentation(const Theme& T, State state) {
    switch (state) {
        case State::Navegacion:     return {"NAVEGACION", T.accentNavegacion};
        case State::Interaccion:    return {"INTERACCION", T.accentInteraccion};
        case State::Seleccion:      return {"SELECCION", T.accentSeleccion};
        case State::Prefix:         return {"COMANDO", T.accentComando};
        case State::BufferSelector: return {"BUFFERS", T.accentBuffers};
        case State::SaveAs:         return {"GUARDAR", T.accentGuardar};
        case State::FileBrowser:    return {"ABRIR", T.accentAbrir};
        case State::Busqueda:       return {"BUSQUEDA", T.accentGuardar};
        case State::IrAFila:        return {"IR A FILA", T.accentNavegacion};
    }
    return {"", T.statusBarAccent};
}

// Solo para MOSTRAR en la barra de estado: reemplaza el home del usuario
// por "~" al inicio de la ruta (estilo shell). NO toca filename real: esa
// sigue siendo la ruta absoluta que usan save()/openFileInBuffer() para
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

// La barra de estado fija + fila de mensajes (buildChrome, layoutLeftBlock,
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
    instrument::ScopedTimer _t(instrument::enabled ? &instrument::current.renderFilledRow_nanos : nullptr);
    if (instrument::enabled) instrument::onRenderFilledRow();
    if (bgStyle.empty()) {
        instrument::setTag(instrument::Utf8Tag::RangeFilled);
        std::string_view visible = utf8::range(text, 0, width);
        instrument::clearTag();
        out.append(visible.data(), visible.size());
        return;
    }
    instrument::setTag(instrument::Utf8Tag::TruncateFilled);
    std::string truncated = utf8::truncate(text, width);
    instrument::clearTag();
    out += bgStyle;
    out += truncated;
    instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
    int cc = colCount(truncated);
    instrument::clearTag();
    for (int c = cc; c < width; ++c) out += ' ';
    out += reset;
}

void renderEmptyMarkerRow(std::string& out, const Theme& T, int width) {
    if (width <= 0) return;
    if (width <= 2) {
        out += utf8::truncate("  ", width);
        return;
    }
    out += "  ";
    out += T.marker;
    out += "~";
    out += T.reset;
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
    if (gutterW <= 0) return "";
    std::string numStr = std::to_string(lineNumber1Based);
    const int maxNumCols = std::max(0, gutterW - 1);
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
void renderPlainLine(std::string& out,
                     const Theme& T,
                     std::string_view line,
                     int width,
                     bool isCurrentLine) {
    renderFilledRow(out, line, width, isCurrentLine ? T.currentLine : "", T.reset);
}

const std::string& rendererSyntaxStyleFor(const Theme& T, SyntaxToken tok) {
    switch (tok) {
        case SyntaxToken::Keyword:      return T.syntaxKeyword;
        case SyntaxToken::Type:         return T.syntaxType;
        case SyntaxToken::Preprocessor: return T.syntaxPreprocessor;
        case SyntaxToken::String:       return T.syntaxString;
        case SyntaxToken::Character:    return T.syntaxCharacter;
        case SyntaxToken::Number:       return T.syntaxNumber;
        case SyntaxToken::Comment:      return T.syntaxComment;
    }
    return T.syntaxKeyword;
}

} // namespace

const std::string& Renderer::syntaxStyleFor(SyntaxToken tok) const {
    return rendererSyntaxStyleFor(theme_, tok);
}

void Renderer::normalizeBracketPair(const std::optional<BracketPair>& pair,
                                    std::optional<Normalized>& outOpen,
                                    std::optional<Normalized>& outClose) {
    outOpen = std::nullopt;
    outClose = std::nullopt;
    if (!pair) return;
    Selection so{pair->open, {pair->open.line, pair->open.col + 1}};
    Selection sc{pair->close, {pair->close.line, pair->close.col + 1}};
    outOpen = normalize(so);
    outClose = normalize(sc);
}

SyntaxState Renderer::syntaxStateAt(const Document& doc, int targetLine) const {
    // Usa SyntaxCache incremental con convergencia; garantiza hasta targetLine sin reparsear todo.
    if (targetLine <= 0) return SyntaxState{};
    int t = std::min(targetLine, doc.lineCount());
    auto& cache = activeCache();
    // language ya sincronizado en updateSyntaxLanguage
    const_cast<SyntaxCache&>(cache).ensureValid(doc, t);
    return cache.stateBefore(t);
}

void Renderer::updateSyntaxLanguage(const std::string& filename) const {
    auto lang = languageFromFilename(filename);
    if (lang != syntaxHighlighter_.language()) {
        syntaxHighlighter_.setLanguage(lang);
        hasCache_ = false;
        hasLastStatusData_ = false;
    }
    auto& cache = activeCache();
    if (cache.language() != lang) {
        const_cast<SyntaxCache&>(cache).setLanguage(lang);
    }
    // Mantener también el cache interno sincronizado cuando hay externo, para tests standalone
    if (externalCache_ && syntaxCache_.language() != lang) {
        syntaxCache_.setLanguage(lang);
    }
}

std::string Renderer::buildScreen(const Document& doc,
                                   const Cursor& cursor,
                                   const Viewport& viewport,
                                   const std::string& filename,
                                   bool modified,
                                   const Message& message,
                                   State state,
                                   const std::optional<Selection>& selection,
                                   const std::optional<Selection>& searchHighlight,
                                   const std::optional<BracketPair>& bracketPair) {
    if (instrument::enabled) {
        // Reset por frame para que report() muestre solo este frame.
        // El caller puede hacer snapshotAndReset() si quiere acumular.
        instrument::resetFrame();
        instrument::onBuildScreen();
    }
    instrument::ScopedTimer _t_buildScreen(&instrument::current.buildScreen_nanos);
    updateSyntaxLanguage(filename);
    std::string out;
    beginFrame(out);
    out += buildEditorBody(doc, cursor, viewport, filename, modified, message,
                           state, selection, searchHighlight, bracketPair);
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
                                       const std::optional<Selection>& searchHighlight,
                                       const std::optional<BracketPair>& bracketPair) const {
    instrument::ScopedTimer _t(&instrument::current.buildEditorBody_nanos);
    if (instrument::enabled) instrument::onBuildEditorBody();
    updateSyntaxLanguage(filename);
    std::string out;
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection)
                                                          : std::nullopt;
    std::optional<Normalized> searchSel = searchHighlight.has_value() ? normalize(*searchHighlight)
                                                                      : std::nullopt;
    std::optional<Normalized> bracketOpen, bracketClose;
    normalizeBracketPair(bracketPair, bracketOpen, bracketClose);
    const EditorGeometry g = editorGeometry(doc, viewport);
    renderEditorContent(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, g.layout.content,
                        g.gutterW);
    const auto presentation = statePresentation(theme_, state);
    StatusBarData data =
        editorBarData(filename, modified, presentation.label, message, cursor,
                      doc.lineCount());
    data.estadoAccent = presentation.accent;
    renderStatusBar(out, g.layout.statusBar, data);
    return out;
}

void Renderer::editorCursorPos(const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               int& outRow, int& outCol) const {
    const EditorGeometry g = editorGeometry(doc, viewport);
    editorCursorPos(doc, cursor, viewport, g, outRow, outCol);
}

void Renderer::editorCursorPos(const Document& doc,
                               const Cursor& cursor,
                               const Viewport& viewport,
                               const EditorGeometry& g,
                               int& outRow, int& outCol) const {
    outRow = cursor.line - viewport.top + 1;
    int absoluteCol = cursor.visualColumn(doc);
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

void Renderer::updateCacheState(const Viewport& viewport, const Cursor& cursor,
                                const Document& doc) {
    lastViewportTop_ = viewport.top;
    lastViewportLeft_ = viewport.left;
    lastCursorLine_ = cursor.line;
    lastCursorCol_ = cursor.col;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
}

void Renderer::beginFrame(std::string& out) const {
    hideCursor(out);
    if (!theme_.background.empty()) out += theme_.background;
    if (!theme_.foreground.empty()) out += theme_.foreground;
    out += "\x1b[2J";
    out += "\x1b[H";
}

// Ends a rendered frame by restoring cursor visibility only.
// Terminal rendition/background state is intentionally left untouched;
// terminal shutdown/reset is handled by the terminal lifecycle code.
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

// Usada por tests/bench sin highlight de búsqueda.
void Renderer::renderEditorContent(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const Rect& area,
                             int gutterW) const {
    renderEditorContent(out, doc, cursor, viewport, sel, std::nullopt, std::nullopt, std::nullopt, area, gutterW);
}

void Renderer::renderEditorRow(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const std::optional<Normalized>& searchSel,
                             const std::optional<Normalized>& bracketOpen,
                             const std::optional<Normalized>& bracketClose,
                             int docLine,
                             int gutterW,
                             int textWidth) const {
    if (docLine >= doc.lineCount()) {
        out += renderGutterBlank(gutterW);
        if (textWidth > 0) {
            out += theme_.marker;
            out += "~";
            out += theme_.reset;
        }
        return;
    }
    auto& cache = activeCache();
    if (!cache.isValidThrough(docLine + 1)) const_cast<SyntaxCache&>(cache).ensureValid(doc, docLine + 1);
    const auto& spans = cache.spansFor(docLine);
    renderEditorRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, docLine, gutterW, textWidth, spans);
}

void Renderer::renderEditorRow(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const std::optional<Normalized>& searchSel,
                             const std::optional<Normalized>& bracketOpen,
                             const std::optional<Normalized>& bracketClose,
                             int docLine,
                             int gutterW,
                             int textWidth,
                             const std::vector<SyntaxSpan>& spans) const {
    instrument::ScopedTimer _t(&instrument::current.renderEditorRow_nanos);
    if (instrument::enabled) instrument::onRenderEditorRow();
    if (docLine >= doc.lineCount()) {
        out += renderGutterBlank(gutterW);
        if (textWidth > 0) {
            out += theme_.marker;
            out += "~";
            out += theme_.reset;
        }
        return;
    }
    const std::string& line = doc.lineAt(docLine);
    bool isCurrentLine = (docLine == cursor.line);

    std::pair<int,int> intervals[2];
    int intervalCount = 0;
    bool lineBreakSelected = false;

    auto addInterval = [&](const std::optional<Normalized>& nrm) {
        if (!nrm.has_value()) return;
        if (docLine < nrm->start.line || docLine > nrm->end.line) return;
        if (nrm->start.line == nrm->end.line) {
            intervals[intervalCount++] = {nrm->start.col, nrm->end.col};
        } else if (docLine == nrm->start.line) {
            intervals[intervalCount++] = {nrm->start.col, static_cast<int>(line.size())};
        } else if (docLine == nrm->end.line) {
            intervals[intervalCount++] = {0, nrm->end.col};
        } else {
            intervals[intervalCount++] = {0, static_cast<int>(line.size())};
        }
    };
    addInterval(sel);
    addInterval(searchSel);

    // brackets: two 1-char intervals (open/close)
    std::pair<int,int> bracketIntervals[2];
    int bracketCount = 0;
    auto addBracket = [&](const std::optional<Normalized>& nrm) {
        if (!nrm.has_value()) return;
        if (docLine < nrm->start.line || docLine > nrm->end.line) return;
        if (nrm->start.line == nrm->end.line) {
            bracketIntervals[bracketCount++] = {nrm->start.col, nrm->end.col};
        }
    };
    addBracket(bracketOpen);
    addBracket(bracketClose);

    auto isLineBreak = [&](const std::optional<Normalized>& nrm) -> bool {
        if (!nrm.has_value() || !line.empty()) return false;
        if (docLine < nrm->start.line || docLine > nrm->end.line) return false;
        bool singleLine = (nrm->start.line == nrm->end.line);
        bool endsAtStart = (docLine == nrm->end.line && nrm->end.col == 0);
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

    if (intervalCount == 2 && intervals[0] > intervals[1]) std::swap(intervals[0], intervals[1]);
    std::pair<int,int> merged[2];
    int mergedCount = 0;
    for (int i = 0; i < intervalCount; ++i) {
        auto p = intervals[i];
        if (p.first >= p.second) continue;
        if (mergedCount == 0 || p.first > merged[mergedCount-1].second) merged[mergedCount++] = p;
        else merged[mergedCount-1].second = std::max(merged[mergedCount-1].second, p.second);
    }

    out += renderGutterCell(theme_, docLine + 1, gutterW, isCurrentLine);

    int absoluteVisStart = viewport.left;
    int absoluteVisEnd = absoluteVisStart + textWidth;
    instrument::setTag(instrument::Utf8Tag::RangeVisibleRaw);
    std::string_view visibleRaw = utf8::range(line, absoluteVisStart, absoluteVisEnd);
    instrument::clearTag();
    instrument::setTag(instrument::Utf8Tag::ExpandTabs);
    std::string visible = utf8::expandTabs(visibleRaw);
    instrument::clearTag();

    std::pair<int,int> visibleSel[2];
    int visibleSelCount = 0;
    for (int i = 0; i < mergedCount; ++i) {
        auto p = merged[i];
        instrument::setTag(instrument::Utf8Tag::ColumnOfSelection);
        int absoluteSc = utf8::columnOf(line, p.first);
        int absoluteEc = utf8::columnOf(line, p.second);
        instrument::clearTag();
        if (absoluteEc <= absoluteVisStart || absoluteSc >= absoluteVisEnd) continue;
        int visibleSc = std::max(absoluteSc, absoluteVisStart) - absoluteVisStart;
        int visibleEc = std::min(absoluteEc, absoluteVisEnd) - absoluteVisStart;
        if (visibleSc < visibleEc) visibleSel[visibleSelCount++] = {visibleSc, visibleEc};
    }

    std::pair<int,int> visibleBracket[2];
    int visibleBracketCount = 0;
    for (int i=0;i<bracketCount;++i){
        auto p = bracketIntervals[i];
        instrument::setTag(instrument::Utf8Tag::ColumnOfBracket);
        int absoluteSc = utf8::columnOf(line, p.first);
        int absoluteEc = utf8::columnOf(line, p.second);
        instrument::clearTag();
        if (absoluteEc <= absoluteVisStart || absoluteSc >= absoluteVisEnd) continue;
        int visibleSc = std::max(absoluteSc, absoluteVisStart) - absoluteVisStart;
        int visibleEc = std::min(absoluteEc, absoluteVisEnd) - absoluteVisStart;
        if (visibleSc < visibleEc) visibleBracket[visibleBracketCount++] = {visibleSc, visibleEc};
    }

    struct SynVis { int s; int e; SyntaxToken tok; };
    std::vector<SynVis> synVis;
    synVis.reserve(spans.size());
    for (auto& sp : spans) {
        instrument::setTag(instrument::Utf8Tag::ColumnOfSyntax);
        int aSc = utf8::columnOf(line, static_cast<int>(sp.begin));
        int aEc = utf8::columnOf(line, static_cast<int>(sp.end));
        instrument::clearTag();
        if (aEc <= absoluteVisStart || aSc >= absoluteVisEnd) continue;
        int vs = std::max(aSc, absoluteVisStart) - absoluteVisStart;
        int ve = std::min(aEc, absoluteVisEnd) - absoluteVisStart;
        if (vs < ve) synVis.push_back({vs, ve, sp.token});
    }

    if (visibleSelCount == 0 && visibleBracketCount == 0 && synVis.empty()) {
        renderPlainLine(out, theme_, visible, textWidth, isCurrentLine);
        return;
    }

    // Estrategia: breakpoints de selección, bracket y sintaxis; prioridad: selección > bracket > sintaxis
    std::vector<int> bounds;
    bounds.reserve(2 + visibleSelCount*2 + visibleBracketCount*2 + synVis.size()*2 + 2);
    bounds.push_back(0);
    bounds.push_back(textWidth);
    for (int i=0;i<visibleSelCount;++i){ bounds.push_back(visibleSel[i].first); bounds.push_back(visibleSel[i].second); }
    for (int i=0;i<visibleBracketCount;++i){ bounds.push_back(visibleBracket[i].first); bounds.push_back(visibleBracket[i].second); }
    for (auto& sv: synVis){ bounds.push_back(sv.s); bounds.push_back(sv.e); }
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    std::vector<int> clipped;
    clipped.reserve(bounds.size());
    for(int v: bounds) if(v>=0 && v<=textWidth) clipped.push_back(v);
    bounds.swap(clipped);
    if(bounds.empty()){ bounds.push_back(0); bounds.push_back(textWidth); }

    int used = 0;
    for (size_t i=0;i+1<bounds.size();++i){
        int segS = bounds[i];
        int segE = bounds[i+1];
        if(segS>=segE) continue;
        if(segS>=textWidth) break;
        if(segE>textWidth) segE=textWidth;
        bool inSel=false;
        for(int k=0;k<visibleSelCount;++k) if(segS>=visibleSel[k].first && segS<visibleSel[k].second){ inSel=true; break; }
        bool inBracket=false;
        if (!inSel) for(int k=0;k<visibleBracketCount;++k) if(segS>=visibleBracket[k].first && segS<visibleBracket[k].second){ inBracket=true; break; }
        instrument::setTag(instrument::Utf8Tag::RangeSegments);
        std::string_view seg = utf8::range(visible, segS, segE);
        instrument::clearTag();
        instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
        bool beyondContent = seg.empty() && segS >= colCount(visible);
        instrument::clearTag();
        if (beyondContent) continue;
        if(inSel){
            if(!seg.empty()){
                out += theme_.selection;
                out += seg;
                out += theme_.reset;
                instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
                used += colCount(seg);
                instrument::clearTag();
            }
        } else if(inBracket){
            if(!seg.empty()){
                out += theme_.bracketMatch;
                out += seg;
                out += theme_.reset;
                instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
                used += colCount(seg);
                instrument::clearTag();
            }
        } else {
            SyntaxToken tok=SyntaxToken::Keyword; bool hasSyn=false;
            for(auto& sv: synVis) if(segS>=sv.s && segS<sv.e){ hasSyn=true; tok=sv.tok; break; }
            if(seg.empty()) continue;
            if(hasSyn){
                const std::string& st = syntaxStyleFor(tok);
                if(isCurrentLine){
                    out += theme_.currentLine;
                    if(!st.empty()) out += st;
                    out += seg;
                    out += theme_.reset;
                } else {
                    if(!st.empty()) out += st;
                    out += seg;
                    if(!st.empty()) out += theme_.reset;
                }
                instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
                used += colCount(seg);
                instrument::clearTag();
            } else {
                if(isCurrentLine){
                    out += theme_.currentLine;
                    out += seg;
                    out += theme_.reset;
                } else {
                    out += seg;
                }
                instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
                used += colCount(seg);
                instrument::clearTag();
            }
        }
    }
    instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
    int visW = colCount(visible);
    instrument::clearTag();
    if (used < visW) used = visW;
    if(isCurrentLine && used < textWidth){
        out += theme_.currentLine;
        for(int c=used;c<textWidth;++c) out+=' ';
        out += theme_.reset;
    }
}

void Renderer::renderEditorContent(std::string& out,
                             const Document& doc,
                             const Cursor& cursor,
                             const Viewport& viewport,
                             const std::optional<Normalized>& sel,
                             const std::optional<Normalized>& searchSel,
                             const std::optional<Normalized>& bracketOpen,
                             const std::optional<Normalized>& bracketClose,
                             const Rect& area,
                             int gutterW) const {
    instrument::ScopedTimer _t(&instrument::current.renderEditorContent_nanos);
    if (instrument::enabled) instrument::onRenderEditorContent();
    int textWidth = std::max(0, area.width - gutterW);
    // Evitar ensureValid cuando ya está válido (isValidThrough mantiene invariante)
    {
        auto& cache = activeCache();
        int need = viewport.top + area.height;
        if (need > doc.lineCount()) need = doc.lineCount();
        if (need > 0 && !cache.isValidThrough(need)) const_cast<SyntaxCache&>(cache).ensureValid(doc, need);
    }
    for (int row = 0; row < area.height; ++row) {
        int docLine = viewport.top + row;
        out += "\x1b[K";
        if (docLine < doc.lineCount()) {
            const auto& spans = activeCache().spansFor(docLine);
            renderEditorRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, docLine, gutterW, textWidth, spans);
        } else {
            renderEditorRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, docLine, gutterW, textWidth, {});
        }
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
                               const std::optional<Normalized>& searchSel,
                               const std::optional<Normalized>& bracketOpen,
                               const std::optional<Normalized>& bracketClose, int docLine,
                               int gutterW, int textWidth, int contentH) {
    const int row = docLine - viewport.top;
    if (row < 0 || row >= contentH || row >= static_cast<int>(rowCache_.size())) return false;
    std::string full;
    full += "\x1b[K";
    renderEditorRow(full, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, docLine, gutterW, textWidth);
    // rowCache_ stores the exact terminal sequence used to paint each content
    // row, including the leading CSI K clear-line command. Therefore equality
    // with `full` is sufficient to know whether the row needs repainting.
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
    auto pres = statePresentation(theme_, state);
    StatusBarData data = editorBarData(filename, modified, pres.label, message, cursor, doc.lineCount());
    data.estadoAccent = pres.accent;
    if (hasLastStatusData_ && data == lastStatusData_) return;
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
                            const std::optional<Selection>& searchHighlight,
                            const std::optional<BracketPair>& bracketPair) {
    updateSyntaxLanguage(filename);
    const EditorGeometry g = editorGeometry(doc, viewport);
    const int contentH = g.layout.content.height;
    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, g.layout.content.width - gutterW);
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection) : std::nullopt;
    std::optional<Normalized> searchSel = searchHighlight.has_value() ? normalize(*searchHighlight) : std::nullopt;
    std::optional<Normalized> bracketOpen, bracketClose;
    normalizeBracketPair(bracketPair, bracketOpen, bracketClose);

    rowCache_.clear();
    {
        auto& cache = activeCache();
        int need = viewport.top + contentH;
        if (need > doc.lineCount()) need = doc.lineCount();
        if (need > 0) const_cast<SyntaxCache&>(cache).ensureValid(doc, need);
    }
    for (int row = 0; row < contentH; ++row) {
        int dl = viewport.top + row;
        std::string full = "\x1b[K";
        if (dl < doc.lineCount()) {
            const auto& spans = activeCache().spansFor(dl);
            renderEditorRow(full, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, dl, gutterW, textWidth, spans);
        } else {
            renderEditorRow(full, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, dl, gutterW, textWidth, {});
        }
        rowCache_.push_back(std::move(full));
    }

    const auto presentation = statePresentation(theme_, state);
    StatusBarData data = editorBarData(filename, modified, presentation.label, message, cursor, doc.lineCount());
    data.estadoAccent = presentation.accent;
    statusCache_.clear();
    renderStatusBar(statusCache_, g.layout.statusBar, data);
    lastStatusData_ = data;
    hasLastStatusData_ = true;

    hasCache_ = true;
    cachedContentH_ = contentH;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
    lastBracketPair_ = bracketPair;
    hasLastBracketPair_ = true;
}

std::string Renderer::buildCursorMoveFrame(const Document& doc, const Cursor& cursor,
                                           const Viewport& viewport, const std::string& filename,
                                           bool modified, const Message& message, State state) {
    updateSyntaxLanguage(filename);
    const EditorGeometry g = editorGeometry(doc, viewport);
    const Layout& layout = g.layout;
    const int contentH = layout.content.height;
    if (contentH <= 0 || static_cast<int>(rowCache_.size()) != contentH) return "";

    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, layout.content.width - gutterW);
    const std::optional<Normalized> sel = std::nullopt;
    const std::optional<Normalized> searchSel = std::nullopt;
    const std::optional<Normalized> bracketOpen = std::nullopt;
    const std::optional<Normalized> bracketClose = std::nullopt;

    std::string out;
    hideCursor(out);
    if (lastCursorLine_ == cursor.line) {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, cursor.line, gutterW, textWidth, contentH);
    } else {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, lastCursorLine_, gutterW, textWidth, contentH);
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, cursor.line, gutterW, textWidth, contentH);
    }
    // La edición pudo propagar estado multilínea (/*, raw string) más allá
    // de la fila del cursor: el patch de una sola fila deja el viewport con
    // spans stale. Si el viewport sigue sin validez, completarlo (acotado
    // por viewport, nunca n) y recalcular/comparar todas las filas del
    // viewport, emitiendo al terminal solamente las que cambiaron
    // (patchContentRow compara contra rowCache_). Caso neutro/edit de una
    // línea: ya está válido y se conserva el O(1) sin entrar al loop. El
    // tail más allá del viewport queda dirty (contrato SyntaxCache) y se
    // sana al scrollear.
    {
        auto& cache = activeCache();
        int need = viewport.top + contentH;
        if (need > doc.lineCount()) need = doc.lineCount();
        if (need > 0 && !cache.isValidThrough(need)) {
            cache.ensureValid(doc, need);
            for (int r = 0; r < contentH; ++r) {
                int dl = viewport.top + r;
                patchContentRow(out, doc, cursor, viewport, sel, searchSel,
                                bracketOpen, bracketClose, dl, gutterW,
                                textWidth, contentH);
            }
        }
    }
    patchStatusBar(out, doc, cursor, filename, modified, message, state, layout, contentH);

    if (state != State::Busqueda) {
        int curRow = 1, curCol = 1;
        editorCursorPos(doc, cursor, viewport, g, curRow, curCol);
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
    if (Renderer::isTestMode()) return true;
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
                             const std::optional<Selection>& searchHighlight,
                             const std::optional<BracketPair>& bracketPair) {
    std::string buffer = buildScreen(doc, cursor, viewport, filename,
                                     modified, message, state, selection, searchHighlight, bracketPair);
    writeAll(STDOUT_FILENO, buffer);
}

// Splits CRLF-delimited terminal output without copying.
// A trailing "\r\n" produces a final empty row. This is intentional because
// callers use the result for positional row-by-row comparisons.
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
                                  const std::optional<Selection>& searchHighlight,
                                  const std::optional<BracketPair>& bracketPair) {
    const std::string out = buildDiffFrame(doc, cursor, viewport, filename,
                                             modified, message, state, selection, searchHighlight, bracketPair);
    if (!writeAll(STDOUT_FILENO, out)) hasCache_ = false;
}

// Precondition: rowCache_[i] corresponds to document line viewport.top + i
// and rowCache_.size() == contentH. This function preserves that invariant
// while scrolling by one or more rows.
std::string Renderer::buildScrollFrame(const Document& doc, const Cursor& cursor,
                                        const Viewport& viewport, const std::string& filename,
                                        bool modified, const Message& message, State state,
                                        const std::optional<Selection>& selection,
                                        const std::optional<Selection>& searchHighlight,
                                        const std::optional<BracketPair>& bracketPair,
                                        int deltaTop) {
    updateSyntaxLanguage(filename);
    const EditorGeometry g = editorGeometry(doc, viewport);
    const int contentH = g.layout.content.height;
    const int absDelta = std::abs(deltaTop);
    if (contentH <= 0 || absDelta == 0 || absDelta >= contentH) return "";
    if (static_cast<int>(rowCache_.size()) != contentH) return "";

    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, g.layout.content.width - gutterW);
    std::optional<Normalized> sel = selection.has_value() ? normalize(*selection) : std::nullopt;
    std::optional<Normalized> searchSel = searchHighlight.has_value() ? normalize(*searchHighlight) : std::nullopt;
    std::optional<Normalized> bracketOpen, bracketClose;
    normalizeBracketPair(bracketPair, bracketOpen, bracketClose);

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
            renderEditorRow(full, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, docLine, gutterW, textWidth);
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
            renderEditorRow(full, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, docLine, gutterW, textWidth);
            rowCache_.push_front(std::move(full));
        }
        for (int i = 0; i < absDelta; ++i) {
            moveCursorTo(out, i + 1, 1);
            out += theme_.reset;
            out += rowCache_[static_cast<size_t>(i)];
        }
    }

    if (lastCursorLine_ == cursor.line) {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, cursor.line, gutterW, textWidth, contentH);
    } else {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, lastCursorLine_, gutterW, textWidth, contentH);
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose, cursor.line, gutterW, textWidth, contentH);
    }
    patchStatusBar(out, doc, cursor, filename, modified, message, state, g.layout, contentH);

    if (state != State::Busqueda) {
        int curRow = 1, curCol = 1;
        editorCursorPos(doc, cursor, viewport, g, curRow, curCol);
        moveCursorTo(out, curRow, curCol);
        setCursorStyle(out, state);
        showCursor(out);
    }

    updateCacheState(viewport, cursor, doc);
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
                                       const std::optional<Selection>& searchHighlight,
                                       const std::optional<BracketPair>& bracketPair) {
    updateSyntaxLanguage(filename);
    const Layout layout = calculateLayout(viewport.height, viewport.width);
    const int contentH = layout.content.height;

    bool bracketChanged = false;
    if (!hasLastBracketPair_) bracketChanged = true;
    else if (lastBracketPair_ != bracketPair) bracketChanged = true;
    if (!hasCache_ || viewport.width != lastViewportW_ || viewport.height != lastViewportH_
        || cachedContentH_ != contentH || bracketChanged) {
        rebuildCache(doc, cursor, viewport, filename, modified, message, state, selection, searchHighlight, bracketPair);
        lastViewportW_ = viewport.width;
        lastViewportH_ = viewport.height;
        updateCacheState(viewport, cursor, doc);
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
    // Bracket highlighting disables the scroll fast path because the overlay
    // may require repainting either endpoint.
    const bool noHighlight = !selection.has_value() && !searchHighlight.has_value() && !bracketPair.has_value();

    // Fast scroll path:
    // - only one-row vertical scroll;
    // - no selection/search highlight;
    // - viewport width unchanged.
    // Multi-row scrolls deliberately use the diff path because maintaining
    // rowCache_ and repainting entering rows becomes less efficient/complex here.
    if ((deltaTop == 1 || deltaTop == -1) && deltaLeft == 0 && noHighlight) {
        const std::string scrollFrame = buildScrollFrame(doc, cursor, viewport, filename,
                                                         modified, message, state,
                                                         selection, searchHighlight, bracketPair,
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
                                                               modified, message, state);
            if (!moveFrame.empty()) return moveFrame;
        }
    }

    // Camino lento: selección/búsqueda/bracket activa, o los caminos rápidos declinaron.
    const std::string fresh = buildEditorBody(doc, cursor, viewport, filename,
                                              modified, message, state,
                                              selection, searchHighlight, bracketPair);
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
        const auto presentation = statePresentation(theme_, state);
        StatusBarData data = editorBarData(filename, modified, presentation.label, message, cursor, doc.lineCount());
        data.estadoAccent = presentation.accent;
        lastStatusData_ = data;
        hasLastStatusData_ = true;
    }
    lastBracketPair_ = bracketPair;
    hasLastBracketPair_ = true;

    if (state == State::Busqueda) {
        updateCacheState(viewport, cursor, doc);
        return out;
    }
    int curRow = 1, curCol = 1;
    editorCursorPos(doc, cursor, viewport, curRow, curCol);
    moveCursorTo(out, curRow, curCol);
    setCursorStyle(out, state);
    showCursor(out);

    updateCacheState(viewport, cursor, doc);
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

    int rows = std::min(static_cast<int>(names.size()), height);
    if (rows > 0) {
        int cursorRow = std::max(1, std::min(selected + 1, rows));
        moveCursorTo(out, cursorRow, 1);
    }

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
    for (int r = rows; r < area.height; ++r) {
        out += "\x1b[K";
        renderEmptyMarkerRow(out, theme_, area.width);
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
    data.right = total == 0 ? "0/0"
                            : std::to_string(selected - scroll + 1) + "/" + std::to_string(total);
    data.message = message;
    renderStatusBar(out, layout.statusBar, data);

    int rows = std::min(static_cast<int>(names.size()) - scroll, height);
    if (rows > 0) {
        int cursorRow = selected - scroll + 1;
        moveCursorTo(out, cursorRow, 1);
    }

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
            renderEmptyMarkerRow(out, theme_, area.width);
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