#include "test_framework.h"

#include "platform/gui/GuiWindowGeometry.h"
#include "platform/gui/GuiSdlMapper.h"
#include "platform/CellPos.h"

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

// Una ventana maximizada no debe ofrecer cursor de redimensionado en hover:
// los bordes/esquinas dejan de ser zona de resize.
TEST(gui_window_hover_cursor_maximizada_no_redimensiona) {
    const int w = 400, h = 300;
    const int border = 8;

    // Todos los bordes y esquinas: con maximized=true el cursor es Arrow.
    const int pts[][2] = {
        {0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1},  // esquinas
        {0, h / 2}, {w - 1, h / 2},                      // lados
        {w / 2, 0}, {w / 2, h - 1},
        {border - 1, border - 1},                         // dentro del borde
    };
    for (const auto& p : pts) {
        CHECK(hoverCursorType(p[0], p[1], w, h, border, true) ==
              ResizeCursorType::Arrow);
    }

    // Control: la misma posición en modo ventana sí ofrece resize.
    CHECK(hoverCursorType(0, 0, w, h, border, false) ==
          ResizeCursorType::SizeNWSE);
    CHECK(hoverCursorType(w - 1, h / 2, w, h, border, false) ==
          ResizeCursorType::SizeWE);
    CHECK(hoverCursorType(w / 2, 0, w, h, border, false) ==
          ResizeCursorType::SizeNS);
}

// Una ventana maximizada no debe permitir iniciar un redimensionado:
// no se detecta ningún borde/corner en toda la superficie.
TEST(gui_window_edge_maximizada_no_permite_resize) {
    const int w = 1920, h = 1080;
    const int border = 8;

    // Bordes, esquinas y centro: ninguno es zona de resize.
    CHECK(guiWindowEdgeAt(0, 0, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w - 1, 0, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, h - 1, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w - 1, h - 1, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, h / 2, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w / 2, 0, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w / 2, h - 1, w, h, border, true) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border, true) == GuiWindowEdge::None);

    // Control: sin maximizar, esas mismas posiciones sí son resize.
    CHECK(guiWindowEdgeAt(0, 0, w, h, border, false) == GuiWindowEdge::TopLeft);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border, false) == GuiWindowEdge::Right);
    CHECK(guiWindowEdgeAt(w / 2, h - 1, w, h, border, false) == GuiWindowEdge::Bottom);
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

// drawable 960x600, celda real 9x18 -> 106x33.
// Es el camino del loop tras SDL_GetRendererOutputSize.
TEST(gui_window_grid_size_scale_1x) {
    const platform::WindowSize s = guiGridSize(960, 600, 9, 18);
    
    CHECK_EQ(s.cols, 106);
    CHECK_EQ(s.rows, 33);
    CHECK_EQ(s.cellW, 9);
    CHECK_EQ(s.cellH, 18);
    CHECK_EQ(s.pixelW, 960);
    CHECK_EQ(s.pixelH, 600);
}

// HiDPI 2x, división FÍSICA: drawable 1920x1200 / celda sin escalar 9x18
// = 213x66 (frente a 106x33 con drawable 960x600). Es lo que daría NO
// escalar la fuente: grilla duplicada con texto diminuto (bug P1).
// El contrato visual-estable escala la celda antes de dividir (ver
// guigrid_stable_visual_size_hidpi): este test solo congela la división.
TEST(gui_window_grid_size_hidpi_2x_uses_drawable) {    
    const platform::WindowSize s = guiGridSize(1920, 1200, 9, 18);
    CHECK_EQ(s.cols, 213);
    CHECK_EQ(s.rows, 66);
    
    // Cadena completa evento->celda a 2x (paridad con guimouse_scale_coord):
    // punto (19,37) -> drawable (38,74) -> celda (4,4).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    const CellPos c = guiCellFromPixels(px, py, s.cellW, s.cellH);
    CHECK_EQ(c.col, 4);
    CHECK_EQ(c.row, 4);
    
    // Sin escalar (bug HiDPI: puntos / celda) daría (2,2).
    const CellPos wrong = guiCellFromPixels(19, 37, s.cellW, s.cellH);
    CHECK_EQ(wrong.col, 2);
    CHECK_EQ(wrong.row, 2);
}

// Sin fuente (celda <=0): fallback canónico, igual que GuiFont y el loop.
TEST(gui_window_grid_size_fallback_and_clamp) {
    const platform::WindowSize f = guiGridSize(960, 600, 0, 0);
    CHECK_EQ(f.cellW, platform::kFallbackCellW);
    CHECK_EQ(f.cellH, platform::kFallbackCellH);
    CHECK_EQ(f.cols, 106);
    CHECK_EQ(f.rows, 33);
    
    const platform::WindowSize f2 = guiGridSize(960, 600, -3, -5);
    CHECK_EQ(f2.cellW, platform::kFallbackCellW);
    CHECK_EQ(f2.cellH, platform::kFallbackCellH);
    
    // Ventana minimizada / drawable diminuto: nunca 0x0 al Editor.
    const platform::WindowSize m = guiGridSize(1, 1, 9, 18);
    CHECK_EQ(m.cols, 1);
    CHECK_EQ(m.rows, 1);
    
    const platform::WindowSize z = guiGridSize(0, 0, 9, 18);
    CHECK_EQ(z.cols, 1);
    CHECK_EQ(z.rows, 1);
    
    // Fraccionaria 1.5x: 300px / 9 = 33 cols (truncado, sin redondeo).
    const platform::WindowSize fr = guiGridSize(300, 300, 9, 18);
    CHECK_EQ(fr.cols, 33);
    CHECK_EQ(fr.rows, 16);
}

// sin precondiciones; píxeles negativos -> 0 
// (desconocido, igual que Editor::resize)
// nunca negativos guardados. La grilla se clampea a 1x1.
TEST(gui_window_grid_size_negative_pixels_normalized) {
    const platform::WindowSize s = guiGridSize(-100, -50, 9, 18);
    CHECK_EQ(s.pixelW, 0);
    CHECK_EQ(s.pixelH, 0);
    CHECK_EQ(s.cols, 1);
    CHECK_EQ(s.rows, 1);
    
    // Mixto: un eje negativo no contamina al otro.
    const platform::WindowSize m = guiGridSize(-100, 600, 9, 18);
    CHECK_EQ(m.pixelW, 0);
    CHECK_EQ(m.pixelH, 600);
    CHECK_EQ(m.cols, 1);
    CHECK_EQ(m.rows, 33);
    
    // Negativos en todo: celdas al fallback, píxeles a 0, grilla 1x1.
    const platform::WindowSize a = guiGridSize(-100, -50, -9, -18);
    CHECK_EQ(a.pixelW, 0);
    CHECK_EQ(a.pixelH, 0);
    CHECK_EQ(a.cellW, platform::kFallbackCellW);
    CHECK_EQ(a.cellH, platform::kFallbackCellH);
    CHECK_EQ(a.cols, 1);
    CHECK_EQ(a.rows, 1);
}

// escalado con redondeo entre espacios.
TEST(gui_window_scale_size_font_and_fallback) {
    // Fuente: 16 a 1x queda 16; a 2x (600pts->1200px) pasa a 32.
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 1200), 32);
    CHECK_EQ(guiScaledFontPixels(16, 960, 960), 16);
    
    // Fraccionaria 1.5x: 16 -> 24 (redondeo, no truncado).
    CHECK_EQ(guiScaledFontPixels(16, 200, 300), 24);
    
    // Escala desconocida (<=0): identidad = comportamiento pre-HiDPI.
    CHECK_EQ(guiScaledFontPixels(16, 0, 1200), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 0), 16);
    CHECK_EQ(guiScaledFontPixels(16, -5, 1200), 16);
    
    // Fallback por eje: 9x18 a 2x -> 18x36 (misma proporción que la fuente).
    CHECK_EQ(guiScaleSize(9, 960, 1920), 18);
    CHECK_EQ(guiScaleSize(18, 600, 1200), 36);
    CHECK_EQ(guiScaleSize(9, 960, 960), 9);
    CHECK_EQ(guiScaleSize(18, 600, 600), 18);
    
    // Base inválida o escala desconocida: identidad.
    CHECK_EQ(guiScaleSize(0, 960, 1920), 0);
    CHECK_EQ(guiScaleSize(9, 0, 1920), 9);
    CHECK_EQ(guiScaleSize(9, 960, 0), 9);
}

// Contrato visual-estable: a 2x la celda SE ESCALA antes de dividir
// y la grilla se mantiene (~106x33) en vez de duplicarse (213x66).
// Ventana 960x600 pts, drawable 1920x1200 px, base 16px/9x18:
TEST(gui_window_grid_stable_visual_size_hidpi) {    
    const int fontPx = guiScaledFontPixels(16, 600, 1200);
    CHECK_EQ(fontPx, 32);
    
    const int cw = guiScaleSize(9, 960, 1920);
    const int ch = guiScaleSize(18, 600, 1200);
    CHECK_EQ(cw, 18);
    CHECK_EQ(ch, 36);
    
    const platform::WindowSize s2 = guiGridSize(1920, 1200, cw, ch);
    const platform::WindowSize s1 = guiGridSize(960, 600, 9, 18);
    CHECK_EQ(s2.cols, s1.cols);
    CHECK_EQ(s2.rows, s1.rows);
    CHECK_EQ(s2.cols, 106);
    CHECK_EQ(s2.rows, 33);
    
    // El mouse sigue la misma celda escalada: punto (19,37) -> drawable
    // (38,74) -> celda (2,2) con celda 18x36 (era (4,4) con 9x18 sin
    // escalar: el índice acompaña al tamaño visual, no al físico).
    const int px = guiScaleCoord(19, 960, 1920);
    const int py = guiScaleCoord(37, 600, 1200);
    const CellPos c = guiCellFromPixels(px, py, cw, ch);
    CHECK_EQ(c.col, 2);
    CHECK_EQ(c.row, 2);
}

// CONTRATO DE UNIDADES CLI (puro y sin SDL): `--font-size N` son píxeles
// lógicos (== píxeles del drawable a escala 1; no puntos tipográficos de
// 1/72in) y la talla que llega a TTF_OpenFont es guiScaledFontPixels(N,
// winH, outH) (eje vertical, redondeo). Fija que el mismo N da la misma
// altura visual en cualquier monitor: el loop escala tanto al abrir (init)
// como en cada resize.
TEST(gui_window_contrato_fontsize_pixeles_logicos) {
    // A escala 1 la base llega intacta a TTF_OpenFont.
    CHECK_EQ(guiScaledFontPixels(16, 600, 600), 16);
    
    // A escala 2 la talla duplica (misma altura visual, misma grilla).
    CHECK_EQ(guiScaledFontPixels(16, 600, 1200), 32);
    
    // Base custom del CLI (--font-size 18) escala igual.
    CHECK_EQ(guiScaledFontPixels(18, 600, 1200), 36);
    
    // Escala fraccionaria 1.5× con redondeo, no truncado (16 -> 24).
    CHECK_EQ(guiScaledFontPixels(16, 600, 900), 24);
    
    // Ventana/salida degenerada (minimizada, 0): identidad segura.
    CHECK_EQ(guiScaledFontPixels(16, 0, 1200), 16);
    CHECK_EQ(guiScaledFontPixels(16, 600, 0), 16);
}

// CONTRATO DE UNIDADES CLI (puro y sin SDL): `--geometry WxH` son
// coordenadas de ventana (puntos, no píxeles físicos). La misma ventana
// lógica en dos escalas da la misma grilla visual: si la geometría fueran
// píxeles físicos, a 2× la ventana lógica sería la mitad y la grilla
// cambiaría (53×16 en vez de 106×33).
TEST(gui_window_contrato_geometry_coords_ventana) {
    // Ventana lógica 1200×800 (p. ej. --geometry 1200x800) a escala 1.
    const int winW = 1200, winH = 800;
    const int cw1 = guiScaleSize(9, winW, winW);
    const int ch1 = guiScaleSize(18, winH, winH);
    const platform::WindowSize g1 = guiGridSize(winW, winH, cw1, ch1);
    CHECK_EQ(g1.cols, 133);
    CHECK_EQ(g1.rows, 44);
    
    // Misma ventana lógica a escala 2 (drawable 2400×1600): misma grilla.
    const int outW = 2400, outH = 1600;
    const int cw2 = guiScaleSize(9, winW, outW);
    const int ch2 = guiScaleSize(18, winH, outH);
    const platform::WindowSize g2 = guiGridSize(outW, outH, cw2, ch2);
    CHECK_EQ(g2.cols, g1.cols);
    CHECK_EQ(g2.rows, g1.rows);
    
    // Y la talla de fuente de esa geometría escala en la misma razón.
    CHECK_EQ(guiScaledFontPixels(16, winH, outH), 32);
}

// el drawable efectivo ante medidas degeneradas 
// (minimizado o driver que reporta un eje en 0). 
// El fallback es POR EJE: un eje válido nunca se descarta porque el otro falle
TEST(gui_window_drawable_fallback_por_eje) {
    // Medidas sanas: passthrough intacto.
    GuiDrawableSize ok = guiDrawableOrWindow(960, 600, 1920, 1200);
    CHECK_EQ(ok.w, 1920);
    CHECK_EQ(ok.h, 1200);
    
    // Un eje degenerado: el otro se conserva (no cae a la ventana).
    GuiDrawableSize mixW = guiDrawableOrWindow(960, 600, 0, 1200);
    CHECK_EQ(mixW.w, 960);
    CHECK_EQ(mixW.h, 1200);
    
    GuiDrawableSize mixH = guiDrawableOrWindow(960, 600, 1920, 0);
    CHECK_EQ(mixH.w, 1920);
    CHECK_EQ(mixH.h, 600);
    
    // Todo degenerado (minimizada sin medidas): 960×600, escala 1:1.
    GuiDrawableSize all = guiDrawableOrWindow(0, 0, 0, 0);
    CHECK_EQ(all.w, 960);
    CHECK_EQ(all.h, 600);
    
    // Negativos: igual que 0 (desconocido, nunca se guarda tal cual).
    GuiDrawableSize neg = guiDrawableOrWindow(-10, 600, -5, 1200);
    CHECK_EQ(neg.w, 960);
    CHECK_EQ(neg.h, 1200);
    
    // La cadena del loop sobre el fallback nunca da 0×0 al Editor: grilla
    // mínima 1×1 y fuente en identidad.
    const platform::WindowSize g = guiGridSize(all.w, all.h, 9, 18);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
    CHECK_EQ(guiScaledFontPixels(16, 0, all.h), 16);
}

// cambio de monitor ida y vuelta (1× -> 2× -> 1×) con la MISMA ventana lógica. 
// Replica la cadena del loop (fallback drawable + fuente + grilla + mouse) en cada etapa: 
// la talla sigue a la escala
// (16->32->16), la grilla no se mueve y el mismo clic lógico cae en la
// misma celda en las tres etapas.
TEST(gui_window_hidpi_transicion_monitor_ida_y_vuelta) {
    const int winW = 960, winH = 600;
    // Etapa 1×: ventana == drawable.
    GuiDrawableSize d1 = guiDrawableOrWindow(winW, winH, 960, 600);
    
    const int f1 = guiScaledFontPixels(16, winH, d1.h);
    CHECK_EQ(f1, 16);
    
    const platform::WindowSize g1 = guiGridSize(d1.w, d1.h, 9, 18);
    CHECK_EQ(g1.cols, 106);
    CHECK_EQ(g1.rows, 33);
    
    // Etapa 2× (DISPLAY_CHANGED sin resize en puntos): drawable duplica.
    GuiDrawableSize d2 = guiDrawableOrWindow(winW, winH, 1920, 1200);
    
    const int f2 = guiScaledFontPixels(16, winH, d2.h);
    CHECK_EQ(f2, 32);
    
    const int cw2 = guiScaleSize(9, winW, d2.w);
    const int ch2 = guiScaleSize(18, winH, d2.h);
    const platform::WindowSize g2 = guiGridSize(d2.w, d2.h, cw2, ch2);
    CHECK_EQ(g2.cols, g1.cols);
    CHECK_EQ(g2.rows, g1.rows);
    
    // Vuelta a 1×: la talla baja sola (ensureFontPixels recarga por cambio)
    // y la grilla sigue estable.
    GuiDrawableSize d3 = guiDrawableOrWindow(winW, winH, 960, 600);
    const int f3 = guiScaledFontPixels(16, winH, d3.h);
    CHECK_EQ(f3, 16);
    
    const platform::WindowSize g3 = guiGridSize(d3.w, d3.h, 9, 18);
    CHECK_EQ(g3.cols, g1.cols);
    CHECK_EQ(g3.rows, g1.rows);
    
    // Mismo clic lógico (19,37) pts en las tres etapas -> misma celda.
    const CellPos c1 = guiCellFromPixels(guiScaleCoord(19, winW, d1.w),
                                         guiScaleCoord(37, winH, d1.h), 9, 18);
    const CellPos c2 = guiCellFromPixels(guiScaleCoord(19, winW, d2.w),
                                         guiScaleCoord(37, winH, d2.h), cw2,
                                         ch2);
    const CellPos c3 = guiCellFromPixels(guiScaleCoord(19, winW, d3.w),
                                         guiScaleCoord(37, winH, d3.h), 9, 18);
    CHECK(c2 == c1);
    CHECK(c3 == c1);
}

// escala NO uniforme (X a 2×, Y a 1.5×; fuera de lo que SDL
// produce, donde el factor es único). Cada eje usa su propia razón en
// coords, fallback y grilla; la fuente sigue el eje VERTICAL (la talla es
// altura) y el raster escala uniforme con ella.
TEST(gui_window_hidpi_escala_no_uniforme_por_eje) {
    const int winW = 960, winH = 600;
    const int outW = 1920, outH = 900;
    CHECK_EQ(guiScaleCoord(100, winW, outW), 200);
    CHECK_EQ(guiScaleCoord(100, winH, outH), 150);
    CHECK_EQ(guiScaledFontPixels(16, winH, outH), 24);
    CHECK_EQ(guiScaleSize(9, winW, outW), 18);
    CHECK_EQ(guiScaleSize(18, winH, outH), 27);
    
    const platform::WindowSize g = guiGridSize(outW, outH, 18, 27);
    CHECK_EQ(g.cols, 106);
    CHECK_EQ(g.rows, 33);
}

// --- Headless extra: grilla grande (4K) + drawable diminuto ---
// División PURA (mismo espacio en ambos lados). El camino HiDPI real con
// celda escalada vive en gui_hidpi_integracion_mismo_espacio_headless.
TEST(gui_window_grid_grande_y_minimo_headless) {
    const platform::WindowSize k4 = guiGridSize(3840, 2160, 9, 18);
    CHECK_EQ(k4.cols, 426);
    CHECK_EQ(k4.rows, 120);
    CHECK_EQ(k4.cellW, 9);
    CHECK_EQ(k4.cellH, 18);
    
    // 1px de drawable: clamp a 1x1, nunca 0x0 al Editor::resize.
    const platform::WindowSize tiny = guiGridSize(1, 1, 18, 36);
    CHECK_EQ(tiny.cols, 1);
    CHECK_EQ(tiny.rows, 1);
    
    // Celda gigante (ventana menor que una celda): también 1x1.
    const platform::WindowSize huge = guiGridSize(100, 100, 1000, 1000);
    CHECK_EQ(huge.cols, 1);
    CHECK_EQ(huge.rows, 1);
}
// integración HiDPI en el MISMO espacio, sin SDL ni ventana.
// Replica EXACTAMENTE la cadena del loop (GuiRunLoop::currentSize +
// mouseCell) solo con helpers puros: la grilla divide píxeles REALES del
// drawable por celda FÍSICA escalada, y el evento en PUNTOS de ventana sube
// a drawable antes de dividirse por esa misma celda. Si un lado quedara sin
// escalar, los tests puros de división seguirían pasando pero el clic real
// caería en otra celda (el bug que este test caza: ambas variantes rotas dan
// una celda distinta de la estable).
TEST(gui_window_hidpi_integracion_mismo_espacio_headless) {
    // Marco 1×: ventana 960×600 pts == drawable 960×600 px, celda 9×18.
    const int winW = 960, winH = 600;
    const int out1W = 960, out1H = 600;
    const int cw1 = guiScaleSize(9, winW, out1W);
    const int ch1 = guiScaleSize(18, winH, out1H);
    CHECK_EQ(cw1, 9);
    CHECK_EQ(ch1, 18);
    
    const platform::WindowSize g1 = guiGridSize(out1W, out1H, cw1, ch1);
    CHECK_EQ(g1.cols, 106);
    CHECK_EQ(g1.rows, 33);
    
    // Marco 2×: misma ventana lógica, drawable 1920×1200, celda 18×36.
    const int out2W = 1920, out2H = 1200;
    const int cw2 = guiScaleSize(9, winW, out2W);
    const int ch2 = guiScaleSize(18, winH, out2H);
    CHECK_EQ(cw2, 18);
    CHECK_EQ(ch2, 36);
    
    const platform::WindowSize g2 = guiGridSize(out2W, out2H, cw2, ch2);
    // La grilla usa el DRAWABLE: con puntos de ventana daría 53×16.
    const platform::WindowSize wrongGrid = guiGridSize(winW, winH, cw2, ch2);
    CHECK_EQ(wrongGrid.cols, 53);
    CHECK_EQ(wrongGrid.rows, 16);
    CHECK(g2.cols == g1.cols);
    CHECK(g2.rows == g1.rows);
    
    // Mismo clic lógico (19,37) pts en ambos marcos -> misma celda visual.
    const CellPos c1 = guiCellFromPixels(
        guiScaleCoord(19, winW, out1W), guiScaleCoord(37, winH, out1H), cw1,ch1);
    const CellPos c2 = guiCellFromPixels(
        guiScaleCoord(19, winW, out2W), guiScaleCoord(37, winH, out2H), cw2,ch2);
    CHECK_EQ(c1.col, 2);
    CHECK_EQ(c1.row, 2);
    CHECK(c2 == c1);
    
    // Sin escalar el evento (puntos / celda física): celda chica (1,1).
    const CellPos noEventScale = guiCellFromPixels(19, 37, cw2, ch2);
    CHECK_EQ(noEventScale.col, 1);
    CHECK_EQ(noEventScale.row, 1);
    CHECK(!(noEventScale == c1));
    
    // Sin escalar la celda (drawable / celda base): celda grande (4,4).
    const int px2 = guiScaleCoord(19, winW, out2W);
    const int py2 = guiScaleCoord(37, winH, out2H);
    const CellPos noCellScale = guiCellFromPixels(px2, py2, 9, 18);
    CHECK_EQ(noCellScale.col, 4);
    CHECK_EQ(noCellScale.row, 4);
    CHECK(!(noCellScale == c1));
    
    // 4K real: ventana 1920×1080 pts, drawable 3840×2160, celda 18×36 ->
    // 213×60, igual que 1× (1920×1080 / 9×18). Sin escalar daría 426×120.
    const int w4 = 1920, h4 = 1080, o4W = 3840, o4H = 2160;
    const int cw4 = guiScaleSize(9, w4, o4W);
    const int ch4 = guiScaleSize(18, h4, o4H);
    CHECK_EQ(cw4, 18);
    CHECK_EQ(ch4, 36);
    
    const platform::WindowSize g4 = guiGridSize(o4W, o4H, cw4, ch4);
    const platform::WindowSize g4base = guiGridSize(w4, h4, 9, 18);
    CHECK_EQ(g4.cols, g4base.cols);
    CHECK_EQ(g4.rows, g4base.rows);
    CHECK_EQ(g4.cols, 213);
    CHECK_EQ(g4.rows, 60);
}