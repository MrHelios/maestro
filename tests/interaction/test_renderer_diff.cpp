// ===========================================================================
// Correccion del RENDER DIFERENCIAL (buildDiffFrame).
//
// Contrato del delta: para cada fila cambiada emite CSI {fila};1H + \x1b[K +
// contenido de la fila completa. Aca un mini-emulador aplica EXACTAMENTE ese
// contrato sobre una pantalla y verificamos que, tras secuencias reales de
// edicion/scroll/undo/pantalla modal, la pantalla resultante sea IDENTICA a
// la que produce el redibujo completo (buildScreen).
//
// Propiedad economica adicional: una tecla no debe emitir el frame entero.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "test_framework.h"

#define private public
#include "ui/Editor.h"
#undef private

namespace {

// Mini-emulador de terminal para las UNICAS secuencias que emite Maestro:
//   \x1b[?25l / \x1b[?25h   (ignoradas)
//   \x1b[{r};{c}H           (posiciona fila actual; col se ignora)
//   \x1b[K                  (limpia resto de la fila actual)
//   \x1b[H \x1b[J           (home / borrar todo: usado por frames completos)
//   texto                   (escribe en la fila actual)
//   \r\n                    (salta a la fila siguiente, col 0)
struct TinyTerm {
    int rows;
    int cur = 0;
    std::vector<std::string> screen;

    explicit TinyTerm(int r) : rows(r), screen(static_cast<size_t>(r), "") {}

    void apply(const std::string& out) {
        std::size_t i = 0;
        while (i < out.size()) {
            if (out[i] == '\x1b' && i + 1 < out.size() && out[i + 1] == '[') {
                std::size_t j = i + 2;
                while (j < out.size() && !((out[j] >= 'A' && out[j] <= 'Z') ||
                                           (out[j] >= 'a' && out[j] <= 'z')))
                    ++j;
                if (j >= out.size()) break;
                char term = out[j];
                ++j;
                if (term == 'H') {
                    std::string params = out.substr(i + 2, j - 1 - (i + 2));
                    int row = 0;
                    std::size_t semi = params.find(';');
                    std::string rowStr = semi == std::string::npos ? params : params.substr(0, semi);
                    if (!rowStr.empty()) {
                        try { row = std::stoi(rowStr); } catch (...) { row = 0; }
                    } else {
                        row = 1;
                    }
                    if (row >= 1 && row <= rows) cur = row - 1;
                    if (params.empty()) cur = 0;
                } else if (term == 'K') {
                    screen[static_cast<size_t>(cur)].clear();
                } else if (term == 'J') {
                    for (int rr = cur; rr < rows; ++rr)
                        screen[static_cast<size_t>(rr)].clear();
                }
                i = j;
                continue;
            }
            if (out[i] == '\r' && i + 1 < out.size() && out[i + 1] == '\n') {
                if (cur + 1 < rows) ++cur;
                ++i;
            } else if (out[i] != '\n' && out[i] != '\r') {
                screen[static_cast<size_t>(cur)] += out[i];
            }
            ++i;
        }
    }

    bool operator==(const TinyTerm& o) const { return screen == o.screen; }
};

Event key(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

Event move(EventType t) {
    Event e;
    e.type = t;
    return e;
}

struct DiffHarness {
    Editor ed;
    Renderer& r;
    static constexpr int kRows = 26; // 24 contenido + 2 barra

    explicit DiffHarness(int lines) : r(ed.renderer_) {
        ed.active().document.restore(
            std::vector<std::string>(static_cast<size_t>(lines),
                                     std::string(80, 'x')));
        ed.state_ = State::Interaccion;
    }

    Buffer& buf() { return ed.active(); }

    // Aplica el delta al emulador "real" y devuelve el stream emitido.
    std::string step() {
        Buffer& b = buf();
        const std::string out = r.buildDiffFrame(
            b.document, b.cursor, b.viewport, b.filename, b.modified,
            Message(""), State::Navegacion, b.selection);
        return out;
    }
};

} // namespace

// La pantalla reconstruida SOLO con deltas debe ser identica a la que se
// obtiene aplicando frames completos, paso a paso.
TEST(render_diff_pantalla_identica_al_frame_completo) {
    DiffHarness h(300);
    Buffer& b = h.buf();

    TinyTerm viaDiff(h.kRows);   // recibe lo que emitira el render diferencial
    TinyTerm viaFull(h.kRows);   // recibe frames completos (verdad de terreno)

    auto syncBoth = [&](const std::function<void()>& action) {
        action();
        b.viewport.scrollToCursor(b.cursor);
        const std::string delta = h.step();
        const std::string full = h.r.buildScreen(
            b.document, b.cursor, b.viewport, b.filename, b.modified,
            Message(""), State::Navegacion, b.selection);
        viaDiff.apply(delta);
        viaFull.apply(full);
        CHECK(viaDiff == viaFull);
    };

    // Frame inicial: completo en ambos.
    syncBoth([] {});

    // Tecleo sostenido.
    for (int burst = 0; burst < 3; ++burst) {
        syncBoth([&] { for (int i = 0; i < 20; ++i) h.ed.handleEvent(key('a')); });
    }

    // Backspace, Enter (SplitLine), scroll hacia abajo y arriba.
    syncBoth([&] { h.ed.handleEvent(move(EventType::Backspace)); });
    syncBoth([&] { h.ed.handleEvent(move(EventType::InsertNewline)); });
    // Nota: en scroll cambian TODAS las filas, asi que el delta es de tamano
    // comparable al frame completo; la ganancia ahi es eliminar \x1b[J
    // (borrado total), no los bytes.
    syncBoth([&] {
        for (int i = 0; i < 40; ++i) h.ed.handleEvent(move(EventType::MoveDown));
    });
    syncBoth([&] {
        for (int i = 0; i < 60; ++i) h.ed.handleEvent(move(EventType::MoveUp));
    });

    // Undo/Redo.
    syncBoth([&] { h.ed.handleEvent(move(EventType::Undo)); });
    syncBoth([&] { h.ed.handleEvent(move(EventType::Redo)); });
}

// Una tecla que solo toca una fila no debe emitir el frame entero (~2.5 KB):
// es LA propiedad que justifica el render diferencial.
TEST(render_diff_tecla_emite_menos_que_frame_completo) {
    DiffHarness h(300);
    Buffer& b = h.buf();

    h.step(); // frame inicial completo

    const std::size_t fullSize = h.r.buildScreen(
        b.document, b.cursor, b.viewport, b.filename, b.modified, Message(""),
        State::Navegacion, b.selection).size();

    const std::size_t deltaSize = [&] {
        h.ed.handleEvent(key('a'));
        return h.step().size();
    }();

    CHECK(deltaSize < fullSize / 4); // <25% del frame completo
    std::printf("      delta tecla: %zu bytes vs frame completo %zu bytes\n",
                deltaSize, fullSize);
}

TEST(render_diff_segundo_frame_solo_mueve_cursor) {
    DiffHarness h(300);
    const std::string init = h.step();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    viaDiff.apply(init);
    viaFull.apply(init);
    const std::size_t fullSize = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, Message(""), State::Navegacion, h.buf().selection).size();

    h.buf().cursor.col = 5;
    const std::string delta = h.step();
    const std::string full = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, Message(""), State::Navegacion, h.buf().selection);

    viaDiff.apply(delta);
    viaFull.apply(full);
    CHECK(viaDiff == viaFull);
    CHECK(delta.find("\x1b[H\x1b[J") == std::string::npos);
    CHECK(delta.size() < fullSize / 4);
}

TEST(render_diff_edicion_una_sola_linea_solo_esa_fila) {
    DiffHarness h(300);
    const std::string init = h.step();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    viaDiff.apply(init);
    viaFull.apply(init);

    const int targetLine = h.buf().cursor.line;
    h.buf().document.lineAt(targetLine);
    h.ed.handleEvent(key('Z'));
    const std::string delta = h.step();
    const std::string full = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, Message(""), State::Navegacion, h.buf().selection);

    viaDiff.apply(delta);
    viaFull.apply(full);
    CHECK(viaDiff == viaFull);
    CHECK(delta.find("\x1b[H\x1b[J") == std::string::npos);
    int rewrites = 0;
    for (std::size_t p = 0; (p = delta.find("\x1b[K", p)) != std::string::npos; ++rewrites, ++p) {}
    CHECK(rewrites >= 1);
    CHECK(rewrites <= 4);
}

TEST(render_diff_scroll_reescribe_filas_sin_borrado_total) {
    DiffHarness h(300);
    const std::string init = h.step();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    viaDiff.apply(init);
    viaFull.apply(init);

    h.buf().viewport.top = 5;
    h.buf().cursor.line = 10;
    const std::string delta = h.step();
    const std::string full = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, Message(""), State::Navegacion, h.buf().selection);

    viaDiff.apply(delta);
    viaFull.apply(full);
    CHECK(viaDiff == viaFull);
    CHECK(delta.find("\x1b[H\x1b[J") == std::string::npos);
    int rewrites = 0;
    for (std::size_t p = 0; (p = delta.find("\x1b[K", p)) != std::string::npos; ++rewrites, ++p) {}
    CHECK(rewrites >= h.buf().viewport.height);
}

TEST(render_diff_vuelta_de_filebrowser_es_completo) {
    DiffHarness h(300);
    h.step();
    h.r.renderFileList({"a.txt", "b.txt"}, 0, 0, "/tmp", Message(""), 80, 24);
    CHECK(!h.r.hasLastEditorBody_);
    const std::string trasFileList = h.step();
    CHECK(trasFileList.find("\x1b[H\x1b[J") != std::string::npos);
}

// Resize y pantallas modales invalidan el cache: el proximo frame sale
// completo para no dejar basura en pantalla.
TEST(render_diff_invalidacion_por_modal_y_resize) {
    DiffHarness h(300);

    h.step(); // inicial completo
    CHECK(h.r.hasLastEditorBody_);

    // Pantalla modal pinta encima => invalida.
    h.r.renderBufferList({"uno", "dos"}, 0, 80, 24);
    CHECK(!h.r.hasLastEditorBody_);
    const std::string trasModal = h.step();
    CHECK(trasModal.find("\x1b[H\x1b[J") != std::string::npos); // frame completo
    CHECK(h.r.hasLastEditorBody_);

    // Resize => invalida.
    h.buf().viewport.height = 30;
    const std::string trasResize = h.step();
    CHECK(trasResize.find("\x1b[H\x1b[J") != std::string::npos);
    CHECK_EQ(h.r.lastViewportH_, 30);
}
