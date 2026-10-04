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

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "test_framework.h"
#include "rendering/Sink.h"
#include "platform/InputEvent.h"  // usa InputEvent común

#define private public
#include "app/ChromePresentation.h"
#include "app/Editor.h"
#include "rendering/tty/TtyRenderer.h"
#include "rendering/tty/TtySink.h"
#undef private

namespace {

struct TinyTerm {
    int rows;
    int cur_row = 0;
    int cur_col = 0;
    std::vector<std::string> screen;

    explicit TinyTerm(int r) : rows(r), screen(static_cast<size_t>(r), "") {}

    void writeChar(char c) {
        if (cur_row < 0 || cur_row >= rows || cur_col < 0) return;
        if (cur_col > 1000) return;
        auto& row = screen[static_cast<size_t>(cur_row)];
        if (cur_col >= static_cast<int>(row.size()))
            row.resize(static_cast<size_t>(cur_col) + 1, ' ');
        row[static_cast<size_t>(cur_col)] = c;
        ++cur_col;
    }

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
                std::string params = out.substr(i + 2, j - (i + 2));
                ++j;
                if (term == 'H') {
                    // Renderer emite \x1b[{fila};{col}H. Para rewrites de fila
                    // completa col==1 (moveCursorToRaw(row,1)), para cursor es
                    // variable (editorCursorPos). Se acepta cualquier col>=1
                    // para no ocultar bugs de cursor; el comentario anterior
                    // que prometía "fallará a propósito" no se cumplía.
                    int row = 1, col = 1;
                    if (!params.empty()) {
                        std::size_t semi = params.find(';');
                        std::string rowStr = semi == std::string::npos ? params : params.substr(0, semi);
                        std::string colStr = semi == std::string::npos ? "" : params.substr(semi + 1);
                        if (!rowStr.empty()) { try { row = std::stoi(rowStr); } catch (...) { row = 1; } }
                        if (!colStr.empty()) { try { col = std::stoi(colStr); } catch (...) { col = 1; } }
                    }
                    if (row >= 1 && row <= rows) cur_row = row - 1;
                    cur_col = col >= 1 ? col - 1 : 0;
                } else if (term == 'K') {
                    if (cur_row < 0 || cur_row >= rows) { i = j; continue; }
                    int mode = 0;
                    if (!params.empty()) { try { mode = std::stoi(params); } catch (...) { mode = 0; } }
                    auto& row = screen[static_cast<size_t>(cur_row)];
                    if (mode == 2) {
                        row.clear();
                    } else if (mode == 1) {
                        for (int c = 0; c <= cur_col && c < static_cast<int>(row.size()); ++c)
                            row[static_cast<size_t>(c)] = ' ';
                    } else {
                        if (cur_col < static_cast<int>(row.size())) row.resize(static_cast<size_t>(cur_col));
                    }
                } else if (term == 'J') {
                    int mode = 0;
                    if (!params.empty()) { try { mode = std::stoi(params); } catch (...) { mode = 0; } }
                    if (mode == 2) {
                        for (auto& row_str : screen) row_str.clear();
                        // Simplificación atada a Renderer::beginFrame (\x1b[2J\x1b[H):
                        // ANSI ED 2 no mueve cursor, pero aquí reseteamos a 0,0
                        // porque el renderer siempre emite H junto a 2J.
                        cur_row = 0;
                        cur_col = 0;
                    } else {
                        if (cur_row < 0 || cur_row >= rows) { i = j; continue; }
                        if (mode == 1) {
                            for (int rr = 0; rr < cur_row; ++rr) screen[static_cast<size_t>(rr)].clear();
                            auto& row = screen[static_cast<size_t>(cur_row)];
                            for (int c = 0; c <= cur_col && c < static_cast<int>(row.size()); ++c)
                                row[static_cast<size_t>(c)] = ' ';
                        } else {
                            auto& row = screen[static_cast<size_t>(cur_row)];
                            if (cur_col < static_cast<int>(row.size())) row.resize(static_cast<size_t>(cur_col));
                            for (int rr = cur_row + 1; rr < rows; ++rr) screen[static_cast<size_t>(rr)].clear();
                        }
                    }
                } else {
                    // CSI no reconocido (colores \x1b[...m, scroll \x1b[...r/S/T): ignorado,
                    // no afecta buffer. Si aparece uno nuevo que sí mueve cursor,
                    // añadir rama explícita.
                }
                i = j;
                continue;
            }
            if (out[i] == '\r') {
                cur_col = 0;
            } else if (out[i] == '\n') {
                if (cur_row >= 0 && cur_row + 1 < rows) ++cur_row;
                else if (cur_row < 0) cur_row = 0;
            } else {
                writeChar(out[i]);
            }
            ++i;
        }
    }

    static std::string rstrip(const std::string& s) {
        std::size_t end = s.find_last_not_of(' ');
        return end == std::string::npos ? std::string() : s.substr(0, end + 1);
    }

    bool operator==(const TinyTerm& o) const {
        // rstrip evita falsos negativos por \x1b[K/resize que dejan
        // longitudes distintas pero mismo contenido visible. Espacios
        // finales son invisibles; basura interior sí se detecta.
        if (screen.size() != o.screen.size()) return false;
        for (std::size_t r = 0; r < screen.size(); ++r)
            if (rstrip(screen[r]) != rstrip(o.screen[r])) return false;
        return true;
    }
};

void checkDiffMatchesFull(TinyTerm& viaDiff, TinyTerm& viaFull,
                          const std::string& delta, const std::string& full,
                          bool allowFullClear) {
    viaDiff.apply(delta);
    viaFull.apply(full);
    if (!(viaDiff == viaFull)) {
        std::printf("DIFF MISMATCH\n");
        for (std::size_t r = 0; r < viaDiff.screen.size(); ++r) {
            if (TinyTerm::rstrip(viaDiff.screen[r]) != TinyTerm::rstrip(viaFull.screen[r])) {
                std::printf("row %zu diff:[%s] full:[%s]\n", r,
                            TinyTerm::rstrip(viaDiff.screen[r]).c_str(),
                            TinyTerm::rstrip(viaFull.screen[r]).c_str());
            }
        }
    }
    CHECK(viaDiff == viaFull);
    if (!allowFullClear) {
        CHECK(delta.find("\x1b[2J") == std::string::npos);
        CHECK(delta.find("\x1b[H\x1b[J") == std::string::npos);
    }
}

InputEvent key(char c) {
    InputEvent e;
    e.type = InputEventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

InputEvent move(InputEventType t) {
    InputEvent e;
    e.type = t;
    return e;
}

struct DiffHarness {
    Editor ed;
    TtyRenderer r;  // driver del diff (el Editor ya no expone el backend)
    int kRows;

    explicit DiffHarness(int lines) {
        ed.active().document.restore(
            std::vector<std::string>(static_cast<size_t>(lines),
                                     std::string(80, 'x')));
        ed.state_ = State::Interaccion;
        kRows = ed.active().viewport.height + 2;
    }

    Buffer& buf() { return ed.active(); }

    std::string getDiffOutput() {
        Buffer& b = buf();
        return r.buildDiffFrame(
            b.document, b.cursor, b.viewport, b.filename, b.modified,
            makeChromeRequest(Message(""), State::Navegacion), b.selection);
    }
};

} // namespace

TEST(render_diff_pantalla_identica_al_frame_completo) {
    DiffHarness h(300);
    Buffer& b = h.buf();

    TinyTerm viaDiff(h.kRows);
    TinyTerm viaFull(h.kRows);

    auto syncBoth = [&](const std::function<void()>& action) {
        action();
        b.viewport.scrollToCursor(b.cursor);
        bool allowFullClear = !h.r.hasCache();
        checkDiffMatchesFull(viaDiff, viaFull,
            h.getDiffOutput(),
            h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, makeChromeRequest(Message(""), State::Navegacion), b.selection),
            allowFullClear);
    };

    syncBoth([] {});

    for (int burst = 0; burst < 3; ++burst) {
        syncBoth([&] { for (int i = 0; i < 20; ++i) h.ed.handleEvent(key('a')); });
    }

    syncBoth([&] { h.ed.handleEvent(move(InputEventType::Backspace)); });
    syncBoth([&] { h.ed.handleEvent(move(InputEventType::InsertNewline)); });
    syncBoth([&] {
        for (int i = 0; i < 40; ++i) h.ed.handleEvent(move(InputEventType::MoveDown));
    });
    syncBoth([&] {
        for (int i = 0; i < 60; ++i) h.ed.handleEvent(move(InputEventType::MoveUp));
    });

    syncBoth([&] { h.ed.handleEvent(move(InputEventType::Undo)); });
    syncBoth([&] { h.ed.handleEvent(move(InputEventType::Redo)); });
}

TEST(render_diff_tecla_emite_menos_que_frame_completo) {
    DiffHarness h(300);
    Buffer& b = h.buf();

    h.getDiffOutput();

    const std::size_t fullSize = h.r.buildScreen(
        b.document, b.cursor, b.viewport, b.filename, b.modified,
        makeChromeRequest(Message(""), State::Navegacion), b.selection).size();

    const std::size_t deltaSize = [&] {
        h.ed.handleEvent(key('a'));
        return h.getDiffOutput().size();
    }();

    CHECK(deltaSize < fullSize / 4);
    std::printf("      delta tecla: %zu bytes vs frame completo %zu bytes\n",
                deltaSize, fullSize);
}

TEST(render_diff_segundo_frame_solo_mueve_cursor) {
    DiffHarness h(300);
    const std::string init = h.getDiffOutput();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    viaDiff.apply(init);
    viaFull.apply(init);
    const std::size_t fullSize = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, makeChromeRequest(Message(""), State::Navegacion), h.buf().selection).size();

    h.buf().cursor.col = 5;
    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, makeChromeRequest(Message(""), State::Navegacion), h.buf().selection);

    checkDiffMatchesFull(viaDiff, viaFull, delta, full, false);
    CHECK(delta.size() < fullSize / 4);
}

TEST(render_diff_edicion_una_sola_linea_solo_esa_fila) {
    DiffHarness h(300);
    const std::string init = h.getDiffOutput();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    viaDiff.apply(init);
    viaFull.apply(init);

    const int targetLine = h.buf().cursor.line;
    h.buf().document.lineAt(targetLine);
    h.ed.handleEvent(key('Z'));
    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, makeChromeRequest(Message(""), State::Navegacion), h.buf().selection);

    checkDiffMatchesFull(viaDiff, viaFull, delta, full, false);
    int rewrites = 0;
    for (std::size_t p = 0; (p = delta.find("\x1b[K", p)) != std::string::npos; ++rewrites, ++p) {}
    // 1 fila editada + 1 statusBar + 1 margen (frágil: ajustar si se añade wrap/indicador)
    CHECK(rewrites >= 1 && rewrites <= 3);
}

TEST(render_diff_linea_se_encoge_no_deja_basura) {
    DiffHarness h(300);
    Buffer& b = h.buf();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    bool allow1 = !h.r.hasCache();
    for (int i = 0; i < 20; ++i) h.ed.handleEvent(key('a'));
    b.viewport.scrollToCursor(b.cursor);
    std::string delta = h.getDiffOutput();
    std::string full = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, makeChromeRequest(Message(""), State::Navegacion), b.selection);
    checkDiffMatchesFull(viaDiff, viaFull, delta, full, allow1);

    for (int i = 0; i < 10; ++i) h.ed.handleEvent(move(InputEventType::Backspace));
    b.viewport.scrollToCursor(b.cursor);

    delta = h.getDiffOutput();
    full = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, makeChromeRequest(Message(""), State::Navegacion), b.selection);

    CHECK(delta.find("\x1b[K") != std::string::npos);
    checkDiffMatchesFull(viaDiff, viaFull, delta, full, true);
    CHECK(viaDiff.screen[0].find("aaaaaaaaaa") != std::string::npos);
    CHECK(viaDiff.screen[0].find("xxxxxxxxxx") != std::string::npos);
}

TEST(render_diff_scroll_reescribe_filas_sin_borrado_total) {
    DiffHarness h(300);
    const std::string init = h.getDiffOutput();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    viaDiff.apply(init);
    viaFull.apply(init);

    h.buf().viewport.top = 5;
    h.buf().cursor.line = 10;
    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport, h.buf().filename,
        h.buf().modified, makeChromeRequest(Message(""), State::Navegacion), h.buf().selection);

    checkDiffMatchesFull(viaDiff, viaFull, delta, full, false);
    int rewrites = 0;
    for (std::size_t p = 0; (p = delta.find("\x1b[K", p)) != std::string::npos; ++rewrites, ++p) {}
    CHECK(rewrites >= h.buf().viewport.height);
}

TEST(render_diff_scroll_realista_reescribe_filas_sin_borrado_total) {
    DiffHarness h(300);
    Buffer& b = h.buf();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    h.getDiffOutput();

    for (int i = 0; i < 30; ++i) h.ed.handleEvent(move(InputEventType::MoveDown));
    b.viewport.scrollToCursor(b.cursor);

    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(
        b.document, b.cursor, b.viewport, b.filename, b.modified,
        makeChromeRequest(Message(""), State::Navegacion), b.selection);

    checkDiffMatchesFull(viaDiff, viaFull, delta, full, false);
    int rewrites = 0;
    for (std::size_t p = 0; (p = delta.find("\x1b[K", p)) != std::string::npos; ++rewrites, ++p) {}
    CHECK(rewrites >= b.viewport.height);
}

TEST(render_diff_vuelta_de_filebrowser_es_completo) {
    DiffHarness h(300);
    NullSink null;
    TtyRenderer tr;
    h.getDiffOutput();
    tr.renderFileList(std::vector<FileListItem>{{"a.txt", false}, {"b.txt", false}}, 0, 0, "/tmp", MessageBarData(""), 80, 24, null);
    // El backend no se autoinvalida: el caller invalida al entrar/salir
    // del modal (igual que Editor::renderFrame).
    h.r.invalidateCache();
    CHECK(!h.r.hasCache());
    const std::string trasFileList = h.getDiffOutput();
    CHECK(trasFileList.find("\x1b[2J\x1b[H") != std::string::npos);
}

TEST(render_diff_mensaje_temporal) {
    DiffHarness h(300);
    Buffer& b = h.buf();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    const std::string init = h.getDiffOutput();
    viaDiff.apply(init);
    viaFull.apply(init);

    b.viewport.scrollToCursor(b.cursor);
    std::string deltaMsg = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, b.filename, b.modified, makeChromeRequest(Message("Guardando..."), State::Navegacion), b.selection);
    std::string fullMsg = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, makeChromeRequest(Message("Guardando..."), State::Navegacion), b.selection);

    checkDiffMatchesFull(viaDiff, viaFull, deltaMsg, fullMsg, false);

    std::string deltaClear = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, b.filename, b.modified, makeChromeRequest(Message(""), State::Navegacion), b.selection);
    std::string fullClear = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, makeChromeRequest(Message(""), State::Navegacion), b.selection);

    checkDiffMatchesFull(viaDiff, viaFull, deltaClear, fullClear, false);
}

TEST(render_diff_invalidacion_por_modal_y_resize) {
    DiffHarness h(300);
    NullSink null;
    TtyRenderer tr;

    h.getDiffOutput();
    CHECK(h.r.hasCache());

    tr.renderBufferList({"uno", "dos"}, 0, 80, 24, null);
    // Idem arriba: el backend no se autoinvalida.
    h.r.invalidateCache();
    CHECK(!h.r.hasCache());
    const std::string trasModal = h.getDiffOutput();
    CHECK(trasModal.find("\x1b[2J\x1b[H") != std::string::npos);
    CHECK(h.r.hasCache());

    h.buf().viewport.height = 30;
    const std::string trasResize = h.getDiffOutput();
    CHECK(trasResize.find("\x1b[2J\x1b[H") != std::string::npos);
    CHECK_EQ(h.r.lastViewportH(), 30);
}

// Contrato de composicion a nivel diff: ChromeData = StatusBar + MessageBar.
// Solo-cambia-statusBar reescribe solo la fila 1 del chrome; solo-cambia-message
// reescribe solo la fila 2. Las filas se identifican por su CUP de rewrite
// (col 1); el cursor cae en el contenido (col != 1).
TEST(render_diff_chrome_status_and_message_isolated) {
    DiffHarness h(10);
    Buffer& b = h.buf();
    b.viewport.height = 6;
    b.viewport.width = 40;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    // contentH == viewport.height cuando hay chrome (ver calculateLayout).
    const int contentH = b.viewport.height;
    const std::string statusCup =
        "\x1b[" + std::to_string(contentH + 1) + ";1H";
    const std::string messageCup =
        "\x1b[" + std::to_string(contentH + 2) + ";1H";

    // Prime: frame completo con a.txt / msg uno.
    std::string prime = h.r.buildDiffFrame(
        b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("msg uno", MessageKind::Info, std::nullopt),
                          State::Navegacion),
        b.selection);
    CHECK(prime.find("\x1b[2J") != std::string::npos);

    // Solo StatusBar (mismo Message): debe tocar fila 1, no fila 2.
    std::string deltaStatus = h.r.buildDiffFrame(
        b.document, b.cursor, b.viewport, "b.txt", false,
        makeChromeRequest(Message("msg uno", MessageKind::Info, std::nullopt),
                          State::Navegacion),
        b.selection);
    CHECK(deltaStatus.find("\x1b[2J") == std::string::npos);
    CHECK(deltaStatus.find(statusCup) != std::string::npos);
    CHECK(deltaStatus.find(messageCup) == std::string::npos);

    // Solo MessageBar (mismo StatusBar): debe tocar fila 2, no fila 1.
    std::string deltaMessage = h.r.buildDiffFrame(
        b.document, b.cursor, b.viewport, "b.txt", false,
        makeChromeRequest(Message("msg dos", MessageKind::Info, std::nullopt),
                          State::Navegacion),
        b.selection);
    CHECK(deltaMessage.find("\x1b[2J") == std::string::npos);
    CHECK(deltaMessage.find(messageCup) != std::string::npos);
    CHECK(deltaMessage.find(statusCup) == std::string::npos);
}

// Mismo texto, distinto MessageKind: el estilo del MessageBar cambia, asi
// que el diff debe repintar la fila inferior (congela que `kind` forma parte
// de la igualdad visual del ChromeData). Complemento: mismo texto y kind
// (el `expiry` ya no viaja al rendering) -> sin repintado del chrome.
TEST(render_diff_mismo_texto_distinto_kind_repinta) {
    DiffHarness h(10);
    Buffer& b = h.buf();
    b.viewport.height = 6;
    b.viewport.width = 40;
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.cursor.line = 0;
    b.cursor.col = 0;
    const int contentH = b.viewport.height;
    const std::string statusCup =
        "\x1b[" + std::to_string(contentH + 1) + ";1H";
    const std::string messageCup =
        "\x1b[" + std::to_string(contentH + 2) + ";1H";

    // Prime: "hola" Info.
    std::string prime = h.r.buildDiffFrame(
        b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("hola", MessageKind::Info, std::nullopt),
                          State::Navegacion),
        b.selection);
    CHECK(prime.find("\x1b[2J") != std::string::npos);

    // Mismo texto, distinto kind: repinta la fila inferior, no la superior.
    std::string deltaKind = h.r.buildDiffFrame(
        b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("hola", MessageKind::Error, std::nullopt),
                          State::Navegacion),
        b.selection);
    CHECK(deltaKind.find("\x1b[2J") == std::string::npos);
    CHECK(deltaKind.find(messageCup) != std::string::npos);
    CHECK(deltaKind.find(statusCup) == std::string::npos);

    // Misma presentación (texto y kind): sin repintado del chrome.
    // El `expiry` temporal del Message ya no viaja al rendering por
    // construcción (makeChromeRequest solo lleva texto + tipo), asi que dos
    // Messages que difieren solo en vencimiento producen el mismo request.
    std::string deltaExpiry = h.r.buildDiffFrame(
        b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("hola", MessageKind::Error, std::nullopt),
                          State::Navegacion),
        b.selection);
    CHECK(deltaExpiry.find("\x1b[2J") == std::string::npos);
    CHECK(deltaExpiry.find(messageCup) == std::string::npos);
    CHECK(deltaExpiry.find(statusCup) == std::string::npos);
}
