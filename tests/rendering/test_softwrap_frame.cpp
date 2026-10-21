#include <string>

#include "app/ChromePresentation.h"
#include "app/EditorState.h"
#include "document/Cursor.h"
#include "document/Document.h"
#include "layout/SoftWrap.h"
#include "layout/Viewport.h"
#include "rendering/frame/Frame.h"
#include "rendering/frame/FrameBuilder.h"
#include "rendering/tty/TtyRenderer.h"
#include "test_framework.h"

namespace {

Viewport viewport(int top, int height, int width) {
    Viewport vp;
    vp.top = top;
    vp.left = 0;
    vp.height = height;
    vp.width = width;
    return vp;
}

} // namespace

TEST(softwrap_frame_cursor_uses_visual_row) {
    Document doc;
    doc.restore({"abcdefghij"});
    softwrap::Index idx;
    CHECK(idx.ensure(doc, 7, true)); // textWidth = width(14) - gutter(7)

    Viewport vp = viewport(0, 5, 14);
    Cursor cursor;
    cursor.line = 0;
    cursor.col = 8; // segunda fila visual, columna relativa 1

    FrameBuilder fb;
    Frame f = fb.buildFrame(doc, cursor, vp, "notes.md", false,
                            makeChromeRequest("", State::Navegacion),
                            std::nullopt, std::nullopt, std::nullopt, &idx);

    CHECK(f.cursor.visible);
    CHECK_EQ(f.cursor.cell.row, 1);
    CHECK_EQ(f.cursor.cell.col, 8); // gutter 7 + columna relativa 1
}

TEST(softwrap_frame_clamps_viewport_top_for_cursor) {
    Document doc;
    doc.restore({"abcdefghij"});
    softwrap::Index idx;
    CHECK(idx.ensure(doc, 7, true));

    // top fuera del máximo válido: contenido y cursor deben usar el mismo
    // top corregido (0), no el valor crudo.
    Viewport vp = viewport(99, 5, 10);
    Cursor cursor;
    cursor.line = 0;
    cursor.col = 8;

    FrameBuilder fb;
    Frame f = fb.buildFrame(doc, cursor, vp, "notes.md", false,
                            makeChromeRequest("", State::Navegacion),
                            std::nullopt, std::nullopt, std::nullopt, &idx);

    CHECK(f.cursor.visible);
    CHECK_EQ(f.cursor.cell.row, 1);
    CHECK_EQ(f.cursor.cell.col, 8);
}

TEST(softwrap_frame_cursor_at_boundary_starts_next_row) {
    Document doc;
    doc.restore({"abcdefghij"});
    softwrap::Index idx;
    CHECK(idx.ensure(doc, 7, true));

    Viewport vp = viewport(0, 5, 14);
    Cursor cursor;
    cursor.line = 0;
    cursor.col = 7; // límite exacto entre las dos filas visuales

    FrameBuilder fb;
    Frame f = fb.buildFrame(doc, cursor, vp, "notes.md", false,
                            makeChromeRequest("", State::Navegacion),
                            std::nullopt, std::nullopt, std::nullopt, &idx);

    CHECK(f.cursor.visible);
    CHECK_EQ(f.cursor.cell.row, 1);
    CHECK_EQ(f.cursor.cell.col, 7); // inicio de la continuación
}

TEST(softwrap_diff_reuses_cache_when_frame_unchanged) {
    Document doc;
    std::vector<std::string> lines;
    lines.push_back("ALPHA " + std::string(40, 'a'));
    for (int i = 1; i < 12; ++i) lines.push_back(std::string(40, 'b'));
    doc.restore(lines);

    Viewport vp = viewport(0, 5, 24); // gutter 7, textWidth 17
    softwrap::Index idx;
    CHECK(idx.ensure(doc, 17, true));
    CHECK(idx.enabled());

    Cursor cursor;
    cursor.line = 0;
    cursor.col = 0;

    TtyRenderer r;
    r.setWrapIndex(&idx);
    const ChromeRequest chrome = makeChromeRequest("", State::Navegacion);

    const std::string full =
        r.buildDiffFrame(doc, cursor, vp, "notes.md", false, chrome);
    CHECK(r.hasCache());
    CHECK(full.find("ALPHA") != std::string::npos);

    // Mover el cursor dentro de la misma línea visual no cambia ninguna
    // fila: el frame diferencial no debe reconstruir ni reenviar el texto.
    Cursor moved = cursor;
    moved.col = 5;
    const std::string diff =
        r.buildDiffFrame(doc, moved, vp, "notes.md", false, chrome);
    CHECK(diff.find("ALPHA") == std::string::npos);
}
