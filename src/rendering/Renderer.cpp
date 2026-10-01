#include "rendering/Renderer.h"

#include <algorithm>
#include <cstdlib>

#include "base/utf8.h"
#include "diagnostics/Instrument.h"
#include "rendering/RenderUtil.h"
#include "rendering/tty/Theme.h"
#include "rendering/tty/TtyDiff.h"
#include "rendering/tty/TtyEncoder.h"

Renderer::Renderer()
    : encoder_(std::make_unique<TtyEncoder>()),
      diff_(std::make_unique<TtyDiff>()) {}

Renderer::~Renderer() = default;

void Renderer::setTheme(const Theme& t) {
    encoder_->setTheme(t);
    diff_->setTheme(t);
}

const Theme& Renderer::theme() const { return encoder_->theme(); }

void Renderer::invalidateCache() { diff_->invalidateCache(); }

bool Renderer::hasCache() const { return diff_->hasCache(); }

int Renderer::lastViewportH() const { return diff_->lastViewportH(); }

void Renderer::beginFrame(std::string& out) const { encoder_->beginFrame(out); }

void Renderer::endFrame(std::string& out) const { encoder_->endFrame(out); }

void Renderer::hideCursor(std::string& out) const {
    encoder_->hideCursor(out);
}

void Renderer::showCursor(std::string& out) const {
    encoder_->showCursor(out);
}

void Renderer::setCursorStyle(std::string& out, State state) const {
    encoder_->setCursorStyle(out, state);
}

void Renderer::setCursorStyle(std::string& out, FrameCursorShape shape) const {
    encoder_->setCursorStyle(out, shape);
}

void Renderer::moveCursorTo(std::string& out, int row, int col) const {
    encoder_->moveCursorTo(out, row, col);
}

namespace {

// Helpers de filas para las pantallas de listas (TTY directo; pendientes de
// extracción a rendering/tty/). Copiados del viejo Renderer sin cambios.
using namespace chrome;

void renderFilledRow(std::string& out, std::string_view text, int width,
                     const std::string& bgStyle, const std::string& reset) {
    instrument::ScopedTimer _t(instrument::renderFilledRowTimer());
    if (instrument::isEnabled()) instrument::onRenderFilledRow();
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

} // namespace

void Renderer::setExternalSyntaxCache(SyntaxCache* c) {
    frameBuilder_.setExternalSyntaxCache(c);
    diff_->setExternalSyntaxCache(c);
}

std::string Renderer::buildScreen(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    Frame f = frameBuilder_.buildFrame(doc, cursor, viewport, filename,
                                       modified, message, state, selection,
                                       searchHighlight, bracketPair);
    std::string out;
    encoder_->appendFrame(out, f);
    return out;
}

void Renderer::renderScreen(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, Sink& sink,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    std::string buffer = buildScreen(doc, cursor, viewport, filename, modified,
                                     message, state, selection,
                                     searchHighlight, bracketPair);
    sink.writeStdout(buffer);
}

void Renderer::renderScreenDiff(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, Sink& sink,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    const std::string out =
        buildDiffFrame(doc, cursor, viewport, filename, modified, message,
                       state, selection, searchHighlight, bracketPair);
    if (!sink.writeStdout(out)) diff_->invalidateCache();
}

std::string Renderer::buildDiffFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    return diff_->buildDiffFrame(doc, cursor, viewport, filename, modified,
                                 message, state, selection, searchHighlight,
                                 bracketPair);
}

void Renderer::renderEditorContent(
    std::string& out, const Document& doc, const Cursor& cursor,
    const Viewport& viewport, const std::optional<Normalized>& sel,
    const Rect& area, int gutterW) const {
    instrument::ScopedTimer _t(instrument::renderEditorContentTimer());
    if (instrument::isEnabled()) instrument::onRenderEditorContent();
    int textWidth = std::max(0, area.width - gutterW);
    for (int row = 0; row < area.height; ++row) {
        int docLine = viewport.top + row;
        out += encoder_->encodeRow(frameBuilder_.buildContentRow(
            doc, cursor, viewport, sel, std::nullopt, std::nullopt,
            std::nullopt, docLine, gutterW, textWidth));
        out += "\r\n";
    }
}

void Renderer::renderStatusBar(std::string& out, const Rect& area,
                               const StatusBarData& data) const {
    out += encoder_->encodeStatus(area, data, StyleRole::StatusAccentDefault);
}

std::string Renderer::buildBufferListScreen(
    const std::vector<std::string>& names, int selected, int width,
    int height) {
    std::string out;
    encoder_->beginFrame(out);

    Layout layout = frameBuilder_.calculateLayout(height, width);
    renderBufferListContent(out, names, selected, layout.content);

    StatusBarData data;
    data.name = "Buffers";
    data.estado = "SELECCIONAR";
    data.estadoAccent = encoder_->theme().accentBuffers;
    const int total = static_cast<int>(names.size());
    data.right = std::to_string(std::min(selected + 1, total)) + "/" +
                 std::to_string(total);
    renderStatusBar(out, layout.statusBar, data);

    int rows = std::min(static_cast<int>(names.size()), height);
    if (rows > 0) {
        int cursorRow = std::max(1, std::min(selected + 1, rows));
        encoder_->moveCursorTo(out, cursorRow, 1);
    }

    encoder_->endFrame(out);
    return out;
}

void Renderer::renderBufferListContent(
    std::string& out, const std::vector<std::string>& names, int selected,
    const Rect& area) const {
    const Theme& T = encoder_->theme();
    int rows = 0;
    for (size_t i = 0; i < names.size() && rows < area.height; ++i, ++rows) {
        encoder_->appendClearLine(out);
        std::string line = "  " + names[i];
        bool isSelected = (static_cast<int>(i) == selected);
        renderFilledRow(out, line, area.width,
                        isSelected ? T.listSelected : "", T.reset);
        out += "\r\n";
    }
    for (int r = rows; r < area.height; ++r) {
        encoder_->appendClearLine(out);
        renderEmptyMarkerRow(out, T, area.width);
        out += "\r\n";
    }
}

void Renderer::renderBufferList(const std::vector<std::string>& names,
                                int selected, int width, int height,
                                Sink& sink) {
    diff_->invalidateCache();
    std::string buffer = buildBufferListScreen(names, selected, width, height);
    sink.writeStdout(buffer);
}

std::string Renderer::buildFileListScreen(
    const std::vector<FileListItem>& items, int selected, int scroll,
    const std::string& path, const Message& message, int width, int height) {
    std::string out;
    encoder_->beginFrame(out);

    Layout layout = frameBuilder_.calculateLayout(height, width);
    renderFileListContent(out, items, selected, scroll, layout.content);

    StatusBarData data;
    data.name = path.empty() ? "/" : collapseHome(path);
    data.estado = "ABRIR ARCHIVO";
    data.estadoAccent = encoder_->theme().accentAbrir;
    const int total = static_cast<int>(items.size());
    data.right = total == 0 ? "0/0"
                            : std::to_string(selected - scroll + 1) + "/" +
                                  std::to_string(total);
    data.message = message;
    renderStatusBar(out, layout.statusBar, data);

    int rows = std::min(static_cast<int>(items.size()) - scroll, height);
    if (rows > 0) {
        int cursorRow = selected - scroll + 1;
        encoder_->moveCursorTo(out, cursorRow, 1);
    }

    encoder_->endFrame(out);
    return out;
}

void Renderer::renderFileListContent(
    std::string& out, const std::vector<FileListItem>& items, int selected,
    int scroll, const Rect& area) const {
    const Theme& T = encoder_->theme();
    int rows = 0;
    for (int row = 0; row < area.height; ++row, ++rows) {
        int idx = scroll + row;
        encoder_->appendClearLine(out);
        if (idx < static_cast<int>(items.size())) {
            const FileListItem& item = items[static_cast<size_t>(idx)];
            std::string line =
                "  " + item.name + (item.isDirectory ? "/" : "");

            bool isSelected = (idx == selected);
            renderFilledRow(out, line, area.width,
                            isSelected ? T.listSelected : "", T.reset);
        } else {
            renderEmptyMarkerRow(out, T, area.width);
        }
        out += "\r\n";
    }
}

void Renderer::renderFileList(const std::vector<FileListItem>& items,
                              int selected, int scroll, const std::string& path,
                              const Message& message, int width, int height,
                              Sink& sink) {
    diff_->invalidateCache();
    std::string buffer =
        buildFileListScreen(items, selected, scroll, path, message, width, height);
    sink.writeStdout(buffer);
}
