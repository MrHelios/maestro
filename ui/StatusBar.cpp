#include "ui/StatusBar.h"

#include <sstream>
#include <algorithm>
#include "ui/RenderUtil.h"

using namespace chrome;

namespace {

// Estilo de la fila de mensajes segun el tipo (paso 8). El tipo lo decide
// la pantalla/el Editor cuando produce el Message; aqui se traduce al color
// del Theme. Info usa el estilo de mensaje base del Theme; Prompt resalta
// la entrada del usuario en negrita (v1.3).
const std::string& messageStyle(const Theme& theme, MessageKind kind) {
    switch (kind) {
        case MessageKind::Info:    return theme.message;
        case MessageKind::Success: return theme.success;
        case MessageKind::Warning: return theme.warning;
        case MessageKind::Error:   return theme.error;
        case MessageKind::Prompt:  return theme.prompt;
    }
    return theme.message;
}

// ---- Limites fijos de la barra de estado (bloque izquierdo) ----
constexpr int kNameMax    = 30;   // columnas maximas del nombre
constexpr int kPathMax    = 40;   // columnas maximas de la ruta
constexpr int kNamePathMax = 60;  // tope combinado nombre + ruta
constexpr std::string_view kSeparator = " - ";
constexpr std::string_view kModifiedMarker = " [*]";

// Construye el bloque izquierdo respetando los límites de nombre/ruta y
// el presupuesto disponible. La ruta se sacrifica antes que el nombre;
// si tampoco cabe el nombre, solo conserva el estado.
// Piezas `name`/`status` separadas para aplicar estilos del Theme
// (T.statusBarName, T.statusBarPath, T.statusBarModified y accent).
//
// CONTRATO DE ANCHO:
// layoutLeftBlock() garantiza que el bloque que render() construye a partir
// de BarLeft no supera `budget` columnas visibles. render() usa el mismo
// calculo para determinar el relleno restante de la barra.
struct BarLeft {
    std::string name;
    std::string path;
    std::string status;
    bool modified;
    bool showMarker;
    bool statusOnly;
};

BarLeft layoutLeftBlock(const std::string& rawName, const std::string& rawPath,
                     const std::string& status, bool modified, int budget) {
    if (budget <= 0) return {"", "", utf8::truncate(status, 0), false, false, true};

    std::string name = rawName;
    if (name.empty()) name = "[sin nombre]";

    std::string path = rawPath;

    int markerW = modified ? colCount(kModifiedMarker) : 0;
    int nameBudget = kNameMax - markerW;
    name = utf8::truncate(name, nameBudget);
    if (colCount(path) > kPathMax)
        path = utf8TruncateFront(path, kPathMax);
    if (colCount(name) + markerW + colCount(path) > kNamePathMax)
        path = utf8TruncateFront(path, std::max(0, kNamePathMax - colCount(name) - markerW));

    int statusW = colCount(status);
    int sepW = colCount(kSeparator);
    int partsBudget = budget - statusW - sepW;
    if (budget <= statusW || partsBudget <= 0)
        return {"", "", utf8::truncate(status, budget), false, false, true};

    int nameW = colCount(name);
    int effectiveNameW = nameW + markerW;
    if (effectiveNameW >= partsBudget) {
        if (modified) {
            if (partsBudget < markerW) {
                // Borde extremadamente angosto: no cabe [*] completo.
                name = utf8::truncate(name, partsBudget);
                return {name, "", status, true, false, false};
            }
            name = utf8::truncate(name, partsBudget - markerW);
            return {name, "", status, true, true, false};
        }
        name = utf8::truncate(name, partsBudget);
        return {name, "", status, false, false, false};
    }

    int pathBudget = partsBudget - effectiveNameW - sepW;
    if (path.empty() || pathBudget <= 0) return {name, "", status, modified, modified, false};
    return {name, utf8TruncateFront(path, pathBudget),
            status, modified, modified, false};
}

} // namespace

std::string StatusBar::render(const Rect& area, const StatusBarData& data) {
    const int width = area.width;
    const Theme& T = theme_;
    std::ostringstream out;

    // Fila fija de la barra de estado. El fondo y los estilos de cada segmento
    // provienen del Theme. El contenido se compone de nombre, ruta, estado,
    // relleno y bloque derecho anclado al borde.
    out << "\x1b[K";
    out << T.statusBar;

    // Bloque derecho: si hay un `right` explicito (pantallas sin documento:
    // selector, explorador) se usa tal cual; si no, se calcula la posicion
    // vertical del cursor como porcentaje del archivo (0% al inicio, 100%
    // al final; una sola linea => 0%) y luego (fila,columna), anclado a la
    // derecha.
    std::string rightBlock;
    if (!data.right.empty()) {
        rightBlock = data.right;
    } else {
        int pct = data.totalLines <= 1 ? 0
                                       : (data.cursorLine * 100) / (data.totalLines - 1);
        rightBlock = std::to_string(pct) + "% (" +
                     std::to_string(data.cursorLine + 1) + "," +
                     std::to_string(data.cursorCol + 1) + ")";
    }
    int rightW = colCount(rightBlock);

    // ---- Cota de ancho (v1.1): la barra NUNCA escribe fuera del ancho de
    // la terminal. En una terminal demasiado angosta el contenido fijo
    // (paddings + bloque derecho) no cabe entero; el pad derecho cede
    // primero, luego el bloque derecho (el bloque izquierdo ya sacrifica
    // dentro de su presupuesto, ver layoutLeftBlock). Con esto se garantiza
    // que la fila fija ocupe EXACTAMENTE `width` columnas (nada mas).
    const int padL = std::min(kStatusBarPadLeft, width);
    const int padR = std::min(kStatusBarPadRight, std::max(0, width - padL));
    const int rightBudget = std::max(0, width - padL - padR);
    if (rightW > rightBudget) {
        rightBlock = utf8::truncate(rightBlock, rightBudget);
        rightW = colCount(rightBlock);
    }

    int leftBudget = std::max(0, width - padL - padR - rightW);
    BarLeft left = layoutLeftBlock(data.name, data.path, data.estado,
                                data.modified, leftBudget);

    int plainW;
    if (left.statusOnly) {
        plainW = colCount(left.status);
    } else {
        int sepCount = left.path.empty() ? 1 : 2;
        int markerW = left.showMarker ? colCount(kModifiedMarker) : 0;
        int sepW = colCount(kSeparator);
        plainW = colCount(left.name) + markerW + colCount(left.path) +
                 colCount(left.status) + sepCount * sepW;
    }

    for (int i = 0; i < padL; ++i) out << ' ';

    const std::string accent = data.estadoAccent.empty() ? T.statusBarAccent
                                                           : data.estadoAccent;

    if (left.statusOnly) {
        out << accent << left.status << T.reset << T.statusBar;
    } else {
        if (left.modified && !left.showMarker) {
            out << T.statusBarModified << left.name << T.reset << T.statusBar;
        } else {
            out << T.statusBarName << left.name << T.reset << T.statusBar;
            if (left.showMarker) out << T.statusBarModified << kModifiedMarker << T.reset << T.statusBar;
        }
        if (!left.path.empty()) {
            out << T.statusBarPath << kSeparator << left.path << T.reset << T.statusBar;
        }
        out << accent << kSeparator << left.status << T.reset << T.statusBar;
    }

    int fill = std::max(0, width - padL - plainW - padR - rightW);
    for (int i = 0; i < fill; ++i) out << ' ';
    for (int i = 0; i < padR; ++i) out << ' ';
    out << rightBlock;

    out << T.reset; // reset de estilo

    // Fila de mensajes (fila propia). Solo existe si el area de la barra
    // tiene mas de una fila. El texto se colorea por tipo (Message.kind);
    // el padding izquierdo y derecho coincide con el de la barra superior
    // para alinear el texto.
    if (area.height >= 2) {
        out << "\r\n";
        out << "\x1b[K";
        // Misma cota: la fila de mensajes tampoco escribe fuera del ancho.
        // El padding derecho cede ante un terminal muy angosto.
        const int msgPadL = std::min(kStatusBarPadLeft, width);
        const int msgPadR = std::min(kStatusBarPadRight,
                                     std::max(0, width - msgPadL));
        for (int i = 0; i < msgPadL; ++i) out << ' ';
        const std::string& style = messageStyle(T, data.message.kind);
        out << style;
        out << utf8::truncate(data.message.text,
                              std::max(0, width - msgPadL - msgPadR));
        if (!style.empty()) out << T.reset;
        for (int i = 0; i < msgPadR; ++i) out << ' ';
    }

    return out.str();
}