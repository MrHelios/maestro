#include "rendering/frame/FrameBuilder.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "base/utf8.h"
#include "diagnostics/Instrument.h"
#include "layout/Gutter.h"
#include "rendering/RenderUtil.h"
#include "syntax/SyntaxLanguage.h"

namespace {

// Solo para MOSTRAR en la barra de estado: reemplaza el home del usuario
// por "~" al inicio de la ruta (estilo shell). NO toca filename real.
std::string collapseHome(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return path;
    std::string h(home);
    if (path.size() < h.size() || path.compare(0, h.size(), h) != 0)
        return path;
    if (path.size() > h.size() && path[h.size()] != '/')
        return path;
    return "~" + path.substr(h.size());
}

// Arma el StatusBarData del Editor. PURO: `estadoAccent` viaja vacío; el
// accent vive en Frame::statusAccent como StyleRole y lo resuelve el backend.
StatusBarData editorBarData(const std::string& filename, bool modified,
                            const std::string& estado,
                            const Message& message,
                            const Cursor& cursor, int totalLines) {
    StatusBarData data;
    data.name = chrome::baseName(filename);
    if (data.name.empty()) data.name = "[sin nombre]";
    data.path = collapseHome(chrome::dirName(filename));
    if (data.path == ".") data.path = "";
    data.estado = estado;
    data.modified = modified;
    data.message = message;
    data.cursorLine = cursor.line;
    data.cursorCol = cursor.col;
    data.totalLines = totalLines;
    data.estadoAccent = ""; // el backend lo resuelve desde statusAccent + Theme
    return data;
}

// Celda de número de línea agregada a `out` (sin temporaries): número
// alineado a la derecha + espacio de separación, con la misma cota de ancho
// del viejo renderGutterCell (conserva la COLA del número).
void appendGutterCell(std::string& out, int lineNumber1Based, int gutterW) {
    if (gutterW <= 0) return;
    std::string numStr = std::to_string(lineNumber1Based); // SSO, sin heap
    const int maxNumCols = std::max(0, gutterW - 1);
    if (static_cast<int>(numStr.size()) > maxNumCols)
        numStr = numStr.substr(numStr.size() - static_cast<size_t>(maxNumCols));
    const int pad = std::max(0, gutterW - 1 - static_cast<int>(numStr.size()));
    out.append(static_cast<size_t>(pad), ' ');
    out += numStr;
    out += ' ';
}

// Referencia a un tramo ya agregado a la arena (offsets, no punteros: las
// agregaciones posteriores pueden realocar sin invalidarla).
struct SegRef {
    size_t start;
    size_t len;
    StyleRole role;
};

// Emisor de la fila: acumula SegRefs y los materializa como vistas sobre la
// arena final en el destructor (cubre todos los returns). Tras el commit la
// arena no vuelve a crecer, así que las vistas quedan estables.
struct RowEmit {
    StyledRow& row;
    SmallVec<SegRef, 8> refs;
    explicit RowEmit(StyledRow& r) : row(r) {}
    ~RowEmit() {
        for (const auto& ref : refs)
            row.segs.push_back(FrameSegment{
                std::string_view(row.arena.data() + ref.start, ref.len),
                ref.role});
    }
    void emitAt(size_t start, StyleRole role) {
        refs.push_back(SegRef{start, row.arena.size() - start, role});
    }
    void emitPiece(std::string_view piece, StyleRole role) {
        refs.push_back(SegRef{
            static_cast<size_t>(piece.data() - row.arena.data()),
            piece.size(), role});
    }
};

} // namespace

StyleRole FrameBuilder::syntaxRoleFor(SyntaxToken tok) {
    switch (tok) {
        case SyntaxToken::Keyword:      return StyleRole::SyntaxKeyword;
        case SyntaxToken::Type:         return StyleRole::SyntaxType;
        case SyntaxToken::Preprocessor: return StyleRole::SyntaxPreprocessor;
        case SyntaxToken::String:       return StyleRole::SyntaxString;
        case SyntaxToken::Character:    return StyleRole::SyntaxCharacter;
        case SyntaxToken::Number:       return StyleRole::SyntaxNumber;
        case SyntaxToken::Comment:      return StyleRole::SyntaxComment;
    }
    return StyleRole::SyntaxKeyword;
}

std::string FrameBuilder::stateLabelFor(State state) {
    switch (state) {
        case State::Navegacion:     return "NAVEGACION";
        case State::Interaccion:    return "INTERACCION";
        case State::Seleccion:      return "SELECCION";
        case State::Prefix:         return "COMANDO";
        case State::BufferSelector: return "BUFFERS";
        case State::SaveAs:         return "GUARDAR";
        case State::FileBrowser:    return "ABRIR";
        case State::Busqueda:       return "BUSQUEDA";
        case State::IrAFila:        return "IR A FILA";
    }
    return "";
}

void FrameBuilder::normalizeBracketPair(const std::optional<BracketPair>& pair,
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

SyntaxState FrameBuilder::syntaxStateAt(const Document& doc, int targetLine) const {
    if (targetLine <= 0) return SyntaxState{};
    int t = std::min(targetLine, doc.lineCount());
    auto& cache = activeCache();
    const_cast<SyntaxCache&>(cache).ensureValid(doc, t);
    return cache.stateBefore(t);
}

bool FrameBuilder::updateSyntaxLanguage(const std::string& filename) const {
    auto lang = languageFromFilename(filename);
    bool changed = false;
    if (lang != syntaxHighlighter_.language()) {
        syntaxHighlighter_.setLanguage(lang);
        changed = true;
    }
    auto& cache = activeCache();
    if (cache.language() != lang) {
        const_cast<SyntaxCache&>(cache).setLanguage(lang);
    }
    if (externalCache_ && syntaxCache_.language() != lang) {
        syntaxCache_.setLanguage(lang);
    }
    return changed;
}

Layout FrameBuilder::calculateLayout(int contentRows, int width) const {
    return computeLayout(contentRows + kStatusBarRows, width);
}

FrameBuilder::EditorGeometry FrameBuilder::editorGeometry(
    const Document& doc, const Viewport& viewport) const {
    EditorGeometry g;
    g.layout = calculateLayout(viewport.height, viewport.width);
    g.gutterW = gutterWidth(doc.lineCount(), viewport.width);
    return g;
}

void FrameBuilder::editorCursorPos(const Document& doc,
                                   const Cursor& cursor,
                                   const Viewport& viewport,
                                   int& outRow, int& outCol) const {
    const EditorGeometry g = editorGeometry(doc, viewport);
    editorCursorPos(doc, cursor, viewport, g, outRow, outCol);
}

void FrameBuilder::editorCursorPos(const Document& doc,
                                   const Cursor& cursor,
                                   const Viewport& viewport,
                                   const EditorGeometry& g,
                                   int& outRow, int& outCol) const {
    outRow = cursor.line - viewport.top + 1;
    int absoluteCol = cursor.visualColumn(doc);
    int visibleCol = absoluteCol - viewport.left;
    outCol = g.gutterW + visibleCol + 1 + g.layout.content.col;
    const int rowLo = g.layout.content.row + 1;
    const int rowHi = rowLo + g.layout.content.height - 1;
    const int colLo = g.layout.content.col + 1;
    const int colHi = colLo + std::max(1, g.layout.content.width) - 1;
    outRow = std::clamp(outRow, rowLo, rowHi);
    outCol = std::clamp(outCol, colLo, colHi);
}

StyledRow FrameBuilder::buildContentRow(
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
    instrument::ScopedTimer _t(&instrument::current.renderEditorRow_nanos);
    if (instrument::enabled) instrument::onRenderEditorRow();
    StyledRow row;
    const bool isCurrentLine = (docLine == cursor.line);
    row.isCurrentLine = isCurrentLine && docLine < doc.lineCount();
    // Una sola reserva por fila (1 alloc): gutter + bytes visibles (UTF-8 y
    // tabs expanden más allá de las columnas) + pad. Los segmentos se
    // registran como offsets y RowEmit los vuelve vistas al final.
    row.arena.reserve(static_cast<size_t>(
        std::max(0, gutterW) + (std::max(0, textWidth) + 16) * 4 + 32));
    RowEmit emit(row);

    if (docLine >= doc.lineCount()) {
        size_t s = row.arena.size();
        row.arena.append(static_cast<size_t>(std::max(0, gutterW)), ' ');
        emit.emitAt(s, StyleRole::GutterBlank);
        if (textWidth > 0) {
            s = row.arena.size();
            row.arena += '~';
            emit.emitAt(s, StyleRole::Marker);
        }
        return row;
    }

    auto& cache = activeCache();
    if (!cache.isValidThrough(docLine + 1))
        const_cast<SyntaxCache&>(cache).ensureValid(doc, docLine + 1);
    const auto& spans = cache.spansFor(docLine);

    const std::string& line = doc.lineAt(docLine);

    std::pair<int, int> intervals[2];
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

    std::pair<int, int> bracketIntervals[2];
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

    const StyleRole gutterRole =
        isCurrentLine ? StyleRole::GutterCurrent : StyleRole::Gutter;

    if (line.empty() && lineBreakSelected) {
        size_t s = row.arena.size();
        appendGutterCell(row.arena, docLine + 1, gutterW);
        emit.emitAt(s, gutterRole);
        s = row.arena.size();
        row.arena.append(static_cast<size_t>(std::max(0, textWidth)), ' ');
        emit.emitAt(s, StyleRole::Selection);
        return row;
    }

    if (intervalCount == 2 && intervals[0] > intervals[1])
        std::swap(intervals[0], intervals[1]);
    std::pair<int, int> merged[2];
    int mergedCount = 0;
    for (int i = 0; i < intervalCount; ++i) {
        auto p = intervals[i];
        if (p.first >= p.second) continue;
        if (mergedCount == 0 || p.first > merged[mergedCount - 1].second)
            merged[mergedCount++] = p;
        else
            merged[mergedCount - 1].second =
                std::max(merged[mergedCount - 1].second, p.second);
    }

    {
        size_t s = row.arena.size();
        appendGutterCell(row.arena, docLine + 1, gutterW);
        emit.emitAt(s, gutterRole);
    }

    int absoluteVisStart = viewport.left;
    int absoluteVisEnd = absoluteVisStart + textWidth;
    instrument::setTag(instrument::Utf8Tag::RangeVisibleRaw);
    std::string_view visibleRaw = utf8::range(line, absoluteVisStart, absoluteVisEnd);
    instrument::clearTag();
    instrument::setTag(instrument::Utf8Tag::ExpandTabs);
    const size_t vstart = row.arena.size();
    utf8::expandTabsInto(row.arena, visibleRaw);
    instrument::clearTag();
    // Vista sobre la arena: válida hasta el próximo append, y todos los usos
    // están antes del relleno final (los segmentos se guardan como offsets).
    const std::string_view visible(row.arena.data() + vstart,
                                   row.arena.size() - vstart);

    std::pair<int, int> visibleSel[2];
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
        if (visibleSc < visibleEc)
            visibleSel[visibleSelCount++] = {visibleSc, visibleEc};
    }

    std::pair<int, int> visibleBracket[2];
    int visibleBracketCount = 0;
    for (int i = 0; i < bracketCount; ++i) {
        auto p = bracketIntervals[i];
        instrument::setTag(instrument::Utf8Tag::ColumnOfBracket);
        int absoluteSc = utf8::columnOf(line, p.first);
        int absoluteEc = utf8::columnOf(line, p.second);
        instrument::clearTag();
        if (absoluteEc <= absoluteVisStart || absoluteSc >= absoluteVisEnd) continue;
        int visibleSc = std::max(absoluteSc, absoluteVisStart) - absoluteVisStart;
        int visibleEc = std::min(absoluteEc, absoluteVisEnd) - absoluteVisStart;
        if (visibleSc < visibleEc)
            visibleBracket[visibleBracketCount++] = {visibleSc, visibleEc};
    }

    struct SynVis {
        int s;
        int e;
        SyntaxToken tok;
    };
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
        // Camino rápido (viejo renderPlainLine): una sola envoltura.
        if (isCurrentLine) {
            instrument::setTag(instrument::Utf8Tag::TruncateFilled);
            std::string truncated = utf8::truncate(visible, textWidth);
            instrument::clearTag();
            instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
            int cc = chrome::colCount(truncated);
            instrument::clearTag();
            size_t s = row.arena.size();
            row.arena.append(truncated);
            for (int c = cc; c < textWidth; ++c) row.arena += ' ';
            emit.emitAt(s, StyleRole::CurrentLine);
        } else {
            instrument::setTag(instrument::Utf8Tag::RangeFilled);
            std::string_view v = utf8::range(visible, 0, textWidth);
            instrument::clearTag();
            emit.emitPiece(v, StyleRole::Default);
        }
        return row;
    }

    std::vector<int> bounds;
    bounds.reserve(2 + visibleSelCount * 2 + visibleBracketCount * 2 +
                   synVis.size() * 2 + 2);
    bounds.push_back(0);
    bounds.push_back(textWidth);
    for (int i = 0; i < visibleSelCount; ++i) {
        bounds.push_back(visibleSel[i].first);
        bounds.push_back(visibleSel[i].second);
    }
    for (int i = 0; i < visibleBracketCount; ++i) {
        bounds.push_back(visibleBracket[i].first);
        bounds.push_back(visibleBracket[i].second);
    }
    for (auto& sv : synVis) {
        bounds.push_back(sv.s);
        bounds.push_back(sv.e);
    }
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    std::vector<int> clipped;
    clipped.reserve(bounds.size());
    for (int v : bounds)
        if (v >= 0 && v <= textWidth) clipped.push_back(v);
    bounds.swap(clipped);
    if (bounds.empty()) {
        bounds.push_back(0);
        bounds.push_back(textWidth);
    }

    int used = 0;
    for (size_t i = 0; i + 1 < bounds.size(); ++i) {
        int segS = bounds[i];
        int segE = bounds[i + 1];
        if (segS >= segE) continue;
        if (segS >= textWidth) break;
        if (segE > textWidth) segE = textWidth;
        bool inSel = false;
        for (int k = 0; k < visibleSelCount; ++k)
            if (segS >= visibleSel[k].first && segS < visibleSel[k].second) {
                inSel = true;
                break;
            }
        bool inBracket = false;
        if (!inSel)
            for (int k = 0; k < visibleBracketCount; ++k)
                if (segS >= visibleBracket[k].first &&
                    segS < visibleBracket[k].second) {
                    inBracket = true;
                    break;
                }
        instrument::setTag(instrument::Utf8Tag::RangeSegments);
        std::string_view seg = utf8::range(visible, segS, segE);
        instrument::clearTag();
        instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
        bool beyondContent = seg.empty() && segS >= chrome::colCount(visible);
        instrument::clearTag();
        if (beyondContent) continue;
        if (seg.empty()) continue;
        if (inSel) {
            emit.emitPiece(seg, StyleRole::Selection);
            instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
            used += chrome::colCount(seg);
            instrument::clearTag();
        } else if (inBracket) {
            emit.emitPiece(seg, StyleRole::BracketMatch);
            instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
            used += chrome::colCount(seg);
            instrument::clearTag();
        } else {
            SyntaxToken tok = SyntaxToken::Keyword;
            bool hasSyn = false;
            for (auto& sv : synVis)
                if (segS >= sv.s && segS < sv.e) {
                    hasSyn = true;
                    tok = sv.tok;
                    break;
                }
            if (hasSyn) {
                emit.emitPiece(seg, syntaxRoleFor(tok));
            } else {
                emit.emitPiece(seg, isCurrentLine ? StyleRole::CurrentLine
                                                 : StyleRole::Default);
            }
            instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
            used += chrome::colCount(seg);
            instrument::clearTag();
        }
    }
    instrument::setTag(instrument::Utf8Tag::ColumnOfFilled);
    int visW = chrome::colCount(visible);
    instrument::clearTag();
    if (used < visW) used = visW;
    if (isCurrentLine && used < textWidth) {
        size_t s = row.arena.size();
        row.arena.append(static_cast<size_t>(textWidth - used), ' ');
        emit.emitAt(s, StyleRole::CurrentLine);
    }
    return row;
}

FrameBuilder::StatusPayload FrameBuilder::buildStatus(
    const std::string& filename, bool modified, const Message& message,
    const Cursor& cursor, int totalLines, State state) const {
    StatusPayload p;
    p.data = editorBarData(filename, modified, stateLabelFor(state), message,
                           cursor, totalLines);
    p.accent = accentRoleFor(state);
    return p;
}

Frame FrameBuilder::buildFrame(    const Document& doc,
    const Cursor& cursor,
    const Viewport& viewport,
    const std::string& filename,
    bool modified,
    const Message& message,
    State state,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) const {
    if (instrument::enabled) {
        instrument::resetFrame();
        instrument::onBuildScreen();
    }
    instrument::ScopedTimer _t_buildScreen(&instrument::current.buildScreen_nanos);
    instrument::ScopedTimer _t_body(&instrument::current.buildEditorBody_nanos);
    if (instrument::enabled) instrument::onBuildEditorBody();

    updateSyntaxLanguage(filename);

    std::optional<Normalized> sel =
        selection.has_value() ? normalize(*selection) : std::nullopt;
    std::optional<Normalized> searchSel =
        searchHighlight.has_value() ? normalize(*searchHighlight) : std::nullopt;
    std::optional<Normalized> bracketOpen, bracketClose;
    normalizeBracketPair(bracketPair, bracketOpen, bracketClose);

    const EditorGeometry g = editorGeometry(doc, viewport);
    const int contentH = g.layout.content.height;
    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, g.layout.content.width - gutterW);

    Frame f;
    f.layout = g.layout;
    f.gutterW = gutterW;
    {
        auto& cache = activeCache();
        int need = viewport.top + contentH;
        if (need > doc.lineCount()) need = doc.lineCount();
        if (need > 0 && !cache.isValidThrough(need))
            const_cast<SyntaxCache&>(cache).ensureValid(doc, need);
    }
    f.contentRows.reserve(static_cast<size_t>(std::max(0, contentH)));
    for (int r = 0; r < contentH; ++r) {
        int docLine = viewport.top + r;
        f.contentRows.push_back(buildContentRow(doc, cursor, viewport, sel,
                                                searchSel, bracketOpen,
                                                bracketClose, docLine, gutterW,
                                                textWidth));
    }
    auto payload = buildStatus(filename, modified, message, cursor,
                               doc.lineCount(), state);
    f.status = payload.data;
    f.statusAccent = payload.accent;
    f.cursor.state = state;
    f.cursor.visible = (state != State::Busqueda);
    editorCursorPos(doc, cursor, viewport, g, f.cursor.row, f.cursor.col);
    return f;
}
