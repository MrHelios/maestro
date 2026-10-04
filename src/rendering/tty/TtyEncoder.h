#pragma once

#include <string>

#include "platform/CellPos.h"
#include "rendering/tty/TtyTheme.h"
#include "rendering/frame/Frame.h"

// ---------------------------------------------------------------------------
// TtyEncoder: backend TTY. Traduce Frame (roles semánticos) a secuencias
// ANSI usando el TtyTheme como tabla de representación.
//
// Todo lo que contenga "\x1b"/CSI vive acá (o en TtyDiff/TtyChrome::render).
// Frame jamás contiene ANSI; el TtyTheme es la tabla ANSI de ESTE backend
// (GuiPainter usará sus propios colores Qt para los mismos roles).
// ---------------------------------------------------------------------------
class TtyEncoder {
public:
    TtyEncoder() = default;
    explicit TtyEncoder(const TtyTheme& t) : theme_(t) {}

    void setTheme(const TtyTheme& t) { theme_ = t; }
    const TtyTheme& theme() const { return theme_; }

    // Tabla rol -> secuencia ANSI (vacía = sin estilo).
    const std::string& ansiFor(StyleRole role) const;

    // Borrado de línea para pantallas de listas. Es detalle de codificación
    // del backend (objeto, sin estado): las pantallas legacy de Renderer lo
    // usan vía su encoder_ miembro en vez de un literal fuera de tty/.
    void appendClearLine(std::string& out) const;

    // Fila codificada lista para pintar: "\x1b[K" + segmentos.
    // Reproduce byte a byte el viejo renderEditorRow ANSI.
    std::string encodeRow(const StyledRow& row) const;    // Variante sin allocation extra: agrega la fila a `out` (camino
    // caliente de buildScreen: un solo buffer con una sola reserva).
    void appendRow(std::string& out, const StyledRow& row) const;

    // Chrome codificado (delega en TtyChrome::render con el rol;
    // el DTO viaja sin ANSI). Incluye StatusBar (fila superior) + MessageBar (fila inferior).
    std::string encodeChrome(const Rect& area,
                             const ChromeData& data,
                             StyleRole accent) const;
    void appendChrome(std::string& out, const Rect& area,
                      const ChromeData& data, StyleRole accent) const;

    // Frame completo (equivale al viejo buildScreen/buildEditorBody +
    // posicionamiento de cursor).
    std::string encodeFrame(const Frame& f) const;
    void appendFrame(std::string& out, const Frame& f) const;

    // Primitivas de terminal (único lugar que las emite, junto con TtyDiff).
    // moveCursorToRaw es CUP crudo 1-based (row,col) para rewrites de fila.
    // moveCursorTo(CellPos) es el borde del cursor: celda 0-based (col,row)
    // -> CUP con +1 (simétrico al -1 de decodeMouseSgr en
    // platform/tty/TtyMouse.h). Nombres distintos a propósito: el orden de
    // los enteros (row,col) es el inverso al de CellPos (col,row) y un
    // overload silencioso sería un error fácil.
    void moveCursorToRaw(std::string& out, int row, int col) const;
    void moveCursorTo(std::string& out, CellPos cell) const;
    void hideCursor(std::string& out) const;
    void showCursor(std::string& out) const;
    // NOTA: sin overload por State: el DTO ya trae la forma resuelta
    // (FrameCursorShape). La traduccion State -> shape vive en el adaptador
    // app/ChromePresentation.h.
    void setCursorStyle(std::string& out, FrameCursorShape shape) const;
    void beginFrame(std::string& out) const;
    void endFrame(std::string& out) const;

private:
    TtyTheme theme_ = defaultTheme();
};
