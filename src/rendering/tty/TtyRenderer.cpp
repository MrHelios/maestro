#include "rendering/tty/TtyRenderer.h"

#include <algorithm>

#include "base/utf8.h"
#include "diagnostics/Instrument.h"
#include "rendering/RenderUtil.h"

namespace {

// Helpers de filas para las pantallas de listas. Movidos tal cual del viejo
// Renderer sin cambios de bytes.
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

void renderEmptyMarkerRow(std::string& out, const TtyTheme& T, int width) {
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

} // namespace

TtyRenderer::TtyRenderer() : diff_(renderer_.frameBuilder(), encoder_) {}

void TtyRenderer::setTheme(const TtyTheme& t) {
    provider_.setTheme(t);
    encoder_.setTheme(t);
    diff_.invalidateCache();
}

void TtyRenderer::toggleTheme() {
    provider_.toggle();
    encoder_.setTheme(provider_.theme());
    diff_.invalidateCache();
}

std::string TtyRenderer::buildScreen(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    Frame f = renderer_.frameBuilder().buildFrame(
        doc, cursor, viewport, filename, modified, message, state, selection,
        searchHighlight, bracketPair);
    std::string out;
    encoder_.appendFrame(out, f);
    return out;
}

void TtyRenderer::renderScreen(
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

void TtyRenderer::renderScreenDiff(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, Sink& sink,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    const std::string out =
        buildDiffFrame(doc, cursor, viewport, filename, modified, message,
                        state, selection, searchHighlight, bracketPair);
    if (!sink.writeStdout(out)) diff_.invalidateCache();
}

std::string TtyRenderer::buildDiffFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    return diff_.buildDiffFrame(doc, cursor, viewport, filename, modified,
                                 message, state, selection, searchHighlight,
                                 bracketPair);
}

void TtyRenderer::renderEditorContent(
    std::string& out, const Document& doc, const Cursor& cursor,
    const Viewport& viewport, const std::optional<Normalized>& sel,
    const Rect& area, int gutterW) const {
    instrument::ScopedTimer _t(instrument::renderEditorContentTimer());
    if (instrument::isEnabled()) instrument::onRenderEditorContent();
    int textWidth = std::max(0, area.width - gutterW);
    for (int row = 0; row < area.height; ++row) {
        int docLine = viewport.top + row;
        out += encoder_.encodeRow(renderer_.frameBuilder().buildContentRow(
            doc, cursor, viewport, sel, std::nullopt, std::nullopt,
            std::nullopt, docLine, gutterW, textWidth));
        out += "\r\n";
    }
}

void TtyRenderer::renderStatusBar(std::string& out, const Rect& area,
                                  const StatusBarData& data,
                                  StyleRole accent) const {
    out += encoder_.encodeStatus(area, data, accent);
}

std::string TtyRenderer::buildBufferListScreen(
    const std::vector<std::string>& names, int selected, int width,
    int height) {
    std::string out;
    encoder_.beginFrame(out);

    Layout layout = renderer_.frameBuilder().calculateLayout(height, width);
    renderBufferListContent(out, names, selected, layout.content);

    StatusBarData data;
    data.name = "Buffers";
    data.estado = "SELECCIONAR";
    const int total = static_cast<int>(names.size());
    data.right = std::to_string(std::min(selected + 1, total)) + "/" +
                 std::to_string(total);
    renderStatusBar(out, layout.statusBar, data, StyleRole::AccentBuffers);

    int rows = std::min(static_cast<int>(names.size()), height);
    if (rows > 0) {
        int cursorRow = std::max(1, std::min(selected + 1, rows));
        encoder_.moveCursorToRaw(out, cursorRow, 1);
    }

    encoder_.endFrame(out);
    return out;
}

void TtyRenderer::renderBufferListContent(
    std::string& out, const std::vector<std::string>& names, int selected,
    const Rect& area) {
    const TtyTheme& T = encoder_.theme();
    int rows = 0;
    for (size_t i = 0; i < names.size() && rows < area.height; ++i, ++rows) {
        encoder_.appendClearLine(out);
        std::string line = "  " + names[i];
        bool isSelected = (static_cast<int>(i) == selected);
        renderFilledRow(out, line, area.width,
                        isSelected ? T.listSelected : "", T.reset);
        out += "\r\n";
    }
    for (int r = rows; r < area.height; ++r) {
        encoder_.appendClearLine(out);
        renderEmptyMarkerRow(out, T, area.width);
        out += "\r\n";
    }
}

void TtyRenderer::renderBufferList(const std::vector<std::string>& names,
                                   int selected, int width, int height,
                                   Sink& sink) {
    std::string buffer = buildBufferListScreen(names, selected, width, height);
    sink.writeStdout(buffer);
}

std::string TtyRenderer::buildFileListFrame(
    const std::vector<FileListItem>& items, int selected, int scroll,
    const std::string& path, const Message& message, int width, int height,
    const char* estado, StyleRole accent) {
    std::string out;
    encoder_.beginFrame(out);

    Layout layout = renderer_.frameBuilder().calculateLayout(height, width);
    renderFileListContent(out, items, selected, scroll, layout.content);

    StatusBarData data;
    data.name = path.empty() ? "/" : chrome::collapseHome(path);
    data.estado = estado;
    const int total = static_cast<int>(items.size());
    data.right = total == 0 ? "0/0"
                            : std::to_string(selected - scroll + 1) + "/" +
                                  std::to_string(total);
    data.message = message;
    renderStatusBar(out, layout.statusBar, data, accent);

    int rows = std::min(static_cast<int>(items.size()) - scroll, height);
    if (rows > 0) {
        int cursorRow = selected - scroll + 1;
        encoder_.moveCursorToRaw(out, cursorRow, 1);
    }

    encoder_.endFrame(out);
    return out;
}

std::string TtyRenderer::buildFileListScreen(
    const std::vector<FileListItem>& items, int selected, int scroll,
    const std::string& path, const Message& message, int width, int height) {
    return buildFileListFrame(items, selected, scroll, path, message, width,
                              height, "ABRIR ARCHIVO", StyleRole::AccentAbrir);
}

void TtyRenderer::renderFileListContent(
    std::string& out, const std::vector<FileListItem>& items, int selected,
    int scroll, const Rect& area) {
    const TtyTheme& T = encoder_.theme();
    int rows = 0;
    for (int row = 0; row < area.height; ++row, ++rows) {
        int idx = scroll + row;
        encoder_.appendClearLine(out);
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

void TtyRenderer::renderFileList(const std::vector<FileListItem>& items,
                                 int selected, int scroll,
                                 const std::string& path,
                                 const Message& message, int width, int height,
                                 Sink& sink) {
    std::string buffer =
        buildFileListScreen(items, selected, scroll, path, message, width, height);
    sink.writeStdout(buffer);
}

std::string TtyRenderer::buildSaveAsFileListScreen(
    const std::vector<FileListItem>& items, int selected, int scroll,
    const std::string& path, const Message& message, int width, int height) {
    // El input del nombre llega ya compuesto en `message` (lo arma el
    // Editor en la fila de mensajes, debajo del statusbar). Solo cambian
    // etiqueta y accent respecto de abrir.
    return buildFileListFrame(items, selected, scroll, path, message, width,
                              height, "GUARDAR COMO", StyleRole::AccentGuardar);
}

void TtyRenderer::renderSaveAsFileList(const std::vector<FileListItem>& items,
                                       int selected, int scroll,
                                       const std::string& path,
                                       const Message& message, int width, int height,
                                       Sink& sink) {
    std::string buffer =
        buildSaveAsFileListScreen(items, selected, scroll, path, message, width, height);
    sink.writeStdout(buffer);
}
