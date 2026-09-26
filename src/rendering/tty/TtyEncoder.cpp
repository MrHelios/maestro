#include "rendering/tty/TtyEncoder.h"

#include <algorithm>

#include "rendering/StatusBar.h"

const std::string& TtyEncoder::ansiFor(StyleRole role) const {
    return themeAnsiFor(theme_, role);
}

namespace {

// ¿El rol es un token de sintaxis? En la fila del cursor se envuelve con
// currentLine + estilo (viejo camino syntax+currentLine de renderEditorRow).
bool isSyntaxRole(StyleRole r) {
    switch (r) {
        case StyleRole::SyntaxKeyword:
        case StyleRole::SyntaxType:
        case StyleRole::SyntaxPreprocessor:
        case StyleRole::SyntaxString:
        case StyleRole::SyntaxCharacter:
        case StyleRole::SyntaxNumber:
        case StyleRole::SyntaxComment:
            return true;
        default:
            return false;
    }
}

} // namespace

std::string TtyEncoder::encodeRow(const StyledRow& row) const {
    std::string out;
    size_t est = 4;
    for (const auto& seg : row.segs) est += seg.text.size() + 16;
    out.reserve(est);
    appendRow(out, row);
    return out;
}

void TtyEncoder::appendRow(std::string& out, const StyledRow& row) const {
    const Theme& T = theme_;
    out += "\x1b[K";
    for (const auto& seg : row.segs) {
        const std::string& st = ansiFor(seg.role);
        switch (seg.role) {
            case StyleRole::Default:
            case StyleRole::GutterBlank:
                out += seg.text; // sin estilo (viejo camino plano)
                break;
            case StyleRole::Gutter:
            case StyleRole::GutterCurrent:
            case StyleRole::Marker:
            case StyleRole::CurrentLine:
            case StyleRole::Selection:
            case StyleRole::BracketMatch:
            case StyleRole::ListSelected:
                out += st;
                out += seg.text;
                out += T.reset;
                break;
            default:
                if (isSyntaxRole(seg.role)) {
                    if (row.isCurrentLine) {
                        out += T.currentLine;
                        if (!st.empty()) out += st;
                        out += seg.text;
                        out += T.reset;
                    } else {
                        if (!st.empty()) out += st;
                        out += seg.text;
                        if (!st.empty()) out += T.reset;
                    }
                } else {
                    out += st;
                    out += seg.text;
                    out += T.reset;
                }
                break;
        }
    }
}

std::string TtyEncoder::encodeStatus(const Rect& area,
                                     const StatusBarData& data,
                                     StyleRole accent) const {
    std::string out;
    appendStatus(out, area, data, accent);
    return out;
}

void TtyEncoder::appendStatus(std::string& out, const Rect& area,
                              const StatusBarData& data,
                              StyleRole accent) const {
    StatusBarData resolved = data;
    if (resolved.estadoAccent.empty())
        resolved.estadoAccent = ansiFor(accent);
    StatusBar bar;
    bar.setTheme(theme_);
    out += bar.render(area, resolved);
}

std::string TtyEncoder::encodeFrame(const Frame& f) const {
    std::string out;
    appendFrame(out, f);
    return out;
}

void TtyEncoder::appendFrame(std::string& out, const Frame& f) const {
    // Una sola reserva para todo el frame (camino caliente): filas
    // (contenido + ANSI) + barra + cursor. Evita el regrowth del viejo
    // `out` tanto como el de una fila por string intermedio.
    size_t est = 64 + theme_.background.size() + theme_.foreground.size();
    const size_t rowEst =
        static_cast<size_t>(std::max(0, f.layout.content.width)) + 96;
    est += f.contentRows.size() * rowEst + 1024;
    out.reserve(out.size() + est);
    beginFrame(out);
    for (const auto& row : f.contentRows) {
        appendRow(out, row);
        out += "\r\n";
    }
    appendStatus(out, f.layout.statusBar, f.status, f.statusAccent);
    if (!f.cursor.visible) return;
    moveCursorTo(out, f.cursor.row, f.cursor.col);
    setCursorStyle(out, f.cursor.state);
    endFrame(out);
}

void TtyEncoder::moveCursorTo(std::string& out, int row, int col) const {
    out += "\x1b[";
    out += std::to_string(row);
    out += ";";
    out += std::to_string(col);
    out += "H";
}

void TtyEncoder::hideCursor(std::string& out) const { out += "\x1b[?25l"; }
void TtyEncoder::showCursor(std::string& out) const { out += "\x1b[?25h"; }
void TtyEncoder::setCursorStyle(std::string& out, State state) const {
    if (state == State::Interaccion) out += "\x1b[1 q";
    else out += "\x1b[2 q";
}

void TtyEncoder::beginFrame(std::string& out) const {
    hideCursor(out);
    if (!theme_.background.empty()) out += theme_.background;
    if (!theme_.foreground.empty()) out += theme_.foreground;
    out += "\x1b[2J";
    out += "\x1b[H";
}

void TtyEncoder::endFrame(std::string& out) const {
    showCursor(out);
}
