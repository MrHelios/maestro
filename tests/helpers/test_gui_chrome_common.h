#pragma once

// Helpers compartidos entre los tests de GUI de chrome.
// Se extraen de test_gui_chrome.cpp y test_chrome_anchored_cursor_ime.cpp
// para evitar duplicarlos en varios archivos de tests.

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/Editor.h"
#include "app/Message.h"
#include "base/utf8.h"
#include "filesystem/NullFileWatcher.h"
#include "helpers/FakeClipboard.h"
#include "helpers/test_render_utils.h"
#include "layout/Gutter.h"
#include "layout/Layout.h"
#include "platform/gui/GuiRunLoop.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"
#include "rendering/RenderUtil.h"
#include "rendering/Sink.h"
#include "rendering/Style.h"
#include "rendering/gui/GuiChrome.h"
#include "rendering/gui/GuiChromePixels.h"
#include "rendering/gui/GuiRenderer.h"
#include "rendering/tty/TtyChrome.h"
#include "rendering/tty/TtyEncoder.h"
#include "rendering/tty/TtyTheme.h"

// ---------- Helpers puros (sin SDL) ----------

inline int colWidth(const std::string& s) {
    return utf8::columnOf(s, static_cast<int>(s.size()));
}

inline bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

class NullTestSink : public Sink {
public:
    bool writeStdout(const std::string&) override { return true; }
};

inline Viewport makeVp(int h = 5, int w = 40) {
    Viewport vp;
    vp.top = 0;
    vp.left = 0;
    vp.height = h;
    vp.width = w;
    return vp;
}

// ---------- Geometría de chrome anclado (cursor / IME) ----------

constexpr int kCw = 10;
constexpr int kCh = 19;
constexpr int kH = 428;        // anclado
constexpr int kHLegacy = 437;  // 437 % 19 == 0 -> NO anclado

inline Layout makeLayout(int contentH, int chromeH = 2) {
    Layout lo;
    lo.content = {0, 0, 80, contentH};
    lo.chrome = {0, contentH, 80, chromeH};
    return lo;
}

inline FrameCursor makeCursor(int row, int col,
                              FrameCursorShape shape = FrameCursorShape::Block) {
    FrameCursor c;
    c.cell = CellPos{col, row};
    c.shape = shape;
    c.visible = true;
    return c;
}

// Fila visual (en píxeles) de una celda lógica. Compartida por
// cursorPixelRect() e imeRectPx(): ambos consultan la misma geometría.
// El topInset se suma UNA sola vez, aquí.
inline int cellTopPx(const FrameCursor& cur, const Layout& lo,
                     const guichrome::BottomChromeGeometry& geom,
                     int topInset) {
    int y = cur.cell.row * kCh;
    if (geom.anchored && cur.cell.row >= lo.chrome.row) {
        const int chromeRow = cur.cell.row - lo.chrome.row;
        if (chromeRow == 0) y = geom.statusTop;
        else if (chromeRow == 1) y = geom.messageTop;
    }
    return y + topInset;
}

inline GuiPixelRect cursorRect(const FrameCursor& cur, const Layout& lo,
                               const guichrome::BottomChromeGeometry& geom,
                               int topInset) {
    GuiPixelRect r;
    r.x = cur.cell.col * kCw;
    r.y = cellTopPx(cur, lo, geom, topInset);
    r.w = (cur.shape == FrameCursorShape::Bar) ? 2 : kCw;
    r.h = kCh;
    r.valid = true;
    return r;
}

inline GuiPixelRect imeRect(const FrameCursor& cur, const Layout& lo,
                            const guichrome::BottomChromeGeometry& geom,
                            int topInset) {
    GuiPixelRect r;
    r.x = cur.cell.col * kCw;
    r.y = cellTopPx(cur, lo, geom, topInset);
    r.w = kCw;
    r.h = kCh;
    r.valid = true;
    return r;
}
