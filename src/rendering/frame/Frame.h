#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "app/EditorState.h"
#include "base/SmallVec.h"
#include "layout/Layout.h"
#include "platform/CellPos.h"
#include "rendering/StatusBarData.h"
#include "rendering/Style.h"

// ---------------------------------------------------------------------------
// Frame (Fase B/C-1): descripción pura de la pantalla, SIN ANSI/CSI.
//
// REGLA ARQUITECTÓNICA: Frame nunca contiene secuencias de escape ni
// operaciones de terminal. Los caches codificados (rowCache_, statusCache_)
// pertenecen exclusivamente a rendering/tty (TtyDiff).
//
//   Editor -> FrameBuilder -> Frame -> TtyEncoder -> diff/scroll -> STDOUT
//                              \-> GuiPainter -> Qt/etc. (futuro)
//
// Los segmentos llevan texto visible + rol semántico; el backend decide la
// representación concreta (ANSI en TTY, colores Qt en GUI).
//
// PRESUPUESTO DE ALLOCATIONS (perf kRenderViewport, baseline 164/frame):
// cada fila posee UN solo buffer (`arena`, 1 alloc) y los segmentos son
// vistas sobre él (cero copias). `segs` es inline hasta 8 segmentos.
// Mover una fila preserva direcciones (std::string mueve el buffer sin
// copiar); copiar está prohibido para no colgar vistas del original.
// ---------------------------------------------------------------------------

// Tramo de texto visible con un rol semántico. `text` es UTF-8 visible
// (nunca contiene '\x1b') y referencia la arena de su propia fila.
struct FrameSegment {
    std::string_view text;
    StyleRole role = StyleRole::Default;
};

// Fila de contenido ya recortada al viewport. `isCurrentLine` indica la fila
// del cursor: el backend la usa para envolver segmentos Default/Syntax con
// el fondo de línea actual (igual que el viejo renderEditorRow).
struct StyledRow {
    StyledRow() = default;
    StyledRow(const StyledRow&) = delete;
    StyledRow& operator=(const StyledRow&) = delete;
    StyledRow(StyledRow&&) = default;
    StyledRow& operator=(StyledRow&&) = default;

    std::string arena; // único dueño de los bytes; 1 alloc por fila
    SmallVec<FrameSegment, 8> segs;
    bool isCurrentLine = false;
};

// Forma visual del cursor para el contrato común TTY/GUI.
// NOTA: se llama FrameCursorShape (y no CursorShape) porque X11/X.h define
// `#define CursorShape 0` y cualquier uso del identificador rompe la
// compilación cuando los headers de X11 ya fueron incluidos (p. ej. via
// X11Clipboard.h antes que Frame.h).
enum class FrameCursorShape {
    Block, // Navegacion/Seleccion/etc: bloque "\x1b[2 q"
    Bar,   // Interaccion: barra "\x1b[1 q"
};

inline FrameCursorShape cursorShapeFor(State state) {
    return state == State::Interaccion ? FrameCursorShape::Bar : FrameCursorShape::Block;
}

// Cursor visual con contrato explícito (comun TTY/GUI):
//
//   visible == true  -> `cell` es la posicion visual real (0-based) y el
//                       backend debe pintar el cursor ahi con `shape`.
//   visible == false -> el cursor logico esta fuera del viewport (ej. rueda
//                       con suppressScrollToCursor_) o el modo lo oculta
//                       (Busqueda). `cell` NO debe utilizarse para pintar.
//
// El GUI no reproduce ningun clamp: solo mira `visible`. El backend TTY
// tampoco clampa: si no es visible deja el cursor oculto (hideCursor sin
// show posterior). `state` se conserva para el estilo legacy/TTY.
struct FrameCursor {
    CellPos cell; // 0-based (col=X, row=Y); valido solo si visible==true
    // Default seguro: oculto, consistente con celda invalida (-1,-1)
    // (la da el default de CellPos; no duplicar el {-1,-1} acá).
    // Todo Frame valido lo rellena FrameBuilder::buildFrame.
    bool visible = false;
    FrameCursorShape shape = FrameCursorShape::Block;
    State state = State::Navegacion;
};

struct Frame {
    Frame() = default;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&) = default;
    Frame& operator=(Frame&&) = default;

    std::vector<StyledRow> contentRows; // contentH filas (sin status bar)
    StatusBarData status;               // DTO puro para la barra común
    StyleRole statusAccent = StyleRole::StatusAccentDefault;
    FrameCursor cursor;
    Layout layout;
    int gutterW = 0;

    // Validador de la regla arquitectónica (para tests/debug): ningún string
    // del Frame puede contener ESC. El campo legacy `status.estadoAccent`
    // debe viajar vacío en el camino nuevo (el accent va en `statusAccent`).
    bool hasAnsi() const {
        auto hasEsc = [](const std::string& s) {
            return s.find('\x1b') != std::string::npos;
        };
        auto hasEscView = [](std::string_view s) {
            return s.find('\x1b') != std::string_view::npos;
        };
        for (const auto& r : contentRows) {
            if (hasEsc(r.arena)) return true;
            for (const auto& s : r.segs)
                if (hasEscView(s.text)) return true;
        }
        if (hasEsc(status.name) || hasEsc(status.path) ||
            hasEsc(status.estado) || hasEsc(status.estadoAccent) ||
            hasEsc(status.right) || hasEsc(status.message.text))
            return true;
        return false;
    }
};
