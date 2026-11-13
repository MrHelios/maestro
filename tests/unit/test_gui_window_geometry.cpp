#include "test_framework.h"

#include "platform/gui/GuiWindowGeometry.h"

/*
*  GUI WINDOW EDGE
*/

TEST(gui_window_edge_fuera_o_degenerado_es_none) {
    const int w = 300, h = 200, border = 6;

    // degenerado: ventana o borde de tamaño 0
    CHECK(guiWindowEdgeAt(0, 0, 0, h, border) == GuiWindowEdge::None);    // w = 0
    CHECK(guiWindowEdgeAt(0, 0, w, 0, border) == GuiWindowEdge::None);    // h = 0
    CHECK(guiWindowEdgeAt(0, 0, w, h, 0) == GuiWindowEdge::None);         // border = 0
}

TEST(gui_window_edge_to_cursor_mapping) {
    // Esquinas
    CHECK(cursorTypeForEdge(GuiWindowEdge::TopLeft) == ResizeCursorType::SizeNWSE);
    CHECK(cursorTypeForEdge(GuiWindowEdge::TopRight) == ResizeCursorType::SizeNESW);
    CHECK(cursorTypeForEdge(GuiWindowEdge::BottomLeft) == ResizeCursorType::SizeNESW);
    CHECK(cursorTypeForEdge(GuiWindowEdge::BottomRight) == ResizeCursorType::SizeNWSE);

    // Lados
    CHECK(cursorTypeForEdge(GuiWindowEdge::Left) == ResizeCursorType::SizeWE);
    CHECK(cursorTypeForEdge(GuiWindowEdge::Right) == ResizeCursorType::SizeWE);
    CHECK(cursorTypeForEdge(GuiWindowEdge::Top) == ResizeCursorType::SizeNS);
    CHECK(cursorTypeForEdge(GuiWindowEdge::Bottom) == ResizeCursorType::SizeNS);

    // Centro / ninguno
    CHECK(cursorTypeForEdge(GuiWindowEdge::None) == ResizeCursorType::Arrow);
}

TEST(gui_window_edge_esquinas) {
    const int w = 400, h = 300;
    const int border = 8;  // kResizeBorderPx

    // Esquinas exactas (0-based, dentro del borde)
    CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
    CHECK(guiWindowEdgeAt(w - 1, 0, w, h, border) == GuiWindowEdge::TopRight);
    CHECK(guiWindowEdgeAt(0, h - 1, w, h, border) == GuiWindowEdge::BottomLeft);
    CHECK(guiWindowEdgeAt(w - 1, h - 1, w, h, border) == GuiWindowEdge::BottomRight);

    // Esquinas: un pixel dentro del borde
    CHECK(guiWindowEdgeAt(border - 1, border - 1, w, h, border) == GuiWindowEdge::TopLeft);
    CHECK(guiWindowEdgeAt(w - border, border - 1, w, h, border) == GuiWindowEdge::TopRight);
    CHECK(guiWindowEdgeAt(border - 1, h - border, w, h, border) == GuiWindowEdge::BottomLeft);
    CHECK(guiWindowEdgeAt(w - border, h - border, w, h, border) == GuiWindowEdge::BottomRight);
}

TEST(gui_window_edge_lados) {
    const int w = 400, h = 300;
    const int border = 8;

    // Lados verticales (excluyendo esquinas)
    CHECK(guiWindowEdgeAt(0, h / 2, w, h, border) == GuiWindowEdge::Left);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border) == GuiWindowEdge::Right);
    CHECK(guiWindowEdgeAt(border - 1, h / 2, w, h, border) == GuiWindowEdge::Left);
    CHECK(guiWindowEdgeAt(w - border, h / 2, w, h, border) == GuiWindowEdge::Right);

    // Lados horizontales (excluyendo esquinas)
    CHECK(guiWindowEdgeAt(w / 2, 0, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, h - 1, w, h, border) == GuiWindowEdge::Bottom);
    CHECK(guiWindowEdgeAt(w / 2, border - 1, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, h - border, w, h, border) == GuiWindowEdge::Bottom);
}

TEST(gui_window_edge_centro_y_fuera) {
    const int w = 400, h = 300;
    const int border = 8;

    // Centro de la ventana -> None
    CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) == GuiWindowEdge::None);

    // Justo fuera del borde interior -> None
    CHECK(guiWindowEdgeAt(border, border, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w - border - 1, border, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(border, h - border - 1, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w - border - 1, h - border - 1, w, h, border) == GuiWindowEdge::None);

    // Fuera de la ventana
    CHECK(guiWindowEdgeAt(-1, 0, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, -1, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w, 0, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, h, w, h, border) == GuiWindowEdge::None);
}

TEST(gui_window_edge_barra_titulo_excluida) {
    const int w = 400, h = 300;
    const int border = 8;
    const int titleH = 28;

    // guiWindowEdgeAt puro: y < border (8) -> Top
    CHECK(guiWindowEdgeAt(w / 2, 0, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, 5, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, 7, w, h, border) == GuiWindowEdge::Top);

    // y >= border -> None (aunque esté en la barra de título)
    CHECK(guiWindowEdgeAt(w / 2, 8, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w / 2, titleH - 1, w, h, border) == GuiWindowEdge::None);

    // Justo debajo de la barra de título -> None
    CHECK(guiWindowEdgeAt(w / 2, titleH, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w / 2, titleH + border - 1, w, h, border) == GuiWindowEdge::None);
}

TEST(gui_window_edge_scrollbar_track_antes_del_borde) {
    const int w = 400, h = 300;
    const int border = 8;
    const int trackWidth = 8;

    int trackX = w - border - trackWidth;   // 400 - 8 - 8 = 384
    int resizeBorderStartX = w - border;    // 400 - 8 = 392

    // Dentro del track -> None (no es borde de ventana)
    CHECK(guiWindowEdgeAt(trackX + 2, h / 2, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(trackX + trackWidth - 1, h / 2, w, h, border) == GuiWindowEdge::None);

    // En el borde de resize -> Right
    CHECK(guiWindowEdgeAt(resizeBorderStartX, h / 2, w, h, border) == GuiWindowEdge::Right);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border) == GuiWindowEdge::Right);
}

TEST(gui_window_edge_ventanas_distintas) {
    const int border = 8;

    // Ventana pequeña
    {
        const int w = 200, h = 150;
        CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
        CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) == GuiWindowEdge::None);
    }

    // Ventana grande
    {
        const int w = 1920, h = 1080;
        CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
        CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) == GuiWindowEdge::None);
    }
}

// Verifica límites y transiciones
// Estos checks cubren las fronteras entre zonas (resize vs arrastre vs contenido).
TEST(gui_window_edge_limites_y_transiciones_criticas) {
    const int w = 400, h = 300;
    const int border = 8;
    const int titleH = 28;

    auto ht = [&](int x, int y) {
        return hoverCursorType(x, y, w, h, border);
    };

    // --- Esquinas de la ventana ---
    CHECK(ht(0, 0) == ResizeCursorType::SizeNWSE);         // TopLeft
    CHECK(ht(w - 1, 0) == ResizeCursorType::SizeNESW);     // TopRight
    CHECK(ht(0, h - 1) == ResizeCursorType::SizeNESW);     // BottomLeft
    CHECK(ht(w - 1, h - 1) == ResizeCursorType::SizeNWSE); // BottomRight

    // --- Borde superior (solo en y < border) ---
    CHECK(ht(w / 2, 0) == ResizeCursorType::SizeNS);
    CHECK(ht(w / 2, border - 1) == ResizeCursorType::SizeNS);
    // y = border ya NO es borde superior -> Arrow (zona arrastrable)
    CHECK(ht(w / 2, border) == ResizeCursorType::Arrow);

    // --- Barras laterales (resize en TODA la altura, incluyendo barra de título) ---
    // Izquierda
    CHECK(ht(0, 0) == ResizeCursorType::SizeNWSE);        // esquina
    CHECK(ht(0, border) == ResizeCursorType::SizeWE);     // EN barra
    CHECK(ht(0, titleH - 1) == ResizeCursorType::SizeWE); // EN barra
    CHECK(ht(0, titleH) == ResizeCursorType::SizeWE);     // fuera de barra
    CHECK(ht(0, h / 2) == ResizeCursorType::SizeWE);      // centro ventana
    CHECK(ht(0, h - 1) == ResizeCursorType::SizeNESW);    // esquina inf

    // Derecha
    CHECK(ht(w - 1, 0) == ResizeCursorType::SizeNESW);        // esquina
    CHECK(ht(w - 1, border) == ResizeCursorType::SizeWE);     // EN barra
    CHECK(ht(w - 1, titleH - 1) == ResizeCursorType::SizeWE); // EN barra
    CHECK(ht(w - 1, titleH) == ResizeCursorType::SizeWE);     // fuera de barra
    CHECK(ht(w - 1, h / 2) == ResizeCursorType::SizeWE);      // centro ventana
    CHECK(ht(w - 1, h - 1) == ResizeCursorType::SizeNWSE);    // esquina inf

    // --- Zona arrastrable (centro barra de título) ---
    CHECK(ht(w / 2, border) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH / 2) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH - 1) == ResizeCursorType::Arrow);

    // --- Justo debajo de la barra de título -> contenido (Arrow) ---
    CHECK(ht(w / 2, titleH) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH + border - 1) == ResizeCursorType::Arrow);

    // --- Centro de la ventana (contenido) ---
    CHECK(ht(w / 2, h / 2) == ResizeCursorType::Arrow);

    // --- Borde inferior (y >= h - border) ---
    CHECK(ht(w / 2, h - 1) == ResizeCursorType::SizeNS);
    CHECK(ht(w / 2, h - border) == ResizeCursorType::SizeNS);
    CHECK(ht(w / 2, h - border + 1) == ResizeCursorType::SizeNS);
    // y = h - border - 1 está en contenido (NO en borde)
    CHECK(ht(w / 2, h - border - 1) == ResizeCursorType::Arrow);

    // --- Transiciones horizontales en la barra de título (y = border) ---
    // Izquierda: x=0 -> SizeWE, x=border-1 -> SizeWE, x=border -> Arrow, x=w-border-1 -> Arrow, x=w-1 -> SizeWE
    CHECK(ht(1, border) == ResizeCursorType::SizeWE);               // en borde izquierdo
    CHECK(ht(border - 1, border) == ResizeCursorType::SizeWE);      // en borde izquierdo
    CHECK(ht(border, border) == ResizeCursorType::Arrow);           // inicio zona arrastrable
    CHECK(ht(w - border - 1, border) == ResizeCursorType::Arrow);   // fin zona arrastrable
    CHECK(ht(w - border, border) == ResizeCursorType::SizeWE);      // en borde derecho
    CHECK(ht(w - 2, border) == ResizeCursorType::SizeWE);           // en borde derecho
    CHECK(ht(border - 1, border - 1) == ResizeCursorType::SizeNWSE);
    CHECK(ht(w - border, border - 1) == ResizeCursorType::SizeNESW);

    // --- Transiciones verticales en el borde izquierdo (x = 0) ---
    // y = border-1 -> SizeNWSE (esquina), y = border -> SizeWE, y = titleH-1 -> SizeWE, y = titleH -> SizeWE
    CHECK(ht(0, border - 1) == ResizeCursorType::SizeNWSE);
    CHECK(ht(0, border) == ResizeCursorType::SizeWE);
    CHECK(ht(0, titleH - 1) == ResizeCursorType::SizeWE);
    CHECK(ht(0, titleH) == ResizeCursorType::SizeWE);
}

/*
*  GUI WINDOW RESIZE
*/

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

    // movemos el borde inferior hacia arriba hasta alcanzar el mínimo
    const GuiWindowRect bottom = guiResizeWindowRect(start, GuiWindowEdge::Bottom, 0, -1000, 160, 120);
    CHECK_EQ(bottom.y, 20);
    CHECK_EQ(bottom.h, 120);

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


