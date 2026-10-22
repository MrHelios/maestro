#include "test_framework.h"

#include "platform/gui/GuiSdlMapper.h"
#include "rendering/gui/GuiTitleBar.h"
#include "platform/gui/GuiWindowGeometry.h"

// Conversión de coordenadas HiDPI para la barra de título:
// - La altura de la barra se mide en puntos lógicos (p.ej. 28) y se
//   convierte a píxeles drawable con guiScaleCoord(puntos, winH, outH).
// - El contenido pierde exactamente esos píxeles; la grilla se recalcula
//   con guiGridSize(outW, outH - titlePx, cw, ch).
TEST(gui_titlebar_hidpi_coordenadas) {
    // Escala 2×: winH=600, outH=1200, titleHPoints=28 -> titleHPx=56.
    const int winH = 600, outH = 1200;
    const int titlePts = 28;
    const int titlePx = guiScaleCoord(titlePts, winH, outH);
    CHECK_EQ(titlePx, 56);
    // Conversión inversa (píxeles -> puntos) para IME.
    const int back = guiScaleCoord(titlePx, outH, winH);
    CHECK_EQ(back, titlePts);

    // Grilla de contenido: outH - titlePx = 1144, cw=18, ch=36 (2×).
    const int cw = 18, ch = 36;
    const int contentH = outH - titlePx;
    const platform::WindowSize g = guiGridSize(outH, contentH, cw, ch);
    CHECK_EQ(g.rows, contentH / ch);
}

// Botones en ventana estrecha: ancho reducido y alineados a la derecha.
TEST(gui_titlebar_botones_ventana_estrecha_hi_dpi) {
    const int w = 50, h = 28;
    const int bw = w / 3; // 16
    const GuiTitleBarRect min =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Minimize, w, h);
    const GuiTitleBarRect max =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Maximize, w, h);
    const GuiTitleBarRect close =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Close, w, h);
    CHECK_EQ(min.w, bw);
    CHECK_EQ(max.w, bw);
    CHECK_EQ(close.w, bw);
    CHECK_EQ(min.x, w - 3 * bw);
    CHECK_EQ(max.x, w - 2 * bw);
    CHECK_EQ(close.x, w - bw);
    CHECK_EQ(close.x + close.w, w);
}

// Regiones de redimensionado con solapamiento de esquinas y exclusión de
// la franja superior de la barra.
TEST(gui_window_edge_at_y_exclusion_barra) {
    const int w = 400, h = 300, border = 7;
    // Punto en la esquina superior-izquierda (x=3,y=3) -> TopLeft.
    CHECK(guiWindowEdgeAt(3, 3, w, h, border) ==
          GuiWindowEdge::TopLeft);
    // Punto en la barra, pero en la franja de borde superior (y=3) y
    // dentro de la barra (y < titleH). La exclusión no se aplica aquí
    // porque guiWindowEdgeAt no conoce la barra; la exclusión se hace en
    // manualResizeEdgeAt. Ver test siguiente.
    CHECK(guiWindowEdgeAt(3, 3, w, h, border) ==
          GuiWindowEdge::TopLeft);
}

// Exclusión de la franja superior dentro de la barra (lógica de fallback
// manual). Replicamos la condición de manualResizeEdgeAt.
TEST(gui_manual_resize_excluye_top_en_barra) {
    const int titleHPoints = 28;
    const int border = 7;
    auto edgeInBar = [&](int x, int y) {
        GuiWindowEdge e = guiWindowEdgeAt(x, y, 400, 300, border);
        if (y < titleHPoints &&
            (e == GuiWindowEdge::Top ||
             e == GuiWindowEdge::TopLeft ||
             e == GuiWindowEdge::TopRight)) {
            return GuiWindowEdge::None;
        }
        return e;
    };
    // Punto en esquina superior (x=3,y=3) dentro de la barra -> None.
    CHECK(edgeInBar(3, 3) == GuiWindowEdge::None);
    // Punto en borde izquierdo pero fuera de la barra (y=50) -> Left.
    CHECK(edgeInBar(3, 50) == GuiWindowEdge::Left);
    // Punto en esquina superior-derecha dentro de la barra -> None.
    CHECK(edgeInBar(397, 3) == GuiWindowEdge::None);
    // Punto en borde derecho fuera de la barra -> Right.
    CHECK(edgeInBar(397, 50) == GuiWindowEdge::Right);
}

// Flags de ventana: borderless se añade sin afectar a HIDDEN/RESIZABLE/ALLOW_HIGHDPI.
TEST(gui_window_flags_borderless) {
    CHECK_EQ(guiWindowFlags(true, true) & ~Uint32(SDL_WINDOW_BORDERLESS),
             guiWindowFlags(true));
    CHECK((guiWindowFlags(true, true) & SDL_WINDOW_BORDERLESS) != 0);
    CHECK((guiWindowFlags(false, true) & SDL_WINDOW_BORDERLESS) != 0);
    // HIDDEN y RESIZABLE siempre presentes.
    CHECK((guiWindowFlags(true, true) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE)) != 0);
}

// Altura de la barra para distintas celdas (incluye HiDPI).
TEST(gui_titlebar_altura_por_celda_varios) {
    CHECK_EQ(GuiTitleBar::heightForCell(0), 0);
    CHECK_EQ(GuiTitleBar::heightForCell(-5), 0);
    CHECK_EQ(GuiTitleBar::heightForCell(18), 28);   // 1×
    CHECK_EQ(GuiTitleBar::heightForCell(36), 46);   // 2×
    CHECK_EQ(GuiTitleBar::heightForCell(20), 30);
    CHECK(GuiTitleBar::heightForCell(9) >= 28);
}

// Grilla de contenido después de restar la barra.
TEST(gui_grid_con_barra) {
    const int outW = 960, outH = 600;
    const int cw = 9, ch = 18;
    const int titlePx = 28;
    const int contentH = outH - titlePx;
    const platform::WindowSize g = guiGridSize(outW, contentH, cw, ch);
    CHECK_EQ(g.cols, outW / cw);
    CHECK_EQ(g.rows, contentH / ch);
}