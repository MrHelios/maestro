#include "rendering/tty/TtyChrome.h"

#include <algorithm>
#include <cassert>
#include "rendering/ChromeLayout.h"
#include "rendering/RenderUtil.h"

using namespace chrome;

namespace {

// Estilo del MessageBar segun el tipo (paso 8). El tipo lo decide
// la pantalla/el Editor cuando produce el mensaje; aqui se traduce al color
// del TtyTheme. Info usa el estilo de mensaje base del TtyTheme; Prompt usa
// theme.prompt (negrita en los temas por defecto, personalizable).
const std::string& messageStyle(const TtyTheme& theme, MessageKind kind) {
    switch (kind) {
        case MessageKind::Info:    return theme.message;
        case MessageKind::Success: return theme.success;
        case MessageKind::Warning: return theme.warning;
        case MessageKind::Error:   return theme.error;
        case MessageKind::Prompt:  return theme.prompt;
    }
    return theme.message;
}

// (Composición del StatusBar en rendering/ChromeLayout.h, compartida con la
// GUI: layoutStatusLeft + statusRightBlock + statusLeftPlainText.)

} // namespace

std::string TtyChrome::renderStatusBar(int width, const StatusBarData& status,
                                     StyleRole accentRole) const {
    std::string out;
    // Fila codificada ~ ancho visible + ANSI (base + fragmentos + resets).
    out.reserve(static_cast<size_t>(std::max(0, width)) + 256);
    appendStatusBar(out, width, status, accentRole);
    return out;
}

void TtyChrome::appendStatusBar(std::string& out, int width,
                             const StatusBarData& status,
                             StyleRole accentRole) const {
    const TtyTheme& T = theme_;

    // Fila superior del chrome: StatusBar fijo. El fondo y los estilos de cada segmento
    // provienen del TtyTheme. El contenido se compone de nombre, ruta, estado,
    // relleno y bloque derecho anclado al borde.
    // NOTA: escritura directa sobre el buffer del caller (sin ostringstream
    // ni strings intermedios): este es el camino caliente del frame.
    out += "\x1b[K";
    out += T.statusBar;

    // Bloque derecho (política compartida en ChromeLayout.h): override tal
    // cual o % + (fila,columna), anclado a la derecha.
    std::string rightBlock = statusRightBlock(status);
    int rightW = colCount(rightBlock);

    // ---- Cota de ancho (v1.1): el StatusBar NUNCA escribe fuera del ancho de
    // la terminal. En una terminal demasiado angosta el contenido fijo
    // (paddings + bloque derecho) no cabe entero; el pad derecho cede
    // primero, luego el bloque derecho (el bloque izquierdo ya sacrifica
    // dentro de su presupuesto, ver layoutStatusLeft). Con esto se garantiza
    // que la fila fija ocupe EXACTAMENTE `width` columnas (nada mas).
    const int padL = std::min(kChromePadLeft, width);
    const int padR = std::min(kChromePadRight, std::max(0, width - padL));
    const int rightBudget = std::max(0, width - padL - padR);
    if (rightW > rightBudget) {
        rightBlock = utf8::truncate(rightBlock, rightBudget);
        rightW = colCount(rightBlock);
    }

    int leftBudget = std::max(0, width - padL - padR - rightW);
    StatusLeft left = layoutStatusLeft(status.name, status.path, status.estado,
                                       status.modified, leftBudget);

    int plainW;
    if (left.statusOnly) {
        plainW = colCount(left.status);
    } else {
        int sepCount = left.path.empty() ? 1 : 2;
        int markerW = left.showMarker ? colCount(kStatusModifiedMarker) : 0;
        int sepW = colCount(kStatusSeparator);
        plainW = colCount(left.name) + markerW + colCount(left.path) +
                 colCount(left.status) + sepCount * sepW;
    }

    for (int i = 0; i < padL; ++i) out += ' ';

    // Rol -> ANSI vía el TtyTheme propio (TtyTheme conoce Style, nunca al revés).
    const std::string& accent = themeAnsiFor(T, accentRole);

    if (left.statusOnly) {
        out += accent;
        out += left.status;
        out += T.reset;
        out += T.statusBar;
    } else {
        if (left.modified && !left.showMarker) {
            out += T.statusBarModified;
            out += left.name;
            out += T.reset;
            out += T.statusBar;
        } else {
            out += T.statusBarName;
            out += left.name;
            out += T.reset;
            out += T.statusBar;
            if (left.showMarker) {
                out += T.statusBarModified;
                out.append(kStatusModifiedMarker.data(), kStatusModifiedMarker.size());
                out += T.reset;
                out += T.statusBar;
            }
        }
        if (!left.path.empty()) {
            out += T.statusBarPath;
            out.append(kStatusSeparator.data(), kStatusSeparator.size());
            out += left.path;
            out += T.reset;
            out += T.statusBar;
        }
        out += accent;
        out.append(kStatusSeparator.data(), kStatusSeparator.size());
        out += left.status;
        out += T.reset;
        out += T.statusBar;
    }

    int fill = std::max(0, width - padL - plainW - padR - rightW);
    out.append(static_cast<size_t>(fill), ' ');
    out.append(static_cast<size_t>(padR), ' ');
    out += rightBlock;

    out += T.reset; // reset de estilo
}

std::string TtyChrome::renderMessageBar(int width, const MessageBarData& message) const {
    std::string out;
    out.reserve(static_cast<size_t>(std::max(0, width)) + 64);
    appendMessageBar(out, width, message);
    return out;
}

void TtyChrome::appendMessageBar(std::string& out, int width,
                              const MessageBarData& message) const {
    const TtyTheme& T = theme_;
    // Fila inferior del chrome: MessageBar (fila propia). El texto se colorea por tipo
    // (MessageBarData.kind); el padding izquierdo y derecho coincide con el del
    // StatusBar superior para alinear el texto.
    // Si hay boldPrefix: solo esos primeros bytes van en negrita
    // (theme.prompt, etiqueta del prompt); el resto (input del usuario +
    // sufijos decorativos) va sin negrita.
    out += "\x1b[K";
    // Misma cota: el MessageBar tampoco escribe fuera del ancho.
    // El padding derecho cede ante un terminal muy angosto.
    const int msgPadL = std::min(kChromePadLeft, width);
    const int msgPadR = std::min(kChromePadRight,
                                 std::max(0, width - msgPadL));
    out.append(static_cast<size_t>(msgPadL), ' ');
    const int avail = std::max(0, width - msgPadL - msgPadR);
    const std::string vis =
        utf8::truncate(message.text, avail);
    if (message.boldPrefix.has_value() && *message.boldPrefix > 0) {
        // Invariante del DTO (ver ChromeData.h): el prefijo es >= 0 y cae
        // en un límite UTF-8 válido (las etiquetas son ASCII, se cumple por
        // construcción). El truncado previo también cae en borde de
        // carácter, así que el min() con lo visible sigue siendo seguro.
        // No se repara un negativo silenciosamente: es un bug del productor.
        assert(*message.boldPrefix >= 0);
        int preBytes = *message.boldPrefix;
        if (preBytes > static_cast<int>(vis.size())) preBytes = static_cast<int>(vis.size());
        const std::string pre = vis.substr(0, static_cast<size_t>(preBytes));
        const std::string suf = vis.substr(static_cast<size_t>(preBytes));
        if (!pre.empty()) {
            out += T.prompt;
            out += pre;
            if (!T.prompt.empty()) out += T.reset;
        }
        if (!suf.empty()) {
            const std::string& sufStyle = (message.kind == MessageKind::Prompt)
                                              ? T.message
                                              : messageStyle(T, message.kind);
            out += sufStyle;
            out += suf;
            if (!sufStyle.empty()) out += T.reset;
        }
    } else {
        const std::string& style = messageStyle(T, message.kind);
        out += style;
        out += vis;
        if (!style.empty()) out += T.reset;
    }
    out.append(static_cast<size_t>(msgPadR), ' ');
}

std::string TtyChrome::render(const Rect& area, const ChromeData& data,
                              StyleRole accentRole) const {
    // Contrato de altura (espejo de computeLayout): height==0 -> sin chrome.
    if (area.height <= 0) return {};
    std::string out;
    out.reserve(static_cast<size_t>(std::max(0, area.width)) * 2 + 512);
    append(out, area, data, accentRole);
    return out;
}

void TtyChrome::append(std::string& out, const Rect& area,
                       const ChromeData& data, StyleRole accentRole) const {
    // Contrato de altura (espejo de computeLayout): height==0 -> sin chrome.
    if (area.height <= 0) return;
    // Una sola reserva para ambas filas (camino caliente): evita el regrowth
    // y los 2 strings intermedios del viejo renderStatusBar + renderMessageBar.
    out.reserve(out.size() +
                static_cast<size_t>(std::max(0, area.width)) * 2 + 512);
    appendStatusBar(out, area.width, data.statusBar, accentRole);
    // Solo existe MessageBar si el area del chrome tiene mas de una fila.
    if (area.height >= 2) {
        out += "\r\n";
        appendMessageBar(out, area.width, data.message);
    }
}