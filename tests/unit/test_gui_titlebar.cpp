#include "test_framework.h"

#include "rendering/gui/GuiTitleBar.h"

TEST(gui_titlebar_alto_por_celda) {
    CHECK_EQ(GuiTitleBar::heightForCell(0), 0);
    CHECK_EQ(GuiTitleBar::heightForCell(-4), 0);
    CHECK_EQ(GuiTitleBar::heightForCell(18), 28);
    CHECK_EQ(GuiTitleBar::heightForCell(36), 46);
    CHECK(GuiTitleBar::heightForCell(9) >= 28);
}

TEST(gui_titlebar_botones_derecha_y_hit_test) {
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
    CHECK(bar.buttonAt(min.x + 1, 1, w, h) ==
          GuiTitleBarButton::Minimize);
    CHECK(bar.buttonAt(max.x + 1, 1, w, h) ==
          GuiTitleBarButton::Maximize);
    CHECK(bar.buttonAt(close.x + 1, 1, w, h) == GuiTitleBarButton::Close);
    CHECK(bar.buttonAt(0, 1, w, h) == GuiTitleBarButton::None);
    CHECK(bar.buttonAt(close.x, h, w, h) == GuiTitleBarButton::None);
    CHECK(bar.buttonAt(-1, 1, w, h) == GuiTitleBarButton::None);
}

TEST(gui_titlebar_botones_se_ajustan_ventana_estrecha) {
    // Ventana muy estrecha: width < 3*height -> botones se reducen.
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
    CHECK(bar.buttonAt(min.x + 1, 1, w, h) ==
          GuiTitleBarButton::Minimize);
    CHECK(bar.buttonAt(max.x + 1, 1, w, h) ==
          GuiTitleBarButton::Maximize);
    CHECK(bar.buttonAt(close.x + 1, 1, w, h) ==
          GuiTitleBarButton::Close);
}

TEST(gui_titlebar_hover_y_maximizado) {
    GuiTitleBar bar;
    CHECK(!bar.setHover(GuiTitleBarButton::None));
    CHECK(bar.setHover(GuiTitleBarButton::Close));
    CHECK(!bar.setHover(GuiTitleBarButton::Close));
    CHECK(bar.setHover(GuiTitleBarButton::None));
    CHECK(!bar.maximized());
    bar.setMaximized(true);
    CHECK(bar.maximized());
    bar.setPressed(GuiTitleBarButton::Maximize);
    CHECK(bar.pressed() == GuiTitleBarButton::Maximize);
}
