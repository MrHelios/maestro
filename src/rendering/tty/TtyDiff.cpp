#include "rendering/tty/TtyDiff.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "rendering/Style.h"
#include "rendering/tty/TtyScroll.h"

void TtyDiff::updateCacheState(const Viewport& viewport, const Cursor& cursor,
                               const Document& doc) {
    lastViewportTop_ = viewport.top;
    lastViewportLeft_ = viewport.left;
    lastCursorLine_ = cursor.line;
    lastCursorCol_ = cursor.col;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
}

void TtyDiff::splitRows(const std::string& body,
                        std::vector<std::string_view>* rows) {
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

void TtyDiff::emitCursor(std::string& out, CellPos pos, bool visible,
                         State state, bool endFrame) {
    // Contrato visual: solo posicionar/mostrar si el cursor esta en
    // viewport. Si esta fuera (rueda con suppressScrollToCursor_) o el modo
    // lo oculta (Busqueda) se deja oculto (hide de beginFrame).
    if (state == State::Busqueda || !visible) return;
    encoder_.moveCursorTo(out, pos);
    encoder_.setCursorStyle(out, state);
    if (endFrame) encoder_.endFrame(out);
    else encoder_.showCursor(out);
}

void TtyDiff::placeCursor(std::string& out, const Document& doc,
                          const Cursor& cursor, const Viewport& viewport,
                          State state, bool endFrame) {
    CellPos pos;
    const bool visible = builder_.editorCursorPos(doc, cursor, viewport, pos);
    emitCursor(out, pos, visible, state, endFrame);
}

void TtyDiff::placeCursor(std::string& out, const Document& doc,
                          const Cursor& cursor, const Viewport& viewport,
                          const FrameBuilder::EditorGeometry& g, State state,
                          bool endFrame) {
    CellPos pos;
    const bool visible = builder_.editorCursorPos(doc, cursor, viewport, g, pos);
    emitCursor(out, pos, visible, state, endFrame);
}

bool TtyDiff::patchContentRow(
    std::string& out, const Document& doc, const Cursor& cursor,
    const Viewport& viewport, const std::optional<Normalized>& sel,
    const std::optional<Normalized>& searchSel,
    const std::optional<Normalized>& bracketOpen,
    const std::optional<Normalized>& bracketClose, int docLine, int gutterW,
    int textWidth, int contentH) {
    const int row = docLine - viewport.top;
    if (row < 0 || row >= contentH ||
        row >= static_cast<int>(rowCache_.size()))
        return false;
    std::string full = encoder_.encodeRow(builder_.buildContentRow(
        doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose,
        docLine, gutterW, textWidth));
    // rowCache_ guarda la secuencia exacta usada para pintar cada fila,
    // incluido el CSI K inicial: la igualdad con `full` basta para saber
    // si la fila necesita repintado.
    if (rowCache_[static_cast<size_t>(row)] == full) return false;
    encoder_.moveCursorToRaw(out, row + 1, 1);
    out += encoder_.theme().reset;
    out += full;
    rowCache_[static_cast<size_t>(row)] = std::move(full);
    return true;
}

void TtyDiff::patchChrome(std::string& out, const Document& doc,
                             const Cursor& cursor, const std::string& filename,
                             bool modified, const Message& message, State state,
                             const Layout& layout, int contentH) {
    auto payload = builder_.buildChrome(filename, modified, message, cursor,
                                        doc.lineCount(), state);
    const ChromeData& data = payload.data;
    if (hasLastChromeData_ && data == lastChromeData_) return;
    std::string chromeBody =
        encoder_.encodeChrome(layout.chrome, data, payload.statusAccent);
    if (chromeBody == chromeCache_) {
        lastChromeData_ = data;
        hasLastChromeData_ = true;
        return;
    }

    std::vector<std::string_view> oldRows, newRows;
    splitRows(chromeCache_, &oldRows);
    splitRows(chromeBody, &newRows);
    const size_t n = std::max(oldRows.size(), newRows.size());
    for (size_t i = 0; i < n; ++i) {
        std::string_view oldRow =
            i < oldRows.size() ? oldRows[i] : std::string_view{};
        std::string_view newRow =
            i < newRows.size() ? newRows[i] : std::string_view{};
        if (oldRow == newRow) continue;
        encoder_.moveCursorToRaw(out, contentH + static_cast<int>(i) + 1, 1);
        out += encoder_.theme().reset;
        out += "\x1b[K";
        out.append(newRow.data(), newRow.size());
    }
    chromeCache_ = std::move(chromeBody);
    lastChromeData_ = data;
    hasLastChromeData_ = true;
}

void TtyDiff::rebuildCache(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    builder_.updateSyntaxLanguage(filename);
    const FrameBuilder::EditorGeometry g =
        builder_.editorGeometry(doc, viewport);
    const int contentH = g.layout.content.height;
    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, g.layout.content.width - gutterW);
    std::optional<Normalized> sel =
        selection.has_value() ? normalize(*selection) : std::nullopt;
    std::optional<Normalized> searchSel =
        searchHighlight.has_value() ? normalize(*searchHighlight)
                                    : std::nullopt;
    std::optional<Normalized> bracketOpen, bracketClose;
    FrameBuilder::normalizeBracketPair(bracketPair, bracketOpen, bracketClose);

    // INCONDICIONAL (igual que el viejo Renderer::rebuildCache): dimensiona
    // el SyntaxCache (ensureSize) y parsea el viewport. Sin esto, con el
    // cache frío isValidThrough() da true espurio (clampa a size()==0) y el
    // heal multilínea posterior nunca corre.
    {
        SyntaxCache& cache = builder_.activeCache();
        int need = viewport.top + contentH;
        if (need > doc.lineCount()) need = doc.lineCount();
        if (need > 0) cache.ensureValid(doc, need);
    }

    rowCache_.clear();
    for (int r = 0; r < contentH; ++r) {
        int dl = viewport.top + r;
        rowCache_.push_back(encoder_.encodeRow(builder_.buildContentRow(
            doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose,
            dl, gutterW, textWidth)));
    }

    auto payload = builder_.buildChrome(filename, modified, message, cursor,
                                        doc.lineCount(), state);
    chromeCache_ =
        encoder_.encodeChrome(g.layout.chrome, payload.data, payload.statusAccent);
    lastChromeData_ = payload.data;
    hasLastChromeData_ = true;

    hasCache_ = true;
    cachedContentH_ = contentH;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
    lastBracketPair_ = bracketPair;
    hasLastBracketPair_ = true;
}

std::string TtyDiff::buildCursorMoveFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state) {
    builder_.updateSyntaxLanguage(filename);
    const FrameBuilder::EditorGeometry g =
        builder_.editorGeometry(doc, viewport);
    const Layout& layout = g.layout;
    const int contentH = layout.content.height;
    if (contentH <= 0 || static_cast<int>(rowCache_.size()) != contentH)
        return "";

    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, layout.content.width - gutterW);
    const std::optional<Normalized> sel = std::nullopt;
    const std::optional<Normalized> searchSel = std::nullopt;
    const std::optional<Normalized> bracketOpen = std::nullopt;
    const std::optional<Normalized> bracketClose = std::nullopt;

    std::string out;
    encoder_.hideCursor(out);
    if (lastCursorLine_ == cursor.line) {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen,
                        bracketClose, cursor.line, gutterW, textWidth, contentH);
    } else {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen,
                        bracketClose, lastCursorLine_, gutterW, textWidth,
                        contentH);
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen,
                        bracketClose, cursor.line, gutterW, textWidth, contentH);
    }
    // La edición pudo propagar estado multilínea (/*, raw string) más allá
    // de la fila del cursor: el patch de una sola fila deja el viewport con
    // spans stale. Si el viewport sigue sin validez, completarlo (acotado
    // por viewport, nunca n) y recalcular/comparar todas las filas del
    // viewport, emitiendo al terminal solamente las que cambiaron
    // (patchContentRow compara contra rowCache_). Caso neutro/edit de una
    // línea: ya está válido y se conserva el O(1) sin entrar al loop.
    {
        SyntaxCache& cache = builder_.activeCache();
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
    patchChrome(out, doc, cursor, filename, modified, message, state, layout,
                   contentH);

    // Contrato visual: solo posicionar/mostrar si el cursor esta en viewport.
    // Si esta fuera (rueda con suppressScrollToCursor_) se deja oculto.
    placeCursor(out, doc, cursor, viewport, g, state, /*endFrame=*/false);
    lastCursorLine_ = cursor.line;
    lastCursorCol_ = cursor.col;
    lastVersion_ = doc.version();
    lastLineCount_ = doc.lineCount();
    return out;
}

// Precondition: rowCache_[i] corresponds to document line viewport.top + i
// and rowCache_.size() == contentH. This function preserves that invariant
// while scrolling by one or more rows.
std::string TtyDiff::buildScrollFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair, int deltaTop) {
    builder_.updateSyntaxLanguage(filename);
    const FrameBuilder::EditorGeometry g =
        builder_.editorGeometry(doc, viewport);
    const int contentH = g.layout.content.height;
    const TtyScrollOp scrollOp = scrollOpFor(contentH, deltaTop);
    const int absDelta = scrollOp.absDelta;
    if (!scrollOp.useRegion) return "";
    assert(static_cast<int>(rowCache_.size()) == contentH);
    if (static_cast<int>(rowCache_.size()) != contentH) return "";

    const int gutterW = g.gutterW;
    const int textWidth = std::max(0, g.layout.content.width - gutterW);
    std::optional<Normalized> sel =
        selection.has_value() ? normalize(*selection) : std::nullopt;
    std::optional<Normalized> searchSel =
        searchHighlight.has_value() ? normalize(*searchHighlight)
                                    : std::nullopt;
    std::optional<Normalized> bracketOpen, bracketClose;
    FrameBuilder::normalizeBracketPair(bracketPair, bracketOpen, bracketClose);

    auto encodeEnteringRow = [&](int docLine) {
        return encoder_.encodeRow(builder_.buildContentRow(
            doc, cursor, viewport, sel, searchSel, bracketOpen, bracketClose,
            docLine, gutterW, textWidth));
    };

    std::string out;
    encoder_.hideCursor(out);
    out += scrollRegionPrefix(contentH, scrollOp);

    if (deltaTop > 0) {
        for (int i = 0; i < absDelta; ++i) rowCache_.pop_front();
        for (int i = 0; i < absDelta; ++i) {
            const int docLine = viewport.top + contentH - absDelta + i;
            rowCache_.push_back(encodeEnteringRow(docLine));
        }
        for (int i = 0; i < absDelta; ++i) {
            const int row = contentH - absDelta + i;
            encoder_.moveCursorToRaw(out, row + 1, 1);
            out += encoder_.theme().reset;
            out += rowCache_[static_cast<size_t>(row)];
        }
    } else {
        for (int i = 0; i < absDelta; ++i) rowCache_.pop_back();
        for (int i = 0; i < absDelta; ++i) {
            const int docLine = viewport.top + absDelta - 1 - i;
            rowCache_.push_front(encodeEnteringRow(docLine));
        }
        for (int i = 0; i < absDelta; ++i) {
            encoder_.moveCursorToRaw(out, i + 1, 1);
            out += encoder_.theme().reset;
            out += rowCache_[static_cast<size_t>(i)];
        }
    }

    if (lastCursorLine_ == cursor.line) {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen,
                        bracketClose, cursor.line, gutterW, textWidth, contentH);
    } else {
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen,
                        bracketClose, lastCursorLine_, gutterW, textWidth,
                        contentH);
        patchContentRow(out, doc, cursor, viewport, sel, searchSel, bracketOpen,
                        bracketClose, cursor.line, gutterW, textWidth, contentH);
    }
    patchChrome(out, doc, cursor, filename, modified, message, state,
                   g.layout, contentH);

    placeCursor(out, doc, cursor, viewport, g, state, /*endFrame=*/false);

    updateCacheState(viewport, cursor, doc);
    return out;
}

std::string TtyDiff::buildDiffFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    const bool langChanged = builder_.updateSyntaxLanguage(filename);
    const Layout layout =
        builder_.calculateLayout(viewport.height, viewport.width);
    const int contentH = layout.content.height;

    bool bracketChanged = false;
    if (!hasLastBracketPair_) bracketChanged = true;
    else if (lastBracketPair_ != bracketPair) bracketChanged = true;
    if (!hasCache_ || langChanged || viewport.width != lastViewportW_ ||
        viewport.height != lastViewportH_ || cachedContentH_ != contentH ||
        bracketChanged) {
        rebuildCache(doc, cursor, viewport, filename, modified, message, state,
                     selection, searchHighlight, bracketPair);
        lastViewportW_ = viewport.width;
        lastViewportH_ = viewport.height;
        updateCacheState(viewport, cursor, doc);
        std::string out;
        encoder_.beginFrame(out);
        for (auto& row : rowCache_) {
            out += row;
            out += "\r\n";
        }
        out += chromeCache_;
        if (state == State::Busqueda) return out;
        {
            placeCursor(out, doc, cursor, viewport, state, /*endFrame=*/true);
            // Si esta fuera del viewport se deja oculto (sin show): respeta
            // el contrato visible==false sin clampar al borde.
        }
        return out;
    }

    const int deltaTop = viewport.top - lastViewportTop_;
    const int deltaLeft = viewport.left - lastViewportLeft_;
    // Bracket highlighting disables the scroll fast path because the overlay
    // may require repainting either endpoint.
    const bool noHighlight = !selection.has_value() &&
                             !searchHighlight.has_value() &&
                             !bracketPair.has_value();

    // Fast scroll path: only one-row vertical scroll, no
    // selection/search highlight, viewport width unchanged. Multi-row
    // scrolls deliberately use the diff path.
    if ((deltaTop == 1 || deltaTop == -1) && deltaLeft == 0 && noHighlight) {
        const std::string scrollFrame = buildScrollFrame(
            doc, cursor, viewport, filename, modified, message, state,
            selection, searchHighlight, bracketPair, deltaTop);
        if (!scrollFrame.empty()) return scrollFrame;
    }

    if (deltaTop == 0 && deltaLeft == 0 && noHighlight) {
        const bool sameLineEdit =
            doc.version() != lastVersion_ && cursor.line == lastCursorLine_ &&
            cursor.col != lastCursorCol_ && doc.lineCount() == lastLineCount_;
        const bool pureCursorMove =
            doc.version() == lastVersion_ &&
            (cursor.line != lastCursorLine_ || cursor.col != lastCursorCol_);
        if (sameLineEdit || pureCursorMove) {
            const std::string moveFrame = buildCursorMoveFrame(
                doc, cursor, viewport, filename, modified, message, state);
            if (!moveFrame.empty()) return moveFrame;
        }
    }

    // Camino lento: selección/búsqueda/bracket activa, o los caminos rápidos
    // declinaron. Codifica el Frame fresco y emite solo las filas que
    // difieren contra los caches (el doble "\x1b[K" intencional del camino
    // original se preserva: las filas nuevas ya traen el suyo y el rewrite
    // puntual agrega otro clear).
    Frame fresh = builder_.buildFrame(doc, cursor, viewport, filename, modified,
                                      message, state, selection,
                                      searchHighlight, bracketPair);
    std::vector<std::string> newRows;
    newRows.reserve(fresh.contentRows.size() + 2);
    for (const auto& r : fresh.contentRows)
        newRows.push_back(encoder_.encodeRow(r));
    const std::string freshChrome = encoder_.encodeChrome(
        fresh.layout.chrome, fresh.chrome, fresh.statusAccent);
    std::vector<std::string_view> newChromeRows;
    splitRows(freshChrome, &newChromeRows);
    for (const auto& sr : newChromeRows)
        newRows.emplace_back(sr.data(), sr.size());

    std::vector<std::string_view> oldChromeRows;
    splitRows(chromeCache_, &oldChromeRows);
    std::string out;
    encoder_.hideCursor(out);
    for (size_t i = 0; i < newRows.size(); ++i) {
        std::string_view oldRow;
        if (i < static_cast<size_t>(contentH)) {
            if (i < rowCache_.size()) oldRow = rowCache_[i];
        } else if (i - static_cast<size_t>(contentH) < oldChromeRows.size()) {
            oldRow = oldChromeRows[i - static_cast<size_t>(contentH)];
        }
        if (oldRow == newRows[i]) continue;
        encoder_.moveCursorToRaw(out, static_cast<int>(i) + 1, 1);
        out += encoder_.theme().reset;
        out += "\x1b[K";
        out += newRows[i];
    }

    rowCache_.clear();
    for (int i = 0; i < contentH && static_cast<size_t>(i) < newRows.size(); ++i)
        rowCache_.emplace_back(newRows[static_cast<size_t>(i)]);
    chromeCache_.clear();
    for (size_t i = static_cast<size_t>(contentH); i < newRows.size(); ++i) {
        if (i > static_cast<size_t>(contentH)) chromeCache_ += "\r\n";
        chromeCache_ += newRows[i];
    }
    lastChromeData_ = fresh.chrome;
    hasLastChromeData_ = true;
    lastBracketPair_ = bracketPair;
    hasLastBracketPair_ = true;

    if (state == State::Busqueda) {
        updateCacheState(viewport, cursor, doc);
        return out;
    }
    {
        placeCursor(out, doc, cursor, viewport, state, /*endFrame=*/false);
    }

    updateCacheState(viewport, cursor, doc);
    return out;
}
