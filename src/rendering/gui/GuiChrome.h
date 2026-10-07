#pragma once

#include <cstdint>
#include <string>

#include "base/utf8.h"
#include "rendering/ChromeData.h"
#include "rendering/ChromeLayout.h"
#include "rendering/RenderUtil.h"
#include "rendering/Style.h"

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

// Fondos por tema (paridad aproximada con TtyTheme dark/light).
inline GuiColor background(bool dark) {
    return dark ? GuiColor{0x12, 0x13, 0x14} : GuiColor{0xFF, 0xFF, 0xFF};
}
inline GuiColor statusBackground(bool dark) {
    return dark ? GuiColor{0x19, 0x1A, 0x1B} : GuiColor{0xEC, 0xEC, 0xEC};
}
inline GuiColor messageBackground(bool dark) {
    return dark ? GuiColor{0x12, 0x13, 0x14} : GuiColor{0xFF, 0xFF, 0xFF};
}
inline GuiColor cursorColor(bool dark) {
    return dark ? GuiColor{0xD4, 0xD4, 0xD4} : GuiColor{0x1E, 0x1E, 0x1E};
}

// Texto por rol (paridad con los ANSI de TtyTheme, aproximados a RGB).
inline GuiColor colorFor(StyleRole role, bool dark) {
    switch (role) {
        case StyleRole::StatusBase:
            return dark ? GuiColor{0x8C, 0x8C, 0x8C} : GuiColor{0xEB, 0xEB, 0xEB};
        case StyleRole::StatusName:
            return dark ? GuiColor{0x8C, 0x8C, 0x8C} : GuiColor{0x1E, 0x1E, 0x1E};
        case StyleRole::StatusPath:
            return dark ? GuiColor{0x8C, 0x8C, 0x8C} : GuiColor{0x6E, 0x6E, 0x6E};
        case StyleRole::StatusModified:
            return GuiColor{0xDD, 0xDD, 0x00};
        case StyleRole::StatusAccentDefault:
        case StyleRole::AccentNavegacion:
        case StyleRole::AccentInteraccion:
        case StyleRole::AccentSeleccion:
        case StyleRole::AccentComando:
        case StyleRole::AccentBuffers:
        case StyleRole::AccentGuardar:
        case StyleRole::AccentAbrir:
            return dark ? GuiColor{0x51, 0xAF, 0xEF} : GuiColor{0x00, 0x5F, 0xA0};
        case StyleRole::MsgPrompt:
            return dark ? GuiColor{0xFF, 0xFF, 0xFF} : GuiColor{0x1E, 0x1E, 0x1E};
        case StyleRole::MsgSuccess:
        case StyleRole::MsgWarning:
        case StyleRole::MsgError:
        case StyleRole::MsgInfo:
        default:
            return dark ? GuiColor{0xD4, 0xD4, 0xD4} : GuiColor{0x1E, 0x1E, 0x1E};
    }
}

inline GuiColor messageColor(MessageKind kind, bool dark) {
    switch (kind) {
        case MessageKind::Prompt:
            return colorFor(StyleRole::MsgPrompt, dark);
        case MessageKind::Success:
            return colorFor(StyleRole::MsgSuccess, dark);
        case MessageKind::Warning:
            return colorFor(StyleRole::MsgWarning, dark);
        case MessageKind::Error:
            return colorFor(StyleRole::MsgError, dark);
        case MessageKind::Info:
        default:
            return colorFor(StyleRole::MsgInfo, dark);
    }
}

// Bloque derecho del StatusBar (política compartida, sin duplicar):
// override tal cual o % + (fila,col). Reexportado para callers que solo
// conocen guichrome::.
inline std::string rightBlock(const StatusBarData& st) {
    return chrome::statusRightBlock(st);
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
