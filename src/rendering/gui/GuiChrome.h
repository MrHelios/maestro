#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "base/utf8.h"
#include "rendering/ChromeData.h"
#include "rendering/ChromeLayout.h"
#include "rendering/RenderUtil.h"
#include "rendering/Style.h"
#include "rendering/Theme.h"

// ---------------------------------------------------------------------------
// GuiChrome: presentación PLANA del chrome para el backend SDL2
// (sin SDL, sin TTF).
//
// Solo presentación: el texto plano que la GUI pinta como texturas y el
// mapeo StyleRole -> RGB. La POLÍTICA de layout (topes, presupuestos, orden
// de sacrificio nombre/ruta/estado, bloque derecho) vive en
// rendering/ChromeLayout.h y es la MISMA que usa TtyChrome: acá no se
// duplica ningún tope ni presupuesto.
//
//   ChromeLayout (layout compartido)
//       ├── TtyChrome (fragmentos -> ANSI)
//       └── GuiChrome (línea plana -> RGB, este archivo)
//
// El posicionamiento del cursor en el MessageBar tampoco se duplica:
// chrome::messageBarCursorCell (RenderUtil.h), igual que FrameBuilder/TTY.
//
// CONTRATO DE ANCHO (deliberado, no uniformar):
//   statusLine()  -> EXACTO `width` columnas (rellena con fill).
//   messageLine() -> A LO SUMO `width` columnas (NO rellena).
// El StatusBar necesita ancho exacto por paridad byte a byte con el TTY
// (stripAnsi(TtyChrome) == statusLine, congelado en tests) y porque su
// textura cubre toda la fila. El MessageBar no rellena a propósito: el
// renderer ya limpió el fondo de la fila (messageBackground) y pinta el
// texto como textura encima, así que un fill de espacios solo agrandaría
// la textura sin efecto visual. No asumir ancho exacto en messageLine().
// ---------------------------------------------------------------------------

struct GuiColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0xFF;
    bool operator==(const GuiColor& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const GuiColor& o) const { return !(*this == o); }
};

namespace guichrome {

inline GuiColor toGuiColor(const ThemeColor& c) {
    return GuiColor{c.r, c.g, c.b, 0xFF};
}

inline ThemePalette palette(bool dark) {
    return dark ? darkPalette() : lightPalette();
}

inline GuiColor background(const ThemePalette& p) {
    return toGuiColor(p.background);
}
inline GuiColor background(bool dark) { return background(palette(dark)); }

inline GuiColor statusBackground(const ThemePalette& p) {
    return toGuiColor(p.statusBackground);
}
inline GuiColor statusBackground(bool dark) {
    return statusBackground(palette(dark));
}

inline GuiColor messageBackground(const ThemePalette& p) {
    return toGuiColor(p.messageBackground);
}
inline GuiColor messageBackground(bool dark) {
    return messageBackground(palette(dark));
}

inline GuiColor cursorColor(const ThemePalette& p) {
    return toGuiColor(p.cursor);
}
inline GuiColor cursorColor(bool dark) { return cursorColor(palette(dark)); }

// ---------------------------------------------------------------------------
// Política del cursor en bloque (inversión real, paridad con el TTY).
//
//   fondo del bloque = cursorColor(dark) (uniforme en toda la celda).
//   fg del glifo    = fondo EFECTIVO de la celda (inversión: el carácter se
//                     pinta del color de la superficie que tapa, como el
//                     reverse del cursor del TTY, que lo mantiene legible).
//
// El fondo efectivo NO es siempre el fondo base: depende del rol del
// segmento y de la línea actual (Selection, BracketMatch, CurrentLine /
// sintaxis en línea actual, ListSelected en modales). El renderer lo
// recupera vía styleFor (contenido) o el estilo de lista (modales); nunca
// supone el fondo base.
//
// Garantía de contraste: el fg efectivo debe contrastar con el fondo del
// bloque (>= kCursorGlyphMinContrast, WCAG). Si el fondo efectivo no
// contrasta (p. ej. bracket amarillo sobre cursor claro en dark), se usa
// el fondo base como fg, que por construcción contrasta al máximo con el
// cursor. cursorGlyphFg() implementa esta decisión; los tests la congelan
// para cada familia de fondo × tema.
// ---------------------------------------------------------------------------
inline constexpr double kCursorGlyphMinContrast = 3.0;

inline double cursorLuminance(GuiColor c) {
    const double r = c.r / 255.0;
    const double g = c.g / 255.0;
    const double b = c.b / 255.0;
    const double rl =
        (r <= 0.03928) ? r / 12.92 : std::pow((r + 0.055) / 1.055, 2.4);
    const double gl =
        (g <= 0.03928) ? g / 12.92 : std::pow((g + 0.055) / 1.055, 2.4);
    const double bl =
        (b <= 0.03928) ? b / 12.92 : std::pow((b + 0.055) / 1.055, 2.4);
    return 0.2126 * rl + 0.7152 * gl + 0.0722 * bl;
}

inline double cursorContrastRatio(GuiColor a, GuiColor b) {
    const double l1 = cursorLuminance(a);
    const double l2 = cursorLuminance(b);
    const double hi = l1 > l2 ? l1 : l2;
    const double lo = l1 > l2 ? l2 : l1;
    return (hi + 0.05) / (lo + 0.05);
}

// Fg del glifo sobre el bloque: inversión exacta (fondo efectivo) cuando
// contrasta con el cursor; fondo base como fallback garantizado si no.
inline GuiColor cursorGlyphFg(GuiColor cellBg, const ThemePalette& p) {
    if (cursorContrastRatio(cellBg, cursorColor(p)) >=
        kCursorGlyphMinContrast)
        return cellBg;
    return background(p);
}
inline GuiColor cursorGlyphFg(GuiColor cellBg, bool dark) {
    return cursorGlyphFg(cellBg, palette(dark));
}

inline GuiColor colorFor(StyleRole role, const ThemePalette& p) {
    return toGuiColor(themeFgFor(p, role).value_or(p.defaultFg));
}
inline GuiColor colorFor(StyleRole role, bool dark) {
    return colorFor(role, palette(dark));
}

// Fondo por familia. La DECISIÓN de qué familia toca a cada segmento y el
// mapeo rol/familia -> color viven una sola vez en Theme.h
// (themeStyleFor/themeFgFor/themeBgFor + rowBgKindFor). Acá solo se
// convierten esos ThemeColor al GuiColor del backend.
inline GuiColor currentLineBackground(const ThemePalette& p) {
    return toGuiColor(p.currentLineBg);
}
inline GuiColor currentLineBackground(bool dark) {
    return currentLineBackground(palette(dark));
}
inline GuiColor selectionBackground(const ThemePalette& p) {
    return toGuiColor(p.selectionBg);
}
inline GuiColor selectionBackground(bool dark) {
    return selectionBackground(palette(dark));
}
inline GuiColor bracketBackground(const ThemePalette& p) {
    return toGuiColor(p.bracketMatchBg);
}
inline GuiColor bracketBackground(bool dark) {
    return bracketBackground(palette(dark));
}
inline GuiColor bracketBackground() {
    return bracketBackground(darkPalette());
}
inline GuiColor listSelectedBackground(const ThemePalette& p) {
    return toGuiColor(p.listSelectedBg);
}
inline GuiColor listSelectedBackground(bool dark) {
    return listSelectedBackground(palette(dark));
}
inline GuiColor guiBgFor(RowBgKind kind, const ThemePalette& p) {
    return toGuiColor(themeBgFor(p, kind));
}
inline GuiColor guiBgFor(RowBgKind kind, bool dark) {
    return guiBgFor(kind, palette(dark));
}

// Estilo resuelto para un segmento del Frame (fg + bg concretos).
// Usa el mismo ThemeStyle semántico que el TTY; los opcionales heredan del
// tema (defaultFg / background). (GutterBlank no necesita caso propio: su
// fg ya es el fondo, los espacios son invisibles.)
struct GuiStyle {
    GuiColor fg;
    GuiColor bg;
};
inline GuiStyle styleFor(StyleRole role, bool isCurrentLine,
                          const ThemePalette& p) {
    const ThemeStyle st = themeStyleFor(p, role, isCurrentLine);
    return GuiStyle{toGuiColor(st.fg.value_or(p.defaultFg)),
                    toGuiColor(st.bg.value_or(p.background))};
}
inline GuiStyle styleFor(StyleRole role, bool isCurrentLine, bool dark) {
    return styleFor(role, isCurrentLine, palette(dark));
}

inline GuiColor messageColor(MessageKind kind, const ThemePalette& p) {
    switch (kind) {
        case MessageKind::Prompt:
            return colorFor(StyleRole::MsgPrompt, p);
        case MessageKind::Success:
            return colorFor(StyleRole::MsgSuccess, p);
        case MessageKind::Warning:
            return colorFor(StyleRole::MsgWarning, p);
        case MessageKind::Error:
            return colorFor(StyleRole::MsgError, p);
        case MessageKind::Info:
        default:
            return colorFor(StyleRole::MsgInfo, p);
    }
}
inline GuiColor messageColor(MessageKind kind, bool dark) {
    return messageColor(kind, palette(dark));
}

// Bloque derecho del StatusBar (política compartida, sin duplicar):
// override tal cual o % + (fila,col). Reexportado para callers que solo
// conocen guichrome::.
inline std::string rightBlock(const StatusBarData& st) {
    return chrome::statusRightBlock(st);
}

// ---------------------------------------------------------------------------
// Segmentos con rol para pintado fragmentado (anotación, no layout).
//
// REGLA: estas funciones NO implementan truncado, relleno, presupuestos ni
// composición: reutilizan `ChromeLayout.h` (layoutStatusLeft /
// statusRightBlock / statusLeftPlainText) y `chrome::colCount` /
// `utf8::truncate`, las mismas primitivas de `statusLine()` / `messageLine()`
// y de `TtyChrome`. Solo etiquetan con `StyleRole` los fragmentos ya
// recortados. El ancho se mide en columnas visuales, nunca en bytes
// (`std::string::size()` rompería UTF-8 ancho).
//
//   statusSegments(st, accent, width): concat(textos) == statusLine(st,width)
//   messageSegments(msg, width):      concat(textos) == messageLine(msg,width)
//
// Roles (paridad con TtyChrome::appendStatusBar/appendMessageBar):
//   nombre -> StatusName (o StatusModified si modified && !showMarker, igual
//   que TTY que pinta el nombre con statusBarModified en ese caso),
//   marker [*] -> StatusModified, sep+path -> StatusPath,
//   sep+estado -> accent, pads/fill/right -> StatusBase.
//   MessageBar: prefijo negrita -> MsgPrompt, resto -> rol de `kind`
//   (con la misma excepción que TTY: sufijo de un Prompt con boldPrefix va
//   con MsgInfo, no con MsgPrompt; sin boldPrefix todo va con el rol de kind).
// ---------------------------------------------------------------------------
struct GuiChromeSeg {
    std::string text;
    StyleRole role = StyleRole::Default;
};

inline StyleRole messageKindRole(MessageKind kind) {
    switch (kind) {
        case MessageKind::Prompt:
            return StyleRole::MsgPrompt;
        case MessageKind::Success:
            return StyleRole::MsgSuccess;
        case MessageKind::Warning:
            return StyleRole::MsgWarning;
        case MessageKind::Error:
            return StyleRole::MsgError;
        case MessageKind::Info:
        default:
            return StyleRole::MsgInfo;
    }
}

inline std::vector<GuiChromeSeg> statusSegments(
    const StatusBarData& st, StyleRole accent, int width) {
    std::vector<GuiChromeSeg> segs;
    if (width <= 0) return segs;
    using namespace chrome;
    std::string rightVis = statusRightBlock(st);
    const int padL = std::min(kMessageBarPadLeft, width);
    const int padR =
        std::min(kMessageBarPadRight, std::max(0, width - padL));
    const int rightBudget = std::max(0, width - padL - padR);
    if (colCount(rightVis) > rightBudget)
        rightVis = utf8::truncate(rightVis, rightBudget);
    const int rightVisW = colCount(rightVis);
    const int leftBudget = std::max(0, width - padL - padR - rightVisW);

    const StatusLeft left =
        layoutStatusLeft(st.name, st.path, st.estado, st.modified, leftBudget);
    const std::string leftText = statusLeftPlainText(left);
    const int plainW = colCount(leftText);

    if (padL > 0)
        segs.push_back({std::string(static_cast<size_t>(padL), ' '),
                        StyleRole::StatusBase});
    if (left.statusOnly) {
        if (!left.status.empty())
            segs.push_back({left.status, accent});
    } else {
        if (left.modified && !left.showMarker) {
            segs.push_back({left.name, StyleRole::StatusModified});
        } else {
            segs.push_back({left.name, StyleRole::StatusName});
            if (left.showMarker) {
                segs.push_back({std::string(kStatusModifiedMarker.data(),
                                            kStatusModifiedMarker.size()),
                                StyleRole::StatusModified});
            }
        }
        if (!left.path.empty()) {
            std::string pp(kStatusSeparator.data(), kStatusSeparator.size());
            pp += left.path;
            segs.push_back({std::move(pp), StyleRole::StatusPath});
        }
        {
            std::string s(kStatusSeparator.data(), kStatusSeparator.size());
            s += left.status;
            segs.push_back({std::move(s), accent});
        }
    }
    const int fill = std::max(0, width - padL - plainW - padR - rightVisW);
    if (fill > 0)
        segs.push_back({std::string(static_cast<size_t>(fill), ' '),
                        StyleRole::StatusBase});
    if (padR > 0)
        segs.push_back({std::string(static_cast<size_t>(padR), ' '),
                        StyleRole::StatusBase});
    if (!rightVis.empty())
        segs.push_back({std::move(rightVis), StyleRole::StatusBase});
    return segs;
}

inline std::vector<GuiChromeSeg> messageSegments(const MessageBarData& msg,
                                                int width) {
    std::vector<GuiChromeSeg> segs;
    if (width <= 0) return segs;
    const int padL = std::min(kMessageBarPadLeft, width);
    const int padR =
        std::min(kMessageBarPadRight, std::max(0, width - padL));
    const int avail = std::max(0, width - padL - padR);
    const std::string vis = utf8::truncate(msg.text, avail);
    if (padL > 0)
        segs.push_back({std::string(static_cast<size_t>(padL), ' '),
                        StyleRole::Default});
    if (msg.boldPrefix.has_value() && *msg.boldPrefix > 0) {
        // Contrato (ChromeData.h): bytes >= 0 sobre límite UTF-8 válido
        // (las etiquetas son ASCII, se cumple por construcción). El assert
        // caza al productor en debug, igual que TtyChrome.
        assert(*msg.boldPrefix >= 0);
        int preBytes = *msg.boldPrefix;
        if (preBytes > static_cast<int>(vis.size()))
            preBytes = static_cast<int>(vis.size());
        // Defensa en profundidad: si un productor futuro violara el contrato
        // con un corte a mitad de celda, retrocede al inicio de celda para
        // no emitir nunca mitades inválidas a medición/texturas. Con el
        // contrato vigente es no-op (ya cae en límite válido).
        preBytes = utf8::alignStart(vis, preBytes);
        const std::string pre = vis.substr(0, static_cast<size_t>(preBytes));
        const std::string suf = vis.substr(static_cast<size_t>(preBytes));
        if (!pre.empty())
            segs.push_back({pre, StyleRole::MsgPrompt});
        if (!suf.empty()) {
            const StyleRole sufRole = (msg.kind == MessageKind::Prompt)
                                          ? StyleRole::MsgInfo
                                          : messageKindRole(msg.kind);
            segs.push_back({std::move(suf), sufRole});
        }
    } else {
        if (!vis.empty())
            segs.push_back({std::move(vis), messageKindRole(msg.kind)});
    }
    if (padR > 0)
        segs.push_back({std::string(static_cast<size_t>(padR), ' '),
                        StyleRole::Default});
    return segs;
}

// Línea PLANA del StatusBar (" <izquierda>  <right>", EXACTO `width`
// columnas). Misma composición que TTY: layoutStatusLeft + pads + fill +
// right; solo cambia que acá no hay ANSI entre fragmentos (ver
// chrome::statusLeftPlainText). La paridad byte a byte con
// stripAnsi(TtyChrome::renderStatusBar) está congelada en tests.
inline std::string statusLine(const StatusBarData& st, int width) {
    using namespace chrome;
    if (width <= 0) return {};
    std::string rightVis = statusRightBlock(st);
    const int padL = std::min(kMessageBarPadLeft, width);
    const int padR =
        std::min(kMessageBarPadRight, std::max(0, width - padL));
    const int rightBudget = std::max(0, width - padL - padR);
    if (colCount(rightVis) > rightBudget)
        rightVis = utf8::truncate(rightVis, rightBudget);
    const int rightVisW = colCount(rightVis);
    const int leftBudget = std::max(0, width - padL - padR - rightVisW);

    const StatusLeft left =
        layoutStatusLeft(st.name, st.path, st.estado, st.modified, leftBudget);
    const std::string leftText = statusLeftPlainText(left);
    const int plainW = colCount(leftText);

    std::string out;
    out.append(static_cast<size_t>(padL), ' ');
    out += leftText;
    const int fill = std::max(0, width - padL - plainW - padR - rightVisW);
    out.append(static_cast<size_t>(fill), ' ');
    out.append(static_cast<size_t>(padR), ' ');
    out += rightVis;
    // Garantía: EXACTO width (recorte defensivo por si algún conteo desvía).
    if (colCount(out) > width) out = utf8::truncate(out, width);
    while (colCount(out) < width) out += ' ';
    return out;
}

// Linea PLANA del MessageBar: " <texto truncado>   " (pad izq 1, der 3),
// SIN fill: a lo sumo `width` columnas (ver CONTRATO DE ANCHO arriba).
// El cursor se posiciona con chrome::messageBarCursorCell (mismo truncado
// que TtyChrome).
inline std::string messageLine(const MessageBarData& msg, int width) {
    if (width <= 0) return {};
    const int padL = std::min(kMessageBarPadLeft, width);
    const int padR =
        std::min(kMessageBarPadRight, std::max(0, width - padL));
    const int avail = std::max(0, width - padL - padR);
    const std::string vis = utf8::truncate(msg.text, avail);
    std::string out;
    out.append(static_cast<size_t>(padL), ' ');
    out += vis;
    out.append(static_cast<size_t>(padR), ' ');
    return out;
}

}  // namespace guichrome
