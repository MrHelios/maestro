#include "test_framework.h"

#include "platform/gui/GuiWindowGeometry.h"

TEST(guiwindowedge_esquinas_y_lados) {
    const int w = 300, h = 200, border = 6;
    CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
    CHECK(guiWindowEdgeAt(w - 1, 0, w, h, border) ==
          GuiWindowEdge::TopRight);
    CHECK(guiWindowEdgeAt(0, h - 1, w, h, border) ==
          GuiWindowEdge::BottomLeft);
    CHECK(guiWindowEdgeAt(w - 1, h - 1, w, h, border) ==
          GuiWindowEdge::BottomRight);
    CHECK(guiWindowEdgeAt(0, h / 2, w, h, border) == GuiWindowEdge::Left);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border) ==
          GuiWindowEdge::Right);
    CHECK(guiWindowEdgeAt(w / 2, 0, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, h - 1, w, h, border) ==
          GuiWindowEdge::Bottom);
    CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) ==
          GuiWindowEdge::None);
}

TEST(guiwindowedge_fuera_o_degenerado_es_none) {
    CHECK(guiWindowEdgeAt(-1, 0, 300, 200, 6) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, -1, 300, 200, 6) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(300, 100, 300, 200, 6) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(100, 200, 300, 200, 6) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, 0, 0, 200, 6) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, 0, 300, 0, 6) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, 0, 300, 200, 0) == GuiWindowEdge::None);
}

TEST(guiwindowresize_lados_derecho_e_inferior) {
    const GuiWindowRect start{10, 20, 300, 200};
    const GuiWindowRect right =
        guiResizeWindowRect(start, GuiWindowEdge::Right, 25, 999, 160, 120);
    CHECK_EQ(right.x, 10);
    CHECK_EQ(right.y, 20);
    CHECK_EQ(right.w, 325);
    CHECK_EQ(right.h, 200);

    const GuiWindowRect bottom =
        guiResizeWindowRect(start, GuiWindowEdge::Bottom, 999, 30, 160, 120);
    CHECK_EQ(bottom.x, 10);
    CHECK_EQ(bottom.y, 20);
    CHECK_EQ(bottom.w, 300);
    CHECK_EQ(bottom.h, 230);

    const GuiWindowRect corner = guiResizeWindowRect(
        start, GuiWindowEdge::BottomRight, 25, 30, 160, 120);
    CHECK_EQ(corner.w, 325);
    CHECK_EQ(corner.h, 230);
}

TEST(guiwindowresize_izquierda_arriba_conserva_borde_opuesto) {
    const GuiWindowRect start{10, 20, 300, 200};
    const GuiWindowRect left =
        guiResizeWindowRect(start, GuiWindowEdge::Left, 20, 999, 160, 120);
    CHECK_EQ(left.x, 30);
    CHECK_EQ(left.w, 280);
    CHECK_EQ(left.x + left.w, start.x + start.w);
    CHECK_EQ(left.h, 200);

    const GuiWindowRect top =
        guiResizeWindowRect(start, GuiWindowEdge::Top, 999, 30, 160, 120);
    CHECK_EQ(top.y, 50);
    CHECK_EQ(top.h, 170);
    CHECK_EQ(top.y + top.h, start.y + start.h);
    CHECK_EQ(top.w, 300);

    const GuiWindowRect corner = guiResizeWindowRect(
        start, GuiWindowEdge::TopLeft, 20, 30, 160, 120);
    CHECK_EQ(corner.x, 30);
    CHECK_EQ(corner.y, 50);
    CHECK_EQ(corner.w, 280);
    CHECK_EQ(corner.h, 170);
    CHECK_EQ(corner.x + corner.w, start.x + start.w);
    CHECK_EQ(corner.y + corner.h, start.y + start.h);
}

TEST(guiwindowresize_clamp_minimo_fija_borde_opuesto) {
    const GuiWindowRect start{10, 20, 300, 200};
    const GuiWindowRect left = guiResizeWindowRect(
        start, GuiWindowEdge::Left, 1000, 0, 160, 120);
    CHECK_EQ(left.w, 160);
    CHECK_EQ(left.x, 150);
    CHECK_EQ(left.x + left.w, start.x + start.w);

    const GuiWindowRect top = guiResizeWindowRect(
        start, GuiWindowEdge::Top, 0, 1000, 160, 120);
    CHECK_EQ(top.h, 120);
    CHECK_EQ(top.y, 100);
    CHECK_EQ(top.y + top.h, start.y + start.h);

    const GuiWindowRect right = guiResizeWindowRect(
        start, GuiWindowEdge::Right, -1000, 0, 160, 120);
    CHECK_EQ(right.x, start.x);
    CHECK_EQ(right.w, 160);

    const GuiWindowRect bottom = guiResizeWindowRect(
        start, GuiWindowEdge::Bottom, 0, -1000, 160, 120);
    CHECK_EQ(bottom.y, start.y);
    CHECK_EQ(bottom.h, 120);
}

TEST(guiwindowresize_none_es_noop) {
    const GuiWindowRect start{10, 20, 300, 200};
    const GuiWindowRect same =
        guiResizeWindowRect(start, GuiWindowEdge::None, 50, 50, 160, 120);
    CHECK_EQ(same.x, start.x);
    CHECK_EQ(same.y, start.y);
    CHECK_EQ(same.w, start.w);
    CHECK_EQ(same.h, start.h);
}
