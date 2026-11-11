#include "test_framework.h"

#include "platform/gui/GuiSdlMapper.h"
#include "rendering/gui/GuiTitleBar.h"
#include "platform/gui/GuiWindowGeometry.h"

TEST(gui_titlebar_altura_de_una_celda) {
    CHECK_EQ(GuiTitleBar::heightForCell(0), 0);
    CHECK_EQ(GuiTitleBar::heightForCell(-4), 0);
    CHECK_EQ(GuiTitleBar::heightForCell(18), 28);
    CHECK_EQ(GuiTitleBar::heightForCell(36), 46);
}

// Aca testeamos cuando ventana tiene una dimesion normal o grande.
TEST(gui_titlebar_botones_derecha_y_hit_test) {
    // tanto w, como h representan las dimesiones del titlebar
    const int w = 300, h = 28;
    const GuiTitleBarRect min =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Minimize, w, h);
    const GuiTitleBarRect max =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Maximize, w, h);
    const GuiTitleBarRect close =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Close, w, h);

    CHECK_EQ(min.x, w - 3 * h);
    CHECK_EQ(max.x, w - 2 * h);
    CHECK_EQ(close.x, w - h);
    CHECK_EQ(close.x + close.w, w);
    CHECK(!min.contains(max.x, max.y));

    GuiTitleBar bar;
    CHECK(bar.buttonAt(min.x + 1, 1, w, h) == GuiTitleBarButton::Minimize);
    CHECK(bar.buttonAt(max.x + 1, 1, w, h) == GuiTitleBarButton::Maximize);
    CHECK(bar.buttonAt(close.x + 1, 1, w, h) == GuiTitleBarButton::Close);
    CHECK(bar.buttonAt(0, 1, w, h) == GuiTitleBarButton::None);
    CHECK(bar.buttonAt(close.x, h, w, h) == GuiTitleBarButton::None);
    CHECK(bar.buttonAt(-1, 1, w, h) == GuiTitleBarButton::None);
}

// Aca testeamos cuando la ventana es estrecha
// Ventana muy estrecha: width < 3*height -> botones se reducen.
TEST(gui_titlebar_botones_se_ajustan_ventana_estrecha) {
    const int w = 50, h = 28;
    const GuiTitleBarRect min =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Minimize, w, h);
    const GuiTitleBarRect max =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Maximize, w, h);
    const GuiTitleBarRect close =
        GuiTitleBar::buttonRect(GuiTitleBarButton::Close, w, h);
        
    // Ancho de botón = w/3 = 16 (redondeo entero), altura = h.
    const int bw = w / 3;
    CHECK_EQ(min.w, bw);
    CHECK_EQ(max.w, bw);
    CHECK_EQ(close.w, bw);

    // Sin solape y dentro de la ventana (alineados a la derecha).
    CHECK_EQ(min.x, w - 3 * bw);
    CHECK_EQ(max.x, w - 2 * bw);
    CHECK_EQ(close.x, w - bw);
    CHECK_EQ(close.x + close.w, w);
    CHECK(!min.contains(max.x, max.y));
    CHECK(!max.contains(close.x, close.y));

    GuiTitleBar bar;
    CHECK(bar.buttonAt(min.x + 1, 1, w, h) == GuiTitleBarButton::Minimize);
    CHECK(bar.buttonAt(max.x + 1, 1, w, h) == GuiTitleBarButton::Maximize);
    CHECK(bar.buttonAt(close.x + 1, 1, w, h) == GuiTitleBarButton::Close);
}

TEST(gui_titlebar_hover) {
    GuiTitleBar bar;
    
    CHECK(bar.setHover(GuiTitleBarButton::Close));
    CHECK(!bar.setHover(GuiTitleBarButton::Close));

    CHECK(bar.setHover(GuiTitleBarButton::Minimize));
    CHECK(!bar.setHover(GuiTitleBarButton::Minimize));
    
    CHECK(bar.setHover(GuiTitleBarButton::Maximize));
    CHECK(!bar.setHover(GuiTitleBarButton::Maximize));
}
    
TEST(gui_titlebar_maximized) {
    GuiTitleBar bar;
    
    CHECK(!bar.maximized());
    
    bar.setMaximized(true);
    CHECK(bar.maximized());
    
    bar.setMaximized(false);
    CHECK(!bar.maximized());
}

// tanto el caso para gui_titlebar_minimize como el caso
// de gui_titlebar_close, no son necesario porque los administra SDL
// por ese motivo no han sido agregados a los

// Pulsar un botón debe marcar ESE botón como presionado
TEST(gui_titlebar_presionar_marca_boton_correcto) {
    const int w = 300, h = 28;
    GuiTitleBar bar;

    const GuiTitleBarButton buttons[] = {
        GuiTitleBarButton::Minimize,
        GuiTitleBarButton::Maximize,
        GuiTitleBarButton::Close,
    };
    
    for (GuiTitleBarButton button : buttons) {
        const GuiTitleBarRect r = GuiTitleBar::buttonRect(button, w, h);
        const GuiTitleBarButton hit = bar.buttonAt(r.x + 1, 1, w, h);
        CHECK(hit == button);

        bar.setPressed(hit);
        CHECK(bar.pressed() == button);
    }
}

// GEOMETRY



// Conversión de coordenadas HiDPI para la barra de título:
// Escala 2×: winH=600, outH=1200
// titleHPoints=28 -> titleHPx=56.
TEST(gui_titlebar_hidpi_coordenadas) {
    const int winH = 600, outH = 1200;
    const int titlePts = 28;
    const int titlePx = guiScaleCoord(titlePts, winH, outH);
    CHECK_EQ(titlePx, 56);
    
    // Conversión inversa (píxeles -> puntos)
    const int back = guiScaleCoord(titlePx, outH, winH);
    CHECK_EQ(back, titlePts);

    // Grilla de contenido: 
    // outH - titlePx = 1144, cw=18, ch=36 (2×).
    const int cw = 18, ch = 36;
    const int contentH = outH - titlePx;
    const platform::WindowSize g = guiGridSize(outH, contentH, cw, ch);
    CHECK_EQ(g.rows, contentH / ch);
}

// Botones en ventana estrecha: 
// ancho reducido y alineados a la derecha.
TEST(gui_titlebar_botones_ventana_estrecha_hi_dpi) {
    const int w = 50, h = 28;
    const int buttonw = w / 3;
    const GuiTitleBarRect min = GuiTitleBar::buttonRect(GuiTitleBarButton::Minimize, w, h);
    const GuiTitleBarRect max = GuiTitleBar::buttonRect(GuiTitleBarButton::Maximize, w, h);
    const GuiTitleBarRect close = GuiTitleBar::buttonRect(GuiTitleBarButton::Close, w, h);
    
    CHECK_EQ(min.w, buttonw);
    CHECK_EQ(max.w, buttonw);
    CHECK_EQ(close.w, buttonw);
    CHECK_EQ(min.x, w - 3 * buttonw);
    CHECK_EQ(max.x, w - 2 * buttonw);
    CHECK_EQ(close.x, w - buttonw);
    CHECK_EQ(close.x + close.w, w);
}

// Regiones de redimensionado con solapamiento de esquinas 
// y exclusión de la franja superior de la barra.
TEST(gui_titlebar_window_edge_at_solapamiento_esquinas_superiores) {
    const int w = 400, h = 300, border = 7;

    // Esquina superior-izquierda: se solapan Top y Left -> gana la esquina.
    CHECK(guiWindowEdgeAt(3, 3, w, h, border) == GuiWindowEdge::TopLeft);

    // Franja superior lejos de las esquinas -> solo Top.
    CHECK(guiWindowEdgeAt(w / 2, 3, w, h, border) == GuiWindowEdge::Top);

    // Esquina superior-derecha: se solapan Top y Right.
    CHECK(guiWindowEdgeAt(w - 4, 3, w, h, border) == GuiWindowEdge::TopRight);
}

// Caso simple: dentro de la barra no debe solaparse con borde (old bug).
TEST(gui_titlebar_window_barra_sin_solapamiento) {
    const int w = 400, h = 300;
    const int titleHPoints = 28;
    const int border = 7;
    
    auto edgeInBar = [&](int x, int y) {
        GuiWindowEdge e = guiWindowEdgeAt(x, y, w, h, border);
        if (y < titleHPoints &&
            (e == GuiWindowEdge::Top ||
             e == GuiWindowEdge::TopLeft ||
             e == GuiWindowEdge::TopRight)) {
            return GuiWindowEdge::None;
        }
        return e;
    };
    
    // Tres puntos dentro de la barra (y < titleHPoints): al 50%, 25% y 75% del
    // ancho, a mitad de la barra. Deben caer en zona sin solapamiento -> None.
    
    // Punto al 50% del ancho y a mitad de la barra -> None.
    CHECK(edgeInBar(w / 2, titleHPoints / 2) == GuiWindowEdge::None);
    
    // Punto al 25% del ancho y a mitad de la barra -> None.
    CHECK(edgeInBar(w / 4, titleHPoints / 2) == GuiWindowEdge::None);
    
    // Punto al 75% del ancho y a mitad de la barra -> None.
    CHECK(edgeInBar(3 * w / 4, titleHPoints / 2) == GuiWindowEdge::None);
}

// rama de exclusion del fallback manual (Top dentro de la barra).
TEST(gui_titlebar_manual_resize_excluye_top_en_barra) {
    const int w = 400, h = 300;
    const int titleHPoints = 28;
    const int border = 7;
    
    auto edgeInBar = [&](int x, int y) -> GuiWindowEdge {
        GuiWindowEdge e = guiWindowEdgeAt(x, y, w, h, border);
        if (y < titleHPoints &&
            (e == GuiWindowEdge::Top ||
             e == GuiWindowEdge::TopLeft ||
             e == GuiWindowEdge::TopRight)) {
            return GuiWindowEdge::None;
        }
        return e;
    };
    
    // Punto en zona Top dentro de la barra -> la exclusion debe devolver None.
    CHECK(edgeInBar(w / 2, 3) == GuiWindowEdge::None);
    
    // Punto en esquina superior-izquierda dentro de la barra -> None.
    CHECK(edgeInBar(border - 1, border - 1) == GuiWindowEdge::None);
    
    // Punto en esquina superior-derecha dentro de la barra -> None.
    CHECK(edgeInBar(3 * w / 4, 3) == GuiWindowEdge::None);
    
    // Punto fuera de la barra (y=50) -> no se excluye -> Left.
    CHECK(edgeInBar(3, 50) == GuiWindowEdge::Left);
}

// Flags de ventana: borderless se añade sin afectar a HIDDEN/RESIZABLE/ALLOW_HIGHDPI.
TEST(gui_titlebar_window_flags_borderless) {
    CHECK_EQ(guiWindowFlags(true, true) & ~Uint32(SDL_WINDOW_BORDERLESS),guiWindowFlags(true));
    CHECK((guiWindowFlags(true, true) & SDL_WINDOW_BORDERLESS) != 0);
    CHECK((guiWindowFlags(false, true) & SDL_WINDOW_BORDERLESS) != 0);
    
    // HIDDEN y RESIZABLE siempre presentes.
    CHECK((guiWindowFlags(true, true) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE)) != 0);
}

// Grilla de contenido después de restar la barra.
TEST(gui_titlebar_grid_con_barra) {
    const int outW = 960, outH = 600;
    const int cellw = 9, cellh = 18;
    const int titlePx = 28;
    const int contentH = outH - titlePx;
    const platform::WindowSize g = guiGridSize(outW, contentH, cellw, cellh);
    
    CHECK_EQ(g.cols, outW / cellw);
    CHECK_EQ(g.rows, contentH / cellh);
}