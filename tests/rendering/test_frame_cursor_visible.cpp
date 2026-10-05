// Contrato visual explicito del cursor (suppressScrollToCursor_):
// visible==true -> cell es la posicion visual real; visible==false -> cell no
// debe usarse (cursor fuera del viewport o Busqueda). El GUI no clampa.
#include <string>

#include "test_framework.h"

#include "app/ChromePresentation.h"
#include "app/EditorState.h"
#include "app/Message.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "layout/Viewport.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/RenderUtil.h"
#include "rendering/tty/TtyEncoder.h"

namespace {

Document makeDoc(int lines = 100) {
    Document doc;
    std::vector<std::string> v;
    for (int i = 0; i < lines; ++i) v.push_back("line " + std::to_string(i));
    doc.restore(v);
    return doc;
}

Viewport makeVp(int top, int height = 10, int width = 80) {
    Viewport vp;
    vp.top = top;
    vp.left = 0;
    vp.height = height;
    vp.width = width;
    return vp;
}

} // namespace

TEST(frame_cursor_visible_inside_viewport) {
    Document doc = makeDoc();
    Viewport vp = makeVp(20);
    Cursor cur;
    cur.line = 25;
    cur.col = 0;
    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    CHECK(f.cursor.visible);
    CHECK(f.cursor.cell.valid());
    CHECK_EQ(f.cursor.cell.row, 25 - 20); // 0-based
    CHECK(f.cursor.shape == FrameCursorShape::Block);
}

TEST(frame_cursor_hidden_below_viewport_wheel_case) {
    // Rueda arriba con cursor abajo: viewport 20->17, cursor 29 off-screen.
    Document doc = makeDoc();
    Viewport vp = makeVp(17);
    Cursor cur;
    cur.line = 29;
    cur.col = 0;
    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    CHECK(!f.cursor.visible);
    CHECK(!f.cursor.cell.valid());
}

TEST(frame_cursor_hidden_above_viewport) {
    Document doc = makeDoc();
    Viewport vp = makeVp(23);
    Cursor cur;
    cur.line = 20;
    cur.col = 0;
    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    CHECK(!f.cursor.visible);
}

TEST(frame_cursor_hidden_horizontal_offscreen) {
    Document doc = makeDoc();
    Viewport vp = makeVp(0);
    vp.left = 50; // cursor col 0 queda fuera a la izquierda
    vp.width = 80;
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    CHECK(!f.cursor.visible);
}

TEST(frame_cursor_hidden_right_offscreen) {
    // Simetrico al caso izquierdo: cursor mas alla de colHi (sin clamp
    // horizontal). Linea larga de 200 celdas, viewport angosto.
    Document doc;
    doc.restore({std::string(200, 'x')});
    Viewport vp = makeVp(0);
    vp.left = 0;
    vp.width = 30;
    Cursor cur;
    cur.line = 0;
    cur.col = 200; // final de la linea larga, fuera por derecha
    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    CHECK(!f.cursor.visible);
    CHECK(!f.cursor.cell.valid());
}

TEST(frame_cursor_busqueda_en_messagebar_aunque_documento_visible) {
    Document doc = makeDoc();
    Viewport vp = makeVp(0);
    Cursor cur;
    cur.line = 0;
    cur.col = 0;
    FrameBuilder b;
    Message prompt{std::string("Find: ho"), MessageKind::Prompt, std::nullopt};
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest(prompt, State::Busqueda),
                           std::nullopt);
    // El cursor del contenido se desactiva; el cursor va al MessageBar.
    CHECK(f.cursor.visible);
    CHECK(f.cursor.cell.valid());
    const CellPos expected = chrome::messageBarCursorCell(f.layout.chrome, f.chrome.message);
    CHECK(expected.valid());
    CHECK(f.cursor.cell == expected);
    CHECK(f.cursor.shape == FrameCursorShape::Bar);
}

TEST(frame_cursor_shape_follows_state) {
    Document doc = makeDoc();
    Viewport vp = makeVp(0);
    Cursor cur;
    FrameBuilder b;
    Frame fNav = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                              std::nullopt);
    CHECK(fNav.cursor.shape == FrameCursorShape::Block);
    Frame fInt = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Interaccion),
                              std::nullopt);
    CHECK(fInt.cursor.visible);
    CHECK(fInt.cursor.shape == FrameCursorShape::Bar);
}

TEST(frame_cursor_editorCursorPos_no_silent_clamp) {
    Document doc = makeDoc();
    Viewport vp = makeVp(17);
    Cursor cur;
    cur.line = 29;
    cur.col = 0;
    FrameBuilder b;
    CellPos pos;
    const bool vis = b.editorCursorPos(doc, cur, vp, pos);
    CHECK(!vis);
    // Sin clamp: celda inválida, no borde del contenido.
    CHECK(!pos.valid());
}

TEST(frame_cursor_tty_hides_when_not_visible) {
    Document doc = makeDoc();
    Viewport vp = makeVp(17);
    Cursor cur;
    cur.line = 29;
    cur.col = 0;
    FrameBuilder b;
    Frame f = b.buildFrame(doc, cur, vp, "t", false, makeChromeRequest("", State::Navegacion),
                           std::nullopt);
    TtyEncoder enc;
    std::string out = enc.encodeFrame(f);
    // Sin estilo de cursor ni show: se deja oculto (hide de beginFrame).
    CHECK(out.find(" q") == std::string::npos);
    CHECK(out.find("\x1b[?25h") == std::string::npos);
}
