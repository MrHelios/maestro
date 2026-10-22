#include "rendering/gui/GuiTitleBar.h"

#include <algorithm>
#include <string>

#include "rendering/gui/GuiFont.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

namespace {

int buttonIndex(GuiTitleBarButton button) {
    switch (button) {
        case GuiTitleBarButton::Minimize:
            return 0;
        case GuiTitleBarButton::Maximize:
            return 1;
        case GuiTitleBarButton::Close:
            return 2;
        case GuiTitleBarButton::None:
            break;
    }
    return -1;
}

#ifdef HAVE_SDL2
void drawThickLine(SDL_Renderer* r, int x0, int y0, int x1, int y1,
                   int thickness) {
    if (thickness <= 0) return;
    for (int i = 0; i < thickness; ++i) {
        SDL_RenderDrawLine(r, x0 + i, y0, x1 + i, y1);
        SDL_RenderDrawLine(r, x0, y0 + i, x1, y1 + i);
    }
}

void fillRect(SDL_Renderer* r, const GuiColor& color, int x, int y, int w,
              int h) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void drawButtonGlyph(SDL_Renderer* r, GuiTitleBarButton button,
                     const GuiTitleBarRect& rect, const GuiColor& fg,
                     bool maximized) {
    const int thick = std::max(1, rect.h / 14);
    const int side = std::max(6, rect.h * 2 / 5);
    const int cx = rect.x + rect.w / 2;
    const int cy = rect.y + rect.h / 2;
    const int half = side / 2;
    SDL_SetRenderDrawColor(r, fg.r, fg.g, fg.b, fg.a);
    switch (button) {
        case GuiTitleBarButton::Minimize:
            fillRect(r, fg, cx - half, cy - thick / 2, side, thick);
            break;
        case GuiTitleBarButton::Maximize:
            if (!maximized) {
                for (int i = 0; i < thick; ++i) {
                    SDL_Rect rr{cx - half + i, cy - half + i, side - 2 * i,
                                side - 2 * i};
                    SDL_RenderDrawRect(r, &rr);
                }
            } else {
                const int back = std::max(2, thick * 2);
                for (int i = 0; i < thick; ++i) {
                    SDL_Rect backRect{cx - half + i, cy - half + i,
                                      side - back - 2 * i,
                                      side - back - 2 * i};
                    SDL_Rect frontRect{cx - half + back + i,
                                       cy - half + back + i,
                                       side - back - 2 * i,
                                       side - back - 2 * i};
                    SDL_RenderDrawRect(r, &backRect);
                    SDL_RenderDrawRect(r, &frontRect);
                }
            }
            break;
        case GuiTitleBarButton::Close:
            drawThickLine(r, cx - half, cy - half, cx + half, cy + half,
                          thick);
            drawThickLine(r, cx + half, cy - half, cx - half, cy + half,
                          thick);
            break;
        case GuiTitleBarButton::None:
            break;
    }
}
#endif

}  // namespace

int GuiTitleBar::heightForCell(int cellH) {
    if (cellH <= 0) return 0;
    return std::max(28, cellH + 10);
}

GuiTitleBarRect GuiTitleBar::buttonRect(GuiTitleBarButton button, int width,
                                        int height) {
    const int idx = buttonIndex(button);
    if (idx < 0 || width <= 0 || height <= 0) return GuiTitleBarRect{};
    // Cada botón idealmente cuadrado (alto = ancho = height), pero si la
    // ventana es muy estrecha se reduce el ancho para que los tres quepan
    // sin solaparse. Mínimo 1 píxel.
    const int buttonW = std::max(1, std::min(height, width / 3));
    return GuiTitleBarRect{width - (3 - idx) * buttonW, 0, buttonW, height};
}

GuiTitleBarButton GuiTitleBar::buttonAt(int x, int y, int width,
                                        int height) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return GuiTitleBarButton::None;
    }
    const GuiTitleBarButton buttons[] = {
        GuiTitleBarButton::Minimize,
        GuiTitleBarButton::Maximize,
        GuiTitleBarButton::Close,
    };
    for (GuiTitleBarButton button : buttons) {
        if (buttonRect(button, width, height).contains(x, y)) return button;
    }
    return GuiTitleBarButton::None;
}

bool GuiTitleBar::setHover(GuiTitleBarButton button) {
    if (hover_ == button) return false;
    hover_ = button;
    return true;
}

void GuiTitleBar::paint(void* sdlRenderer, GuiFont& font, int width,
                        int height, bool dark) const {
#ifdef HAVE_SDL2
    SDL_Renderer* r = static_cast<SDL_Renderer*>(sdlRenderer);
    if (!r || width <= 0 || height <= 0) return;

    const ThemePalette p = guichrome::palette(dark);
    const GuiColor bg = guichrome::statusBackground(p);
    // En oscuro el statusName del tema queda demasiado apagado sobre el
    // fondo de la barra: el título y los glifos usan blanco puro. En claro
    // se conserva el color del tema (contraste ya correcto).
    const GuiColor fg = dark ? GuiColor{0xFF, 0xFF, 0xFF, 0xFF}
                             : guichrome::toGuiColor(p.statusName);
    const GuiColor hoverBg = guichrome::currentLineBackground(p);
    const GuiColor pressedBg = guichrome::selectionBackground(p);
    const GuiColor closeHover = guichrome::toGuiColor(p.statusModified);
    const GuiColor closeFg = guichrome::background(p);
    const GuiColor separator = guichrome::toGuiColor(p.gutter);

    fillRect(r, bg, 0, 0, width, height);
    if (height > 1) fillRect(r, separator, 0, height - 1, width, 1);

    const int pad = std::max(4, height / 4);
    int iconSize = std::max(8, height - 2 * pad);
    iconSize = std::min(iconSize, height - 2);
    const int iconX = pad;
    const int iconY = (height - iconSize) / 2;
    const GuiColor iconBg = guichrome::toGuiColor(p.statusAccentDefault);
    const GuiColor iconFg = guichrome::background(p);
    fillRect(r, iconBg, iconX, iconY, iconSize, iconSize);
    {
        const int t = std::max(1, iconSize / 8);
        const int left = iconX + std::max(2, iconSize / 5);
        const int right = iconX + iconSize - t - std::max(2, iconSize / 5);
        const int top = iconY + std::max(2, iconSize / 4);
        const int bottom = iconY + iconSize - std::max(2, iconSize / 4);
        fillRect(r, iconFg, left, top, t, bottom - top + 1);
        fillRect(r, iconFg, right, top, t, bottom - top + 1);
        SDL_SetRenderDrawColor(r, iconFg.r, iconFg.g, iconFg.b, iconFg.a);
        drawThickLine(r, left, top, (left + right) / 2, bottom, t);
        drawThickLine(r, right, top, (left + right) / 2, bottom, t);
    }

    const std::string title = "Maestro";
    if (font.ok()) {
        void* tex = font.textTexture(r, title, fg.r, fg.g, fg.b);
        if (tex) {
            SDL_Texture* t = static_cast<SDL_Texture*>(tex);
            int tw = 0, th = 0;
            SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            const int minX = iconX + iconSize + pad;
            const int maxX =
                buttonRect(GuiTitleBarButton::Minimize, width, height).x -
                tw - pad;
            int x = (width - tw) / 2;
            if (maxX >= minX) x = std::min(std::max(x, minX), maxX);
            const int y = (height - th) / 2;
            SDL_Rect dst{x, y, tw, th};
            SDL_RenderCopy(r, t, nullptr, &dst);
        }
    }

    const GuiTitleBarButton buttons[] = {
        GuiTitleBarButton::Minimize,
        GuiTitleBarButton::Maximize,
        GuiTitleBarButton::Close,
    };
    for (GuiTitleBarButton button : buttons) {
        const GuiTitleBarRect rect = buttonRect(button, width, height);
        const bool hovered = hover_ == button;
        const bool pressed = pressed_ == button;
        const bool close = button == GuiTitleBarButton::Close;
        if (pressed || hovered) {
            const GuiColor fill = close
                                      ? closeHover
                                      : (pressed ? pressedBg : hoverBg);
            fillRect(r, fill, rect.x, rect.y, rect.w, rect.h);
        }
        const GuiColor glyph = close && (hovered || pressed) ? closeFg : fg;
        drawButtonGlyph(r, button, rect, glyph, maximized_);
    }
#else
    (void)sdlRenderer;
    (void)font;
    (void)width;
    (void)height;
    (void)dark;
#endif
}
