#include "rendering/Renderer.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <unistd.h>

#include "base/utf8.h"
#include "diagnostics/Instrument.h"
#include "rendering/RenderUtil.h"

namespace {

// Helpers de filas para las pantallas de listas (TTY directo; pendientes de
// extracción a rendering/tty/). Copiados del viejo Renderer sin cambios.
using namespace chrome;

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

void Renderer::setTheme(const Theme& t) {
    encoder_.setTheme(t);
    diff_.setTheme(t);
}

void Renderer::setExternalSyntaxCache(SyntaxCache* c) {
    frameBuilder_.setExternalSyntaxCache(c);
    diff_.setExternalSyntaxCache(c);
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
    encoder_.appendFrame(out, f);
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

void Renderer::renderScreen(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    std::string buffer = buildScreen(doc, cursor, viewport, filename, modified,
                                     message, state, selection,
                                     searchHighlight, bracketPair);
    writeAll(STDOUT_FILENO, buffer);
}

void Renderer::renderScreenDiff(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    const std::string out =
        buildDiffFrame(doc, cursor, viewport, filename, modified, message,
                       state, selection, searchHighlight, bracketPair);
    if (!writeAll(STDOUT_FILENO, out)) diff_.invalidateCache();
}

std::string Renderer::buildDiffFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    return diff_.buildDiffFrame(doc, cursor, viewport, filename, modified,
                                message, state, selection, searchHighlight,
                                bracketPair);
}

void Renderer::renderEditorContent(
    std::string& out, const Document& doc, const Cursor& cursor,
    const Viewport& viewport, const std::optional<Normalized>& sel,
    const Rect& area, int gutterW) const {
    instrument::ScopedTimer _t(&instrument::current.renderEditorContent_nanos);
    if (instrument::enabled) instrument::onRenderEditorContent();
    int textWidth = std::max(0, area.width - gutterW);
    for (int row = 0; row < area.height; ++row) {
        int docLine = viewport.top + row;
        out += encoder_.encodeRow(frameBuilder_.buildContentRow(
            doc, cursor, viewport, sel, std::nullopt, std::nullopt,
            std::nullopt, docLine, gutterW, textWidth));
        out += "\r\n";
    }
}

void Renderer::renderStatusBar(std::string& out, const Rect& area,
                               const StatusBarData& data) const {
    out += encoder_.encodeStatus(area, data, StyleRole::StatusAccentDefault);
}

std::string Renderer::buildBufferListScreen(
    const std::vector<std::string>& names, int selected, int width,
    int height) {
    std::string out;
    encoder_.beginFrame(out);

    Layout layout = frameBuilder_.calculateLayout(height, width);
    renderBufferListContent(out, names, selected, layout.content);

    StatusBarData data;
    data.name = "Buffers";
    data.estado = "SELECCIONAR";
    data.estadoAccent = encoder_.theme().accentBuffers;
    const int total = static_cast<int>(names.size());
    data.right = std::to_string(std::min(selected + 1, total)) + "/" +
                 std::to_string(total);
    renderStatusBar(out, layout.statusBar, data);

    int rows = std::min(static_cast<int>(names.size()), height);
    if (rows > 0) {
        int cursorRow = std::max(1, std::min(selected + 1, rows));
        encoder_.moveCursorTo(out, cursorRow, 1);
    }

    encoder_.endFrame(out);
    return out;
}

void Renderer::renderBufferListContent(
    std::string& out, const std::vector<std::string>& names, int selected,
    const Rect& area) const {
    const Theme& T = encoder_.theme();
    int rows = 0;
    for (size_t i = 0; i < names.size() && rows < area.height; ++i, ++rows) {
        out += "\x1b[K";
        std::string line = "  " + names[i];
        bool isSelected = (static_cast<int>(i) == selected);
        renderFilledRow(out, line, area.width,
                        isSelected ? T.listSelected : "", T.reset);
        out += "\r\n";
    }
    for (int r = rows; r < area.height; ++r) {
        out += "\x1b[K";
        renderEmptyMarkerRow(out, T, area.width);
        out += "\r\n";
    }
}

void Renderer::renderBufferList(const std::vector<std::string>& names,
                                int selected, int width, int height) {
    diff_.invalidateCache();
    std::string buffer = buildBufferListScreen(names, selected, width, height);
    writeAll(STDOUT_FILENO, buffer);
}

std::string Renderer::buildFileListScreen(
    const std::vector<std::string>& names, int selected, int scroll,
    const std::string& path, const Message& message, int width, int height) {
    std::string out;
    encoder_.beginFrame(out);

    Layout layout = frameBuilder_.calculateLayout(height, width);
    renderFileListContent(out, names, selected, scroll, layout.content);

    StatusBarData data;
    data.name = path.empty() ? "/" : collapseHome(path);
    data.estado = "ABRIR ARCHIVO";
    data.estadoAccent = encoder_.theme().accentAbrir;
    const int total = static_cast<int>(names.size());
    data.right = total == 0 ? "0/0"
                            : std::to_string(selected - scroll + 1) + "/" +
                                  std::to_string(total);
    data.message = message;
    renderStatusBar(out, layout.statusBar, data);

    int rows = std::min(static_cast<int>(names.size()) - scroll, height);
    if (rows > 0) {
        int cursorRow = selected - scroll + 1;
        encoder_.moveCursorTo(out, cursorRow, 1);
    }

    encoder_.endFrame(out);
    return out;
}

void Renderer::renderFileListContent(
    std::string& out, const std::vector<std::string>& names, int selected,
    int scroll, const Rect& area) const {
    const Theme& T = encoder_.theme();
    int rows = 0;
    for (int row = 0; row < area.height; ++row, ++rows) {
        int idx = scroll + row;
        out += "\x1b[K";
        if (idx < static_cast<int>(names.size())) {
            std::string line = "  " + names[static_cast<size_t>(idx)];

            bool isSelected = (idx == selected);
            renderFilledRow(out, line, area.width,
                            isSelected ? T.listSelected : "", T.reset);
        } else {
            renderEmptyMarkerRow(out, T, area.width);
        }
        out += "\r\n";
    }
}

void Renderer::renderFileList(const std::vector<std::string>& names,
                              int selected, int scroll, const std::string& path,
                              const Message& message, int width, int height) {
    diff_.invalidateCache();
    std::string buffer =
        buildFileListScreen(names, selected, scroll, path, message, width, height);
    writeAll(STDOUT_FILENO, buffer);
}
