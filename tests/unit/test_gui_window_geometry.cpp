#include "test_framework.h"

#include "platform/gui/GuiWindowGeometry.h"

TEST(gui_window_edge_esquinas_y_lados) {
    const int w = 300, h = 200, border = 6;
    
    CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
    CHECK(guiWindowEdgeAt(w - 1, 0, w, h, border) == GuiWindowEdge::TopRight);
    CHECK(guiWindowEdgeAt(0, h - 1, w, h, border) == GuiWindowEdge::BottomLeft);
    CHECK(guiWindowEdgeAt(w - 1, h - 1, w, h, border) == GuiWindowEdge::BottomRight);
    CHECK(guiWindowEdgeAt(0, h / 2, w, h, border) == GuiWindowEdge::Left);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border) == GuiWindowEdge::Right);
    CHECK(guiWindowEdgeAt(w / 2, 0, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, h - 1, w, h, border) == GuiWindowEdge::Bottom);
    CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) == GuiWindowEdge::None);
}

TEST(gui_window_edge_fuera_o_degenerado_es_none) {
    const int w = 300, h = 200, border = 6;

    // fuera de la ventana
    CHECK(guiWindowEdgeAt(-1, 0, w, h, border) == GuiWindowEdge::None);   // x < 0
    CHECK(guiWindowEdgeAt(0, -1, w, h, border) == GuiWindowEdge::None);   // y < 0
    CHECK(guiWindowEdgeAt(w, 100, w, h, border) == GuiWindowEdge::None);  // x == w (ya afuera)
    CHECK(guiWindowEdgeAt(100, h, w, h, border) == GuiWindowEdge::None);  // y == h (ya afuera)

    // degenerado: ventana o borde de tamaño 0
    CHECK(guiWindowEdgeAt(0, 0, 0, h, border) == GuiWindowEdge::None);    // w = 0
    CHECK(guiWindowEdgeAt(0, 0, w, 0, border) == GuiWindowEdge::None);    // h = 0
    CHECK(guiWindowEdgeAt(0, 0, w, h, 0) == GuiWindowEdge::None);         // border = 0
}

TEST(gui_window_resize_right) {
    // (10,20)  -- (310,20)
    // (10,220) -- (310,220)
    const GuiWindowRect start{10, 20, 300, 200};
    
    const GuiWindowRect r = guiResizeWindowRect(start, GuiWindowEdge::Right, 25, 999, 160, 120);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 20);
    CHECK_EQ(r.w, 325);
    CHECK_EQ(r.h, 200);
    CHECK_EQ((r.x + r.w) - (start.x + start.w),25);
    CHECK_EQ((r.y + r.h) - (start.y + start.h),0);
    
    const GuiWindowRect r2 = guiResizeWindowRect(start, GuiWindowEdge::Right, -10, 999, 160, 120);
    CHECK_EQ(r2.x, 10);
    CHECK_EQ(r2.y, 20);
    CHECK_EQ(r2.w, 290);
    CHECK_EQ(r2.h, 200);
    CHECK_EQ((r2.x + r2.w) - (start.x + start.w),-10);
    CHECK_EQ((r2.y + r2.h) - (start.y + start.h),0);

    // bottom right
    const GuiWindowRect br = guiResizeWindowRect(start, GuiWindowEdge::BottomRight, 25, 30, 160, 120);
    CHECK_EQ(br.x, 10);
    CHECK_EQ(br.y, 20);
    CHECK_EQ(br.w, 325);
    CHECK_EQ(br.h, 230);
    CHECK_EQ((br.x + br.w) - (start.x + start.w),25);
    CHECK_EQ((br.y + br.h) - (start.y + start.h),30);
    
    const GuiWindowRect br2 = guiResizeWindowRect(start, GuiWindowEdge::BottomRight, -25, -30, 160, 120);
    CHECK_EQ(br2.x, 10);
    CHECK_EQ(br2.y, 20);
    CHECK_EQ(br2.w, 275);
    CHECK_EQ(br2.h, 170);
    CHECK_EQ((br2.x + br2.w) - (start.x + start.w),-25);
    CHECK_EQ((br2.y + br2.h) - (start.y + start.h),-30);

    // top right
    const GuiWindowRect tr = guiResizeWindowRect(start, GuiWindowEdge::TopRight, 25, 30, 160, 120);
    CHECK_EQ(tr.x, 10);
    CHECK_EQ(tr.y, 50);
    CHECK_EQ(tr.w, 325);
    CHECK_EQ(tr.h, 170);
    CHECK_EQ((tr.x + tr.w) - (start.x + start.w), 25);
    CHECK_EQ(tr.y + tr.h, start.y + start.h);
    
    const GuiWindowRect tr2 = guiResizeWindowRect(start, GuiWindowEdge::TopRight, -10, -10, 160, 120);
    CHECK_EQ(tr2.x, 10);
    CHECK_EQ(tr2.y, 10);
    CHECK_EQ(tr2.w, 290);
    CHECK_EQ(tr2.h, 210);
    CHECK_EQ((tr2.x + tr2.w) - (start.x + start.w), -10);
    CHECK_EQ((tr2.y + tr2.h) - (start.y + start.h), 0);
}

TEST(gui_window_resize_left) {
    // (10,20)  -- (310,20)
    // (10,220) -- (310,220)
    const GuiWindowRect start{10, 20, 300, 200};

    // left: el borde derecho queda fijo
    const GuiWindowRect l = guiResizeWindowRect(start, GuiWindowEdge::Left, 20, 999, 160, 120);
    CHECK_EQ(l.x, 30);
    CHECK_EQ(l.y, 20);
    CHECK_EQ(l.w, 280);
    CHECK_EQ(l.h, 200);
    CHECK_EQ(l.x - start.x, 20);
    CHECK_EQ((l.x + l.w) - (start.x + start.w), 0);
    CHECK_EQ((l.y + l.h) - (start.y + start.h), 0);

    const GuiWindowRect l2 = guiResizeWindowRect(start, GuiWindowEdge::Left, -10, 999, 160, 120);
    CHECK_EQ(l2.x, 0);
    CHECK_EQ(l2.y, 20);
    CHECK_EQ(l2.w, 310);
    CHECK_EQ(l2.h, 200);
    CHECK_EQ(l2.x - start.x, -10);
    CHECK_EQ((l2.x + l2.w) - (start.x + start.w), 0);
    CHECK_EQ((l2.y + l2.h) - (start.y + start.h), 0);

    // top left: derecha y abajo quedan fijos
    const GuiWindowRect tl = guiResizeWindowRect(start, GuiWindowEdge::TopLeft, 20, 30, 160, 120);
    CHECK_EQ(tl.x, 30);
    CHECK_EQ(tl.y, 50);
    CHECK_EQ(tl.w, 280);
    CHECK_EQ(tl.h, 170);
    CHECK_EQ(tl.x - start.x, 20);
    CHECK_EQ(tl.y - start.y, 30);
    CHECK_EQ((tl.x + tl.w) - (start.x + start.w), 0);
    CHECK_EQ((tl.y + tl.h) - (start.y + start.h), 0);

    const GuiWindowRect tl2 = guiResizeWindowRect(start, GuiWindowEdge::TopLeft, -10, -10, 160, 120);
    CHECK_EQ(tl2.x, 0);
    CHECK_EQ(tl2.y, 10);
    CHECK_EQ(tl2.w, 310);
    CHECK_EQ(tl2.h, 210);
    CHECK_EQ(tl2.x - start.x, -10);
    CHECK_EQ(tl2.y - start.y, -10);
    CHECK_EQ((tl2.x + tl2.w) - (start.x + start.w), 0);
    CHECK_EQ((tl2.y + tl2.h) - (start.y + start.h), 0);

    // bottom left: derecha y arriba quedan fijos
    const GuiWindowRect bl = guiResizeWindowRect(start, GuiWindowEdge::BottomLeft, 20, 30, 160, 120);
    CHECK_EQ(bl.x, 30);
    CHECK_EQ(bl.y, 20);
    CHECK_EQ(bl.w, 280);
    CHECK_EQ(bl.h, 230);
    CHECK_EQ(bl.x - start.x, 20);
    CHECK_EQ((bl.x + bl.w) - (start.x + start.w), 0);
    CHECK_EQ((bl.y + bl.h) - (start.y + start.h), 30);

    const GuiWindowRect bl2 = guiResizeWindowRect(start, GuiWindowEdge::BottomLeft, -10, -25, 160, 120);
    CHECK_EQ(bl2.x, 0);
    CHECK_EQ(bl2.y, 20);
    CHECK_EQ(bl2.w, 310);
    CHECK_EQ(bl2.h, 175);
    CHECK_EQ(bl2.x - start.x, -10);
    CHECK_EQ((bl2.x + bl2.w) - (start.x + start.w), 0);
    CHECK_EQ((bl2.y + bl2.h) - (start.y + start.h), -25);
}

TEST(gui_window_resize_top_bottom) {
    const GuiWindowRect start{10, 20, 300, 200};

    // top: el borde inferior queda fijo, dx se ignora
    const GuiWindowRect t = guiResizeWindowRect(start, GuiWindowEdge::Top, 999, 30, 160, 120);
    CHECK_EQ(t.x, 10);
    CHECK_EQ(t.y, 50);
    CHECK_EQ(t.w, 300);
    CHECK_EQ(t.h, 170);
    CHECK_EQ(t.y - start.y, 30);
    CHECK_EQ((t.x + t.w) - (start.x + start.w), 0);
    CHECK_EQ((t.y + t.h) - (start.y + start.h), 0);

    const GuiWindowRect t2 = guiResizeWindowRect(start, GuiWindowEdge::Top, 999, -10, 160, 120);
    CHECK_EQ(t2.x, 10);
    CHECK_EQ(t2.y, 10);
    CHECK_EQ(t2.w, 300);
    CHECK_EQ(t2.h, 210);
    CHECK_EQ(t2.y - start.y, -10);
    CHECK_EQ((t2.x + t2.w) - (start.x + start.w), 0);
    CHECK_EQ((t2.y + t2.h) - (start.y + start.h), 0);

    // bottom: el borde superior queda fijo, dx se ignora
    const GuiWindowRect b = guiResizeWindowRect(start, GuiWindowEdge::Bottom, 999, 30, 160, 120);
    CHECK_EQ(b.x, 10);
    CHECK_EQ(b.y, 20);
    CHECK_EQ(b.w, 300);
    CHECK_EQ(b.h, 230);
    CHECK_EQ((b.x + b.w) - (start.x + start.w), 0);
    CHECK_EQ((b.y + b.h) - (start.y + start.h), 30);

    const GuiWindowRect b2 = guiResizeWindowRect(start, GuiWindowEdge::Bottom, 999, -30, 160, 120);
    CHECK_EQ(b2.x, 10);
    CHECK_EQ(b2.y, 20);
    CHECK_EQ(b2.w, 300);
    CHECK_EQ(b2.h, 170);
    CHECK_EQ((b2.x + b2.w) - (start.x + start.w), 0);
    CHECK_EQ((b2.y + b2.h) - (start.y + start.h), -30);
}

TEST(gui_window_resize_minimo_resize_permitido) {
    const GuiWindowRect start{10, 20, 300, 200};
    
    // movemos a la derecha todo el left que podemos
    const GuiWindowRect left = guiResizeWindowRect(start, GuiWindowEdge::Left, 1000, 0, 160, 120);
    CHECK_EQ(left.x, 150);
    CHECK_EQ(left.w, 160);
    CHECK_EQ(left.x + left.w, start.x + start.w);

    // movemos el top para abajo todo lo que podemos
    const GuiWindowRect top = guiResizeWindowRect(start, GuiWindowEdge::Top, 0, 1000, 160, 120);
    CHECK_EQ(top.y, 100);
    CHECK_EQ(top.h, 120);
    CHECK_EQ(top.y + top.h, start.y + start.h);

    // movemos el right para la izquierda todo lo que podemos
    const GuiWindowRect right = guiResizeWindowRect(start, GuiWindowEdge::Right, -1000, 0, 160, 120);
    CHECK_EQ(right.x, 10);
    CHECK_EQ(right.w, 160);
    CHECK_EQ(right.x + right.w + (300 - 160), start.x + start.w);

    // movemos el bottom para la derecha todo lo que podemos
    const GuiWindowRect bottom = guiResizeWindowRect(start, GuiWindowEdge::Bottom, 0, -1000, 160, 120);
    CHECK_EQ(bottom.y, 20);
    CHECK_EQ(bottom.h, 120);
    CHECK_EQ(bottom.y + bottom.h + (200 - 120), start.y + start.h);

    // bottom right: achicamos todo lo posible, x e y no cambian
    const GuiWindowRect br = guiResizeWindowRect(start, GuiWindowEdge::BottomRight, -1000, -1000, 160, 120);
    CHECK_EQ(br.x, 10);
    CHECK_EQ(br.y, 20);
    CHECK_EQ(br.w, 160);
    CHECK_EQ(br.h, 120);

    // bottom left: derecha y arriba quedan fijos
    const GuiWindowRect bl = guiResizeWindowRect(start, GuiWindowEdge::BottomLeft, 1000, -1000, 160, 120);
    CHECK_EQ(bl.x, 150);
    CHECK_EQ(bl.y, 20);
    CHECK_EQ(bl.w, 160);
    CHECK_EQ(bl.h, 120);
    CHECK_EQ(bl.x + bl.w, start.x + start.w);

    // top right: izquierda y abajo quedan fijos
    const GuiWindowRect tr = guiResizeWindowRect(start, GuiWindowEdge::TopRight, -1000, 1000, 160, 120);
    CHECK_EQ(tr.x, 10);
    CHECK_EQ(tr.y, 100);
    CHECK_EQ(tr.w, 160);
    CHECK_EQ(tr.h, 120);
    CHECK_EQ(tr.y + tr.h, start.y + start.h);

    // top left: derecha y abajo quedan fijos
    const GuiWindowRect tl = guiResizeWindowRect(start, GuiWindowEdge::TopLeft, 1000, 1000, 160, 120);
    CHECK_EQ(tl.x, 150);
    CHECK_EQ(tl.y, 100);
    CHECK_EQ(tl.w, 160);
    CHECK_EQ(tl.h, 120);
    CHECK_EQ(tl.x + tl.w, start.x + start.w);
    CHECK_EQ(tl.y + tl.h, start.y + start.h);
}

TEST(gui_window_resize_none_es_noop) {
    const GuiWindowRect start{10, 20, 300, 200};
    
    const GuiWindowRect same = guiResizeWindowRect(start, GuiWindowEdge::None, 50, 50, 160, 120);
    CHECK_EQ(same.x, start.x);
    CHECK_EQ(same.y, start.y);
    CHECK_EQ(same.w, start.w);
    CHECK_EQ(same.h, start.h);
}
