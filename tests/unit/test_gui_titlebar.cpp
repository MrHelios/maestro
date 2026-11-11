#include "test_framework.h"

#include "rendering/gui/GuiTitleBar.h"

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


