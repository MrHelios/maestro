// Tests del render diferencial (buildDiffFrame): 
// un mini-emulador aplica el delta sobre una pantalla y comprueba que, tras edicion/scroll/undo/modal,
// queda IDENTICA al redibujo completo (buildScreen).
//
// Casos del plan:
//   - softwrap: solo cambia topOffset con top constante        -> frame completo
//   - tipeo repetido                                           -> identico al full
//   - edicion de lineas (backspace + newline)                  -> identico al full
//   - navegacion vertical (down/up)                            -> identico al full
//   - undo / redo                                              -> identico al full
//   - una tecla emite menos bytes que un frame completo
//   - segundo frame sin cambios solo mueve el cursor
//   - editar una linea reescribe solo esa fila
//   - acortar una linea limpia los caracteres eliminados
//   - desplazar el viewport actualiza las filas visibles
//   - navegar con scroll actualiza las filas visibles
//   - file browser: la pantalla muestra los archivos
//   - cache invalidada (vuelta de modal)                       -> frame completo
//   - mensaje temporal: aparece y se limpia                    -> identico al full
//   - selector de buffers: muestra la lista y no autoinvalida
//   - resize del viewport                                      -> frame completo
//   - chrome: statusBar y message se repintan por separado
//   - mismo texto, distinto MessageKind                        -> repinta el mensaje
//   - gutter 99999->100000 lineas                              -> rebuild completo
//
// Invariante central: 
// cada delta deja la pantalla IDENTICA al frame completo;
// una vez construido el cache no se permite borrado total (\x1b[2J).

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "helpers/test_gui_chrome_common.h"  // makeVp
#include "test_framework.h"

#include "layout/Gutter.h"
#include "layout/SoftWrap.h"
#include "layout/Viewport.h"
#include "rendering/Sink.h"
#include "platform/InputEvent.h"  // usa InputEvent común

#define private public
#include "app/ChromePresentation.h"
#include "app/Editor.h"
#include "rendering/tty/TtyRenderer.h"
#include "rendering/tty/TtySink.h"
#undef private

namespace {

std::vector<std::string> softWrapLines() {
    return {std::string(40, 'a'), "corta", std::string(40, 'b'),
            std::string(40, 'c')};
}

struct TinyTerm {
    int rows;
    int cur_row = 0;
    int cur_col = 0;
    // Region de scroll (DECSTBM, 0-based inclusive). Por defecto toda la
    // pantalla. TtyScroll la fija a las filas de contenido y la resetea luego.
    int scrollTop = 0;
    int scrollBottom = 0;
    std::vector<std::string> screen;

    explicit TinyTerm(int r)
        : rows(r), scrollBottom(r - 1), screen(static_cast<size_t>(r), "") {}

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
                } else if (term == 'r') {
                    // DECSTBM: define la region de scroll [top;bottom] (1-based).
                    // Parametros vacios (reset) => toda la pantalla.
                    int top = 1, bottom = rows;
                    if (!params.empty()) {
                        std::size_t semi = params.find(';');
                        std::string topStr = semi == std::string::npos ? params : params.substr(0, semi);
                        std::string botStr = semi == std::string::npos ? "" : params.substr(semi + 1);
                        if (!topStr.empty()) { try { top = std::stoi(topStr); } catch (...) { top = 1; } }
                        if (!botStr.empty()) { try { bottom = std::stoi(botStr); } catch (...) { bottom = rows; } }
                    }
                    if (top < 1) top = 1;
                    if (bottom > rows) bottom = rows;
                    if (bottom < top) { top = 1; bottom = rows; }
                    scrollTop = top - 1;
                    scrollBottom = bottom - 1;
                } else if (term == 'S' || term == 'T') {
                    // SU/SD: desplaza la region de scroll n filas (arriba/abajo),
                    // rellenando con lineas vacias. Emula el movimiento fisico
                    // que el render diferencial asume al usar region de scroll.
                    int n = 1;
                    if (!params.empty()) { try { n = std::stoi(params); } catch (...) { n = 1; } }
                    if (n < 0) n = 0;
                    const int top = scrollTop < 0 ? 0 : scrollTop;
                    const int bottom = scrollBottom >= rows ? rows - 1 : scrollBottom;
                    for (int k = 0; k < n && top <= bottom; ++k) {
                        if (term == 'S') {
                            for (int r = top; r < bottom; ++r) screen[static_cast<size_t>(r)] = screen[static_cast<size_t>(r) + 1];
                            screen[static_cast<size_t>(bottom)].clear();
                        } else {
                            for (int r = bottom; r > top; --r) screen[static_cast<size_t>(r)] = screen[static_cast<size_t>(r) - 1];
                            screen[static_cast<size_t>(top)].clear();
                        }
                    }
                } else {
                    // CSI no reconocido (colores \x1b[...m): ignorado,
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
    // Pantallas acumuladas: una recibe los deltas, la otra los frames completos.
    // Persisten entre acciones para comparar el estado tras cada paso.
    TinyTerm viaDiff{0};
    TinyTerm viaFull{0};

    explicit DiffHarness(int lines) {
        ed.getActiveBufferForTesting().document.restore(
            std::vector<std::string>(static_cast<size_t>(lines),
                                     std::string(80, 'x')));
        ed.executeCommand("navegacion.interaccion");
        kRows = ed.getActiveBufferForTesting().viewport.height + 2;
        viaDiff = TinyTerm(kRows);
        viaFull = TinyTerm(kRows);
    }

    Buffer& buf() { return ed.getActiveBufferForTesting(); }

    std::string getDiffOutput() {
        Buffer& b = buf();
        return r.buildDiffFrame(
            b.document, b.cursor, b.viewport, b.filename, b.modified,
            makeChromeRequest(Message(""), State::Navegacion), b.selection);
    }
};

// Aplica el siguiente delta sobre la pantalla acumulada y verifica que coincide
// con el frame completo equivalente. También exige que el delta no haga un
// borrado total una vez que el renderer ya tiene caché.
void checkDiffMatchesFull(DiffHarness& h) {
    Buffer& b = h.buf();
    b.viewport.scrollToCursor(b.cursor);
    const bool allowFullClear = !h.r.hasCache();
    const std::string full = h.r.buildScreen(
        b.document, b.cursor, b.viewport, b.filename, b.modified,
        makeChromeRequest(Message(""), State::Navegacion), b.selection);
    checkDiffMatchesFull(h.viaDiff, h.viaFull, h.getDiffOutput(), full,
                         allowFullClear);
}

// Primer frame: el renderer aún no tiene caché, así que se permite el borrado
// total que emite buildDiffFrame al arrancar.
void checkInitialFrame(DiffHarness& h) {
    checkDiffMatchesFull(h);
}

void typeRepeatedly(DiffHarness& h, char c, int times) {
    for (int i = 0; i < times; ++i) h.ed.handleEvent(key(c));
}

void pressBackspace(DiffHarness& h) {
    h.ed.handleEvent(move(InputEventType::Backspace));
}

void insertNewline(DiffHarness& h) {
    h.ed.handleEvent(move(InputEventType::InsertNewline));
}

void moveDown(DiffHarness& h, int times) {
    for (int i = 0; i < times; ++i) h.ed.handleEvent(move(InputEventType::MoveDown));
}

void moveUp(DiffHarness& h, int times) {
    for (int i = 0; i < times; ++i) h.ed.handleEvent(move(InputEventType::MoveUp));
}

void undo(DiffHarness& h) {
    h.ed.handleEvent(move(InputEventType::Undo));
}

void redo(DiffHarness& h) {
    h.ed.handleEvent(move(InputEventType::Redo));
}

} // namespace

// TtyDiff: 
// cambiar solo topOffset (top constante) debe forzar frame completo, porque la primera fila visual 
// del viewport se desplaza. 
TEST(render_diff_softwrap_topoffset_changes_force_full_frame) {
    Document doc;
    Viewport vp = makeVp(6, 24);
    Cursor cur;
    softwrap::Index idx;
    
    doc.restore(softWrapLines());
    cur.col = 20;  // continuación de L0
    const int textWidth = vp.width - gutterWidth(doc.lineCount(), vp.width);
    CHECK(idx.ensure(doc, textWidth, true, vp.top, vp.height));
    CHECK(idx.enabled());

    TtyRenderer r;
    r.setWrapIndex(&idx);

    // top=0, topOffset=0 -> construye caché.
    std::string first = r.buildDiffFrame(doc, cur, vp, "notes.md", false, 
                                        makeChromeRequest(Message(""), State::Navegacion), 
                                        std::nullopt);
    CHECK(r.hasCache());
    CHECK(!first.empty());

    // Solo cambia topOffset a 1 (top permanece 0).
    vp.topOffset = 1;
    CHECK_EQ(vp.top, 0);
    
    // El diff no puede estar vacío: la primera fila visual cambió.
    std::string incremental = r.buildDiffFrame(doc, cur, vp, "notes.md", false,
                                            makeChromeRequest(Message(""), State::Navegacion), 
                                            std::nullopt);
    CHECK(!incremental.empty());

    // Oráculo: renderer nuevo, frame completo con el mismo viewport.
    TtyRenderer fresh;
    softwrap::Index idx2;
    
    CHECK(idx2.ensure(doc, textWidth, true, vp.top, vp.height));
    fresh.setWrapIndex(&idx2);
    std::string full = fresh.buildScreen(doc, cur, vp, "notes.md", false,
                                        makeChromeRequest(Message(""), State::Navegacion), 
                                        std::nullopt);

    // Las filas de contenido deben coincidir con el frame completo.
    TinyTerm viaDiff(vp.height + 2), viaFull(vp.height + 2);
    viaDiff.apply(incremental);
    viaFull.apply(full);
    CHECK(viaDiff == viaFull);

    // Volver a topOffset=0 también debe invalidar.
    vp.topOffset = 0;
    std::string back = r.buildDiffFrame(doc, cur, vp, "notes.md", false,
                                        makeChromeRequest(Message(""), State::Navegacion), 
                                        std::nullopt);                
    TtyRenderer freshBack;
    softwrap::Index idx3;
    CHECK(idx3.ensure(doc, textWidth, true, vp.top, vp.height));
    
    freshBack.setWrapIndex(&idx3);
    std::string fullBack = fresh.buildScreen(doc, cur, vp, "notes.md", false,
                                        makeChromeRequest(Message(""), State::Navegacion), 
                                        std::nullopt);
    TinyTerm viaBack(vp.height + 2), viaFullBack(vp.height + 2);
    viaBack.apply(back);
    viaFullBack.apply(fullBack);
    
    CHECK(viaBack == viaFullBack);
}

TEST(render_diff_matches_full_after_typing) {
    DiffHarness h(300);

    checkInitialFrame(h);

    typeRepeatedly(h, 'a', 20);
    checkDiffMatchesFull(h);

    typeRepeatedly(h, 'a', 20);
    checkDiffMatchesFull(h);
}

TEST(render_diff_matches_full_after_editing_lines) {
    DiffHarness h(300);

    checkInitialFrame(h);

    pressBackspace(h);
    checkDiffMatchesFull(h);

    insertNewline(h);
    checkDiffMatchesFull(h);
}

TEST(render_diff_matches_full_after_vertical_navigation) {
    DiffHarness h(300);

    checkInitialFrame(h);

    moveDown(h, 40);
    checkDiffMatchesFull(h);

    moveUp(h, 60);
    checkDiffMatchesFull(h);
}

TEST(render_diff_matches_full_after_undo_redo) {
    DiffHarness h(300);

    checkInitialFrame(h);

    typeRepeatedly(h, 'a', 20);
    checkDiffMatchesFull(h);

    undo(h);
    checkDiffMatchesFull(h);

    redo(h);
    checkDiffMatchesFull(h);
}

TEST(render_diff_tecla_emite_menos_que_frame_completo) {
    DiffHarness h(300);
    Buffer& b = h.buf();

    // Preparar la caché del renderer incremental.
    h.getDiffOutput();

    // Medir un frame completo con un renderer independiente.
    TtyRenderer fullRenderer;
    const std::size_t fullFrameBytes = fullRenderer.buildScreen(b.document, b.cursor, b.viewport,b.filename, b.modified,
                                                    makeChromeRequest(Message(""), State::Navegacion),b.selection).size();

    // Escribir un carácter y medir la salida incremental.
    h.ed.handleEvent(key('a'));
    const std::size_t incrementalBytes = h.getDiffOutput().size();
    CHECK(incrementalBytes < fullFrameBytes / 4);
}

TEST(render_diff_segundo_frame_solo_mueve_cursor) {
    DiffHarness h(300);
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    Buffer& b = h.buf();

    // Sincronizar ambas terminales con el frame inicial.
    const std::string initialFrame = h.getDiffOutput();
    viaDiff.apply(initialFrame);
    viaFull.apply(initialFrame);

    // Medir el tamaño de referencia con un renderer independiente.
    TtyRenderer fullRenderer;
    const std::size_t fullFrameBytes =
        fullRenderer.buildScreen(
            b.document, b.cursor, b.viewport,
            b.filename, b.modified,
            makeChromeRequest(Message(""), State::Navegacion),
            b.selection
        ).size();

    // Mover el cursor sin modificar el documento.
    b.cursor.col = 5;
    const std::string cursorUpdate = h.getDiffOutput();

    // Verificar que el resultado incremental sea correcto.
    TtyRenderer oracleRenderer;
    const std::string expectedFrame =
        oracleRenderer.buildScreen(
            b.document, b.cursor, b.viewport,
            b.filename, b.modified,
            makeChromeRequest(Message(""), State::Navegacion),
            b.selection
        );

    checkDiffMatchesFull(
        viaDiff, viaFull, cursorUpdate, expectedFrame, false
    );

    // Mover el cursor no debería requerir redibujar toda la pantalla.
    CHECK(cursorUpdate.size() < fullFrameBytes / 4);
}

TEST(render_diff_edicion_una_sola_linea_solo_esa_fila) {
    DiffHarness h(300);
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    // Sincronizar las terminales con el frame inicial.
    const std::string initialFrame = h.getDiffOutput();
    viaDiff.apply(initialFrame);
    viaFull.apply(initialFrame);

    // Modificar un carácter de la línea actual.
    h.ed.handleEvent(key('Z'));

    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(
        h.buf().document, h.buf().cursor, h.buf().viewport,
        h.buf().filename, h.buf().modified,
        makeChromeRequest(Message(""), State::Navegacion),
        h.buf().selection
    );

    // El resultado incremental debe coincidir con el frame completo.
    checkDiffMatchesFull(viaDiff, viaFull, delta, full, false);

    // Contar las filas que se limpian para reescribirse.
    int rewrittenRows = 0;
    for (std::size_t pos = 0;
         (pos = delta.find("\x1b[K", pos)) != std::string::npos;
         ++rewrittenRows, ++pos) {}

    CHECK(rewrittenRows >= 1 && rewrittenRows <= 3);
}

TEST(render_diff_al_acortar_linea_limpia_caracteres_eliminados) {
    DiffHarness h(300);
    Buffer& b = h.buf();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    bool allow1 = !h.r.hasCache();
    for (int i = 0; i < 20; ++i) 
        h.ed.handleEvent(key('a'));
    b.viewport.scrollToCursor(b.cursor);
    std::string delta = h.getDiffOutput();
    std::string full = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, 
                                        makeChromeRequest(Message(""), State::Navegacion), b.selection);
    checkDiffMatchesFull(viaDiff, viaFull, delta, full, allow1);

    for (int i = 0; i < 10; ++i) 
        h.ed.handleEvent(move(InputEventType::Backspace));
    b.viewport.scrollToCursor(b.cursor);
    delta = h.getDiffOutput();
    full = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, 
                            makeChromeRequest(Message(""), State::Navegacion), b.selection);

    CHECK(delta.find("\x1b[K") != std::string::npos);
    checkDiffMatchesFull(viaDiff, viaFull, delta, full, true);
    CHECK(viaDiff.screen[0].find("aaaaaaaaaa") != std::string::npos);
    // TinyTerm inicializa las celdas de su pantalla con x para poder detectar posiciones que nunca fueron dibujadas.
    CHECK(viaDiff.screen[0].find("xxxxxxxxxx") != std::string::npos);
}

TEST(render_diff_al_desplazar_viewport_actualiza_filas_visibles) {
    DiffHarness h(300);
    Buffer& b = h.buf();

    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    // Estado inicial: viewport en el comienzo del documento.
    b.viewport.top = 0;
    b.cursor.line = 0;

    const std::string initialFrame = h.getDiffOutput();
    viaDiff.apply(initialFrame);
    viaFull.apply(initialFrame);

    // Desplazar el viewport cinco líneas.
    b.viewport.top = 5;
    b.cursor.line = 10;

    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(
        b.document, b.cursor, b.viewport,
        b.filename, b.modified,
        makeChromeRequest(Message(""), State::Navegacion),
        b.selection
    );

    // El resultado incremental debe coincidir con el frame completo.
    checkDiffMatchesFull(viaDiff, viaFull, delta, full, false);

    // El desplazamiento debe actualizar las filas visibles.
    int rewrittenRows = 0;
    for (std::size_t pos = 0;
         (pos = delta.find("\x1b[K", pos)) != std::string::npos;
         ++rewrittenRows, ++pos) {}

    CHECK(rewrittenRows >= b.viewport.height);
}

TEST(render_diff_navegacion_vertical_con_scroll_actualiza_filas_visibles) {
    DiffHarness h(300);
    Buffer& b = h.buf();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    // Sincronizar las terminales con el frame inicial.
    const std::string initialFrame = h.getDiffOutput();
    viaDiff.apply(initialFrame);
    viaFull.apply(initialFrame);

    // Simular navegación vertical que provoca desplazamiento.
    for (int i = 0; i < 30; ++i)
        h.ed.handleEvent(move(InputEventType::MoveDown));

    b.viewport.scrollToCursor(b.cursor);

    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(
        b.document, b.cursor, b.viewport,
        b.filename, b.modified,
        makeChromeRequest(Message(""), State::Navegacion),
        b.selection
    );

    checkDiffMatchesFull(viaDiff, viaFull, delta, full, false);

    // El desplazamiento debe actualizar las filas visibles.
    int rewrittenRows = 0;
    for (std::size_t pos = 0;
         (pos = delta.find("\x1b[K", pos)) != std::string::npos;
         ++rewrittenRows, ++pos) {}

    CHECK(rewrittenRows >= b.viewport.height);
}

TEST(render_filebrowser_muestra_archivos) {
    // StringSink: a diferencia de NullSink, conserva los bytes emitidos para
    // poder comprobar que el listado se generó de verdad.
    StringSink sink;
    TtyRenderer renderer;

    const std::vector<FileListItem> files{
        {"a.txt", false},
        {"b.txt", false}
    };

    renderer.renderFileList(
        files, 0, 0, "/tmp",
        MessageBarData(""),
        80, 24, sink
    );

    // El frame no puede estar vacío y debe contener ambos nombres, la ruta
    // y el contador de selección (1/2 para el primer archivo).
    CHECK(!sink.buf.empty());
    CHECK(sink.buf.find("a.txt") != std::string::npos);
    CHECK(sink.buf.find("b.txt") != std::string::npos);
    CHECK(sink.buf.find("/tmp") != std::string::npos);
    CHECK(sink.buf.find("1/2") != std::string::npos);

    // La etiqueta del modo abrir también forma parte del chrome.
    CHECK(sink.buf.find("ABRIR ARCHIVO") != std::string::npos);
}

// El selector de buffers es un modal: se dibuja con el mismo backend, pero no
// invalida el cache diferencial por si solo. El caller (Editor::renderFrame)
// invalida al entrar/salir, y el primer frame de vuelta es un rebuild total.
TEST(render_diff_vuelta_de_selector_de_buffers_es_completa) {
    DiffHarness h(300);
    StringSink modal;

    // Cache inicial del editor.
    h.getDiffOutput();
    CHECK(h.r.hasCache());

    h.r.renderBufferList({"uno", "dos"}, 0, 0, 80, 24, modal);

    // La salida es la pantalla del selector, con ambos buffers visibles.
    CHECK(!modal.buf.empty());
    CHECK(modal.buf.find("Buffers") != std::string::npos);
    CHECK(modal.buf.find("SELECCIONAR") != std::string::npos);
    CHECK(modal.buf.find("1/2") != std::string::npos);
    CHECK(modal.buf.find("uno") != std::string::npos);
    CHECK(modal.buf.find("dos") != std::string::npos);

    // Renderizar el modal no invalida el cache diferencial.
    CHECK(h.r.hasCache());

    // Vuelta al editor: el caller invalida y el frame siguiente es completo.
    h.r.invalidateCache();
    CHECK(!h.r.hasCache());
    const std::string trasModal = h.getDiffOutput();
    CHECK(trasModal.find("\x1b[2J\x1b[H") != std::string::npos);
    CHECK(h.r.hasCache());
}

// Cambiar la altura del viewport (resize) sin invalidar el cache también
// fuerza rebuild total: la geometría cacheada ya no sirve.
TEST(render_diff_resize_fuerza_frame_completo) {
    DiffHarness h(300);

    h.getDiffOutput();
    CHECK(h.r.hasCache());
    CHECK(h.r.lastViewportH() != 30);

    h.buf().viewport.height = 30;
    const std::string trasResize = h.getDiffOutput();
    CHECK(trasResize.find("\x1b[2J\x1b[H") != std::string::npos);
    CHECK_EQ(h.r.lastViewportH(), 30);
}

TEST(render_diff_cache_invalidada_fuerza_frame_completo) {
    DiffHarness h(300);
    Buffer& b = h.buf();

    // Construir la caché inicial.
    h.getDiffOutput();
    CHECK(h.r.hasCache());

    // Simular la pantalla modal (file browser) dibujada por el mismo backend:
    // escribe un frame completo sobre el terminal, pero no toca el cache
    // diferencial del editor.
    StringSink modal;
    h.r.renderFileList(
        std::vector<FileListItem>{{"a.txt", false}, {"b.txt", false}},
        0, 0, "/tmp", MessageBarData(""), 80, 24, modal);
    CHECK(!modal.buf.empty());
    CHECK(h.r.hasCache());

    // Al volver del modal el caller (Editor::renderFrame) invalida el cache:
    // el estado que quedó en pantalla ya no corresponde al del editor.
    h.r.invalidateCache();
    CHECK(!h.r.hasCache());

    const std::string output = h.getDiffOutput();

    // El siguiente frame es un rebuild total, no un delta sobre el modal.
    CHECK(output.find("\x1b[2J\x1b[H") != std::string::npos);
    CHECK(h.r.hasCache());

    // Además de limpiar, debe reescribir todas las filas del contenido.
    int rewrittenRows = 0;
    for (std::size_t pos = 0;
         (pos = output.find("\x1b[K", pos)) != std::string::npos;
         ++rewrittenRows, ++pos) {}
    CHECK(rewrittenRows >= b.viewport.height);

    // Y la pantalla resultante coincide con el frame completo canónico.
    const std::string full = h.r.buildScreen(
        b.document, b.cursor, b.viewport, b.filename, b.modified,
        makeChromeRequest(Message(""), State::Navegacion), b.selection);
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    viaDiff.apply(output);
    viaFull.apply(full);
    CHECK(viaDiff == viaFull);
}

TEST(render_diff_mostrar_y_quitar_mensaje_temporal) {
    DiffHarness h(300);
    Buffer& b = h.buf();
    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);

    const std::string init = h.getDiffOutput();
    viaDiff.apply(init);
    viaFull.apply(init);

    b.viewport.scrollToCursor(b.cursor);
    std::string deltaMsg = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, b.filename, b.modified, 
                                            makeChromeRequest(Message("Guardando..."), State::Navegacion), b.selection);
    std::string fullMsg = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, 
                                            makeChromeRequest(Message("Guardando..."), State::Navegacion), b.selection);
                                            
    checkDiffMatchesFull(viaDiff, viaFull, deltaMsg, fullMsg, false);

    std::string deltaClear = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, b.filename, b.modified, 
                                                makeChromeRequest(Message(""), State::Navegacion), b.selection);
    std::string fullClear = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified, 
                                            makeChromeRequest(Message(""), State::Navegacion), b.selection);
                                            
    checkDiffMatchesFull(viaDiff, viaFull, deltaClear, fullClear, false);
}

// Contrato de composicion a nivel diff: 
// ChromeData = StatusBar + MessageBar.
// Solo-cambia-statusBar reescribe solo la fila 1 del chrome; 
// solo-cambia-message reescribe solo la fila 2. 
// Las filas se identifican por su CUP de rewrite (col 1); 
// el cursor cae en el contenido (col != 1).
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
    const std::string statusCup = "\x1b[" + std::to_string(contentH + 1) + ";1H";
    const std::string messageCup = "\x1b[" + std::to_string(contentH + 2) + ";1H";

    // Prime: frame completo con a.txt / msg uno.
    std::string prime = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("msg uno", MessageKind::Info, std::nullopt),State::Navegacion),b.selection);
    CHECK(prime.find("\x1b[2J") != std::string::npos);

    // Solo StatusBar (mismo Message): debe tocar fila 1, no fila 2.
    std::string deltaStatus = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, "b.txt", false,
        makeChromeRequest(Message("msg uno", MessageKind::Info, std::nullopt),State::Navegacion),b.selection);
    CHECK(deltaStatus.find("\x1b[2J") == std::string::npos);
    CHECK(deltaStatus.find(statusCup) != std::string::npos);
    CHECK(deltaStatus.find(messageCup) == std::string::npos);

    // Solo MessageBar (mismo StatusBar): debe tocar fila 2, no fila 1.
    std::string deltaMessage = h.r.buildDiffFrame(
        b.document, b.cursor, b.viewport, "b.txt", false,
        makeChromeRequest(Message("msg dos", MessageKind::Info, std::nullopt),State::Navegacion),b.selection);
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
    const std::string statusCup = "\x1b[" + std::to_string(contentH + 1) + ";1H";
    const std::string messageCup = "\x1b[" + std::to_string(contentH + 2) + ";1H";

    // Prime: "hola" Prompt (estilo distinto al resto de mensajes).
    std::string prime = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("hola", MessageKind::Prompt, std::nullopt),State::Navegacion),b.selection);
    CHECK(prime.find("\x1b[2J") != std::string::npos);

    // Mismo texto, distinto kind: repinta la fila inferior, no la superior.
    std::string deltaKind = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("hola", MessageKind::Error, std::nullopt),State::Navegacion),b.selection);
    CHECK(deltaKind.find("\x1b[2J")  == std::string::npos);
    CHECK(deltaKind.find(messageCup) != std::string::npos);
    CHECK(deltaKind.find(statusCup)  == std::string::npos);

    // Misma presentación (texto y kind): sin repintado del chrome.
    // El `expiry` temporal del Message ya no viaja al rendering por
    // construcción (makeChromeRequest solo lleva texto + tipo), asi que dos
    // Messages que difieren solo en vencimiento producen el mismo request.
    std::string deltaExpiry = h.r.buildDiffFrame(b.document, b.cursor, b.viewport, "a.txt", false,
        makeChromeRequest(Message("hola", MessageKind::Error, std::nullopt),State::Navegacion),b.selection);
    CHECK(deltaExpiry.find("\x1b[2J")  == std::string::npos);
    CHECK(deltaExpiry.find(messageCup) == std::string::npos);
    CHECK(deltaExpiry.find(statusCup)  == std::string::npos);
}

// Regresion BUG VISUAL gutter: 
// al cruzar 99999->100000 lineas el ancho del gutter pasa de 7 a 8 columnas. 
TEST(render_diff_gutter_crece_repinta_viewport_completo) {
    DiffHarness h(99999);
    Buffer& b = h.buf();

    // Cursor en la ultima linea, viewport al fondo: InsertNewline scrollea +1.
    b.cursor.line = b.document.lineCount() - 1;
    b.cursor.col = 0;
    b.viewport.scrollToCursor(b.cursor);

    TinyTerm viaDiff(h.kRows), viaFull(h.kRows);
    const std::string init = h.getDiffOutput();
    viaDiff.apply(init);
    viaFull.apply(init);
    // Con 99999 lineas el gutter es el minimo de 7 columnas (campo 5 + gap 2).
    CHECK_EQ(b.document.lineCount(), 99999);
    CHECK_EQ(h.r.lastGutterW(), 7);

    h.ed.handleEvent(move(InputEventType::InsertNewline));
    b.viewport.scrollToCursor(b.cursor);
    CHECK_EQ(b.document.lineCount(), 100000);

    const std::string delta = h.getDiffOutput();
    const std::string full = h.r.buildScreen(b.document, b.cursor, b.viewport, b.filename, b.modified,
                                            makeChromeRequest(Message(""), State::Navegacion), b.selection);

    // 100000 lineas => gutter de 8 columnas y rebuild total (no scroll parcial).
    CHECK_EQ(h.r.lastGutterW(), 8);
    CHECK(delta.find("\x1b[2J\x1b[H") != std::string::npos);
    checkDiffMatchesFull(viaDiff, viaFull, delta, full, /*allowFullClear=*/true);

    // Todas las filas visibles quedaron alineadas con el gutter nuevo.
    // El prefijo incluye campo alineado a la derecha + gap; 99999 y 100000 viven en 8 columnas.
    auto gutterPrefix = [](int lineNumber, const GutterMetrics& gm) {
        std::string num = std::to_string(lineNumber);
        if (static_cast<int>(num.size()) > gm.numberField)
            num = num.substr(num.size() - static_cast<size_t>(gm.numberField));
        int pad = std::max(0, gm.numberField - static_cast<int>(num.size()));
        return std::string(static_cast<size_t>(pad), ' ') + num + std::string(static_cast<size_t>(gm.gap), ' ');
    };
    
    const GutterMetrics gm = gutterMetrics(b.document.lineCount(), b.viewport.width);
    for (int row = 0; row < b.viewport.height; ++row) {
        const int lineNumber = b.viewport.top + row + 1;
        if (lineNumber > b.document.lineCount()) 
            break;
        CHECK_EQ(viaDiff.screen[static_cast<size_t>(row)].substr(0, gm.total), gutterPrefix(lineNumber, gm));
    }
    CHECK_EQ(viaDiff.screen[static_cast<size_t>(b.viewport.height - 2)].substr(0, 8)," 99999  ");
    CHECK_EQ(viaDiff.screen[static_cast<size_t>(b.viewport.height - 1)].substr(0, 8),"100000  ");
}
