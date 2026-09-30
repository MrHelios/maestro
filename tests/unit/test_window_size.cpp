#define private public
#include "app/Editor.h"
#undef private

#include <pty.h>
#include <unistd.h>

#include "platform/WindowSize.h"
#include "platform/tty/Terminal.h"
#include "test_framework.h"

TEST(window_size_default_values) {
    platform::WindowSize s;
    CHECK_EQ(s.rows, 24);
    CHECK_EQ(s.cols, 80);
    CHECK_EQ(s.cellW, 0);
    CHECK_EQ(s.cellH, 0);
    CHECK_EQ(s.pixelW, 0);
    CHECK_EQ(s.pixelH, 0);
}

TEST(window_size_aggregate_construction) {
    platform::WindowSize s{30, 100, 8, 16, 800, 480};
    CHECK_EQ(s.rows, 30);
    CHECK_EQ(s.cols, 100);
    CHECK_EQ(s.cellW, 8);
    CHECK_EQ(s.cellH, 16);
    CHECK_EQ(s.pixelW, 800);
    CHECK_EQ(s.pixelH, 480);
}

TEST(editor_resize_preserves_window_metrics) {
    Editor ed;
    platform::WindowSize s{30, 100, 9, 18, 900, 540};
    ed.resize(s);
    // Métricas guardadas aunque hoy ningún lector las consuma.
    CHECK_EQ(ed.currentRows_, 30);
    CHECK_EQ(ed.currentCols_, 100);
    CHECK_EQ(ed.currentCellW_, 9);
    CHECK_EQ(ed.currentCellH_, 18);
    CHECK_EQ(ed.currentPixelW_, 900);
    CHECK_EQ(ed.currentPixelH_, 540);
    // El viewport sigue sincronizado con rows/cols.
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

TEST(editor_resize_int_clears_metrics) {
    Editor ed;
    ed.resize(platform::WindowSize{30, 100, 9, 18, 900, 540});
    ed.resize(24, 80);
    CHECK_EQ(ed.currentRows_, 24);
    CHECK_EQ(ed.currentCols_, 80);
    CHECK_EQ(ed.currentCellW_, 0);
    CHECK_EQ(ed.currentCellH_, 0);
    CHECK_EQ(ed.currentPixelW_, 0);
    CHECK_EQ(ed.currentPixelH_, 0);
}

TEST(editor_resize_invalid_keeps_metrics) {
    Editor ed;
    ed.resize(platform::WindowSize{30, 100, 9, 18, 900, 540});
    ed.resize(platform::WindowSize{0, -5, 1, 2, 3, 4});
    CHECK_EQ(ed.currentRows_, 30);
    CHECK_EQ(ed.currentCols_, 100);
    CHECK_EQ(ed.currentCellW_, 9);
    CHECK_EQ(ed.currentCellH_, 18);
    CHECK_EQ(ed.currentPixelW_, 900);
    CHECK_EQ(ed.currentPixelH_, 540);
}

TEST(terminal_get_window_size_pixels_does_not_break) {
    // Sin pty con píxeles: fallback 24x80 + píxeles 0, sin crash.
    Terminal t;
    int rows = 0, cols = 0, pixelW = -1, pixelH = -1;
    t.getWindowSize(rows, cols, pixelW, pixelH);
    CHECK(rows > 0);
    CHECK(cols > 0);
    CHECK(pixelW >= 0);
    CHECK(pixelH >= 0);
    // El overload simple sigue funcionando.
    int r2 = 0, c2 = 0;
    t.getWindowSize(r2, c2);
    CHECK_EQ(r2, rows);
    CHECK_EQ(c2, cols);
}
