#include <pty.h>
#include <unistd.h>

#include "platform/WindowSize.h"
#include "platform/tty/Terminal.h"
#include "rendering/Sink.h"
#include "layout/Layout.h"
#include "helpers/test_tty_renderer.h"
#include "test_framework.h"

#define private public
#include "app/Editor.h"
#undef private

namespace {
// Sink capturador: retiene lo escrito para assertar el primer frame
struct CaptureSink : public Sink {
    std::string data;
    bool writeStdout(const std::string& s) override {
        data += s;
        return true;
    }
};
} // namespace

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

TEST(window_ed_resize_applies_rows_cols_and_viewport) {
    Editor ed;
    platform::WindowSize s{30, 100, 9, 18, 900, 540};
    
    ed.resize(s);
    CHECK_EQ(ed.currentRows_, 30);
    CHECK_EQ(ed.currentCols_, 100);
    
    // El viewport sigue sincronizado con rows/cols.
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

TEST(window_ed_resize_int_applies_rows_cols) {
    Editor ed;
    platform::WindowSize s{30, 100, 9, 18, 900, 540};
    
    ed.resize(s);
    ed.resize(24, 80);
    CHECK_EQ(ed.currentRows_, 24);
    CHECK_EQ(ed.currentCols_, 80);
}

TEST(window_ed_resize_invalid_rows_cols_preserves_current_grid) {
    Editor ed;
    
    ed.resize(platform::WindowSize{30, 100, 9, 18, 900, 540});
    ed.resize(platform::WindowSize{0, -5, 1, 2, 3, 4});
    CHECK_EQ(ed.currentRows_, 30);
    CHECK_EQ(ed.currentCols_, 100);
}

// Sin pty con píxeles: fallback 24x80 + píxeles 0, sin crash.
TEST(window_terminal_get_window_size_pixels_does_not_break) {
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

// Solo filas/cols validas aplican (guard); el resto ya no se conserva.
TEST(window_ed_resize_applies_valid_rows_cols_with_negative_metrics) {
    Editor ed;
    
    ed.resize(platform::WindowSize{30, 100, -9, -18, -900, -540});
    CHECK_EQ(ed.currentRows_, 30);
    CHECK_EQ(ed.currentCols_, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

// Sin resize() previo: el constructor ya deja geometría coherente
TEST(window_fase_c_initial_viewport_matches_fallback) {
    Editor ed;
    const Layout exp = computeLayout(24, 80);
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
}

// Sin resize() previo el primer render no se rompe: 
// frame completo, cursor en origen y loop sigue vivo.
TEST(window_fase_c_render_without_resize_does_not_break) {
    Editor ed;
    CaptureSink cap;
    const Layout exp = computeLayout(24, 80);
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.renderFrame();
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    CHECK(ed.isRunning());
}

// Con resize() el viewport coincide con la geometría real 
// (no con el fallback)
// el dueño aplica el tamaño y el editor lo refleja + renderiza.
TEST(window_fase_c_resize_viewport_matches_real_size) {
    Editor ed;
    CaptureSink cap;
    const Layout exp = computeLayout(30, 100);
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.resize(30, 100);
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, exp.content.width);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, exp.content.height);
    ed.renderFrame();
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.isRunning());
}

TEST(window_fase_c_resize_invalid_ignored) {
    Editor ed;
    
    ed.resize(30, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    
    ed.resize(0, -5);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    
    ed.resize(-1, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
}

// secuencia de arranque del loop
// (getWindowSize -> resize -> primer renderFrame). 
// Replica TtyRunLoop::run() sin TTY
// el frame debe ser completo 
// (rebuild con clear-screen), con la geometría aplicada y el cursor en origen.
TEST(window_fase_c_first_render_after_resize) {
    Editor ed;
    CaptureSink cap;
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.resize(30, 100);
    ed.renderFrame();
    
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    
    CHECK(!cap.data.empty());
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.line, 0);
    CHECK_EQ(ed.getActiveBufferForTesting().cursor.col, 0);
    CHECK(ed.getStateForTesting() == State::Navegacion);
    CHECK(ed.isRunning());
}

// el renderer solo hace dibujado completo si se cambia la dimensiones de la terminal
// pero un resize al mismo tamaño no lo fuerza
TEST(window_fase_c_resize_rebuild_only_on_geometry_change) {
    Editor ed;
    CaptureSink cap;
    
    ed.setRenderer(makeTtyTestRenderer());
    ed.setSink(cap);
    ed.renderFrame(); // fallback 24x80, caché frío
    cap.data.clear();
    
    ed.resize(30, 100);
    ed.renderFrame();
    CHECK(cap.data.find("\x1b[2J") != std::string::npos);
    
    cap.data.clear();
    ed.resize(30, 100); // mismo tamaño: fast path
    ed.renderFrame();
    CHECK(cap.data.find("\x1b[2J") == std::string::npos);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.width, 100);
    CHECK_EQ(ed.getActiveBufferForTesting().viewport.height, 28);
    CHECK(ed.isRunning());
}
