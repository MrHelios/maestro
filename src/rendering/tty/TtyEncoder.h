#pragma once

#include <string>

#include "rendering/Theme.h"
#include "rendering/frame/Frame.h"

// ---------------------------------------------------------------------------
// TtyEncoder: backend TTY. Traduce Frame (roles semánticos) a secuencias
// ANSI usando el Theme como tabla de representación.
//
// Todo lo que contenga "\x1b"/CSI vive acá (o en TtyDiff/StatusBar::render).
// Frame jamás contiene ANSI; el Theme es la tabla ANSI de ESTE backend
// (GuiPainter usará sus propios colores Qt para los mismos roles).
// ---------------------------------------------------------------------------
class TtyEncoder {
public:
    TtyEncoder() = default;
    explicit TtyEncoder(const Theme& t) : theme_(t) {}

    void setTheme(const Theme& t) { theme_ = t; }
    const Theme& theme() const { return theme_; }

    // Tabla rol -> secuencia ANSI (vacía = sin estilo).
    const std::string& ansiFor(StyleRole role) const;

    // Fila codificada lista para pintar: "\x1b[K" + segmentos.
    // Reproduce byte a byte el viejo renderEditorRow ANSI.
    std::string encodeRow(const StyledRow& row) const;
    // Variante sin allocation extra: agrega la fila a `out` (camino
    // caliente de buildScreen: un solo buffer con una sola reserva).
    void appendRow(std::string& out, const StyledRow& row) const;

    // Barra de estado codificada (delega en StatusBar::render tras resolver
    // el accent: respeta el camino legacy `estadoAccent` no vacío).
    std::string encodeStatus(const Rect& area,
                             const StatusBarData& data,
                             StyleRole accent) const;
    void appendStatus(std::string& out, const Rect& area,
                      const StatusBarData& data, StyleRole accent) const;

    // Frame completo (equivale al viejo buildScreen/buildEditorBody +
    // posicionamiento de cursor).
    std::string encodeFrame(const Frame& f) const;
    void appendFrame(std::string& out, const Frame& f) const;

    // Primitivas de terminal (único lugar que las emite, junto con TtyDiff).
    void moveCursorTo(std::string& out, int row, int col) const;
    void hideCursor(std::string& out) const;
    void showCursor(std::string& out) const;
    void setCursorStyle(std::string& out, State state) const;
    void beginFrame(std::string& out) const;
    void endFrame(std::string& out) const;

private:
    Theme theme_ = defaultTheme();
};
