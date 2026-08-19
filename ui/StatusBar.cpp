#include "ui/StatusBar.h"

#include <sstream>
#include <algorithm>
#include "ui/RenderUtil.h"

using namespace chrome;

namespace {

// Estilo de la fila de mensajes segun el tipo (paso 8). El tipo lo decide
// la pantalla/el Editor cuando produce el Message; aqui se traduce al color
// del Theme. Info es el caso base (sin color); Prompt resalta la entrada
// del usuario en negrita (v1.3).
std::string messageStyle(const Theme& theme, MessageKind kind) {
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

// Une `name SEP path` dentro de `budget` columnas, respetando la
// prioridad de sacrificio: la ruta se agota primero (truncada por la
// IZQUIERDA, con "..." al inicio) y el nombre se toca solo como ultimo
// recurso. Para eso se RESERVA el nombre (fijo, sin truncarlo si se
// puede evitar), se resta del presupuesto y el resto entero se da a la
// ruta. Devuelve la parte que cabe del bloque (sin la etiqueta de
// estado).
// Piezas del bloque izquierdo de la barra fija: `name` (nombre[ - ruta])
// y `estado`, devueltos POR SEPARADO para poder colorearlos distinto en
// buildChrome (nombre en blanco, estado en negrita dorada). Respeta los
// limites fijos y, ante falta de espacio (terminal chica), sacrifica primero
// la ruta y despues el nombre. `onlyEstado` queda true cuando no hay sitio
// para nombre+ruta: se muestra solo el estado (sin separador) para no
// exceder el presupuesto.
struct BarLeft {
    std::string name;     // nombre[ [modificado]], sin estilo; vacio si onlyEstado
    std::string path;     // ruta, sin estilo; vacia si no cabe / no aplica
    std::string estado;   // etiqueta de estado, sin estilo
    bool onlyEstado;      // true => no hubo lugar para el contenido
};

BarLeft buildBarLeft(const std::string& rawName, const std::string& rawPath,
                     const std::string& estado, bool modified, int budget) {
    if (budget <= 0) return {"", "", utf8::truncate(estado, 0), true};

    std::string name = rawName;
    if (name.empty()) name = "[sin nombre]";
    const std::string modificado = " [modificado]";

    std::string path = rawPath;

    // Limites fijos (columnas visuales). El sufijo [modificado] se
    // RESERVA entero: se trunca el nombre (no el indicador) para que
    // jamás se pierda la señal de "cambios sin guardar" en pantalla.
    int nameBudget = kNameMax - (modified ? colCount(modificado) : 0);
    name = utf8::truncate(name, nameBudget);
    if (modified) name += modificado;
    // La ruta se acorta por la izquierda: se pierde el inicio cuando
    // excede, manteniendo la cola (donde esta el nombre de archivo).
    if (colCount(path) > kPathMax)
        path = utf8TruncateFront(path, kPathMax);
    if (colCount(name) + colCount(path) > kNamePathMax)
        path = utf8TruncateFront(path, std::max(0, kNamePathMax - colCount(name)));

    int estadoW = colCount(estado);
    const std::string sep = " - ";
    // Reservamos el espacio del estado (a la derecha) y el separador
    // anterior; el resto es para nombre + ruta. Si no cabe ni el separador
    // entero (partsBudget negativo), se muestra solo el estado.
    int partsBudget = budget - estadoW - static_cast<int>(sep.size());
    if (budget <= estadoW || partsBudget < 0)
        return {"", "", utf8::truncate(estado, budget), true};

    int nameW = colCount(name);
    if (nameW >= partsBudget) {
        // El nombre consume el presupuesto entero: se trunca, sin ruta.
        name = utf8::truncate(name, partsBudget);
        return {name, "", estado, false};
    }

    // El nombre cabe; la ruta toma lo que sobra (con su separador). Si no
    // queda sitio, se omite la ruta (solo nombre + estado).
    int pathBudget = partsBudget - nameW - static_cast<int>(sep.size());
    if (path.empty() || pathBudget <= 0) return {name, "", estado, false};
    return {name, utf8TruncateFront(path, pathBudget), estado, false};
}

} // namespace

std::string StatusBar::render(const Rect& area, const StatusBarData& data) {
    const int width = area.width;
    const Theme& T = theme_;
    std::ostringstream out;

    // Fila fija (barra de estado): fondo gris 60%. El contenido es
    // "  BLANCO[nombre] NEGRO[ - ruta] DORADO[ - comando] relleno  {pct}%".
    out << "\x1b[K";
    out << T.statusBar; // base: negro sobre gris 60%

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
    // dentro de su presupuesto, ver buildBarLeft). Con esto se garantiza
    // que la fila fija ocupe EXACTAMENTE `width` columnas (nada mas).
    const int padL = std::min(kStatusBarPadLeft, width);
    const int padR = std::min(kStatusBarPadRight, std::max(0, width - padL));
    const int rightBudget = std::max(0, width - padL - padR);
    if (rightW > rightBudget) {
        rightBlock = utf8::truncate(rightBlock, rightBudget);
        rightW = colCount(rightBlock);
    }

    int leftBudget = std::max(0, width - padL - padR - rightW);
    BarLeft left = buildBarLeft(data.name, data.path, data.estado,
                                data.modified, leftBudget);

    // Ancho VISIBLE (sin ANSI) de todo a la izquierda del relleno, para que
    // el relleno consiga exactamente `width` columnas y el bloque derecho
    // (fila,columnapct%) quede anclado a la derecha.
    const std::string sep = " - ";
    int plainW;
    if (left.onlyEstado) {
        plainW = colCount(left.estado);
    } else {
        // "nombre[ - ruta] - estado": un separador si no hay ruta, dos si la
        // hay, y el texto de nombre + ruta + estado.
        int sepCount = left.path.empty() ? 1 : 2;
        plainW = colCount(left.name) + colCount(left.path) +
                 colCount(left.estado) +
                 sepCount * static_cast<int>(sep.size());
    }

    for (int i = 0; i < padL; ++i) out << ' ';

    // Accent de la etiqueta de estado: el EstadoData puede traer el color
    // propio del estado activo (v1.3); si no, se usa el fallback del Theme.
    const std::string accent = data.estadoAccent.empty() ? T.statusBarAccent
                                                         : data.estadoAccent;

    // El sufijo [modificado] se identifica y pinta por separado (v1.3): el
    // nombre va en statusBarName y el indicador en statusBarModified. Se
    // conservan exactamente las columnas de left.name (nameText + marker),
    // asi el calculo de ancho/relleno sigue siendo correcto.
    const std::string modMarker = " [modificado]";
    std::string nameText = left.name;
    bool hasMod = data.modified &&
                  nameText.size() >= modMarker.size() &&
                  nameText.compare(nameText.size() - modMarker.size(),
                                   modMarker.size(), modMarker) == 0;
    if (hasMod) nameText = nameText.substr(0, nameText.size() - modMarker.size());

    if (left.onlyEstado) {
        out << accent << left.estado << T.reset << T.statusBar;
    } else {
        out << T.statusBarName << nameText << T.reset << T.statusBar;
        if (hasMod) out << T.statusBarModified << modMarker << T.reset << T.statusBar;
        if (!left.path.empty()) {
            out << T.statusBarPath << sep << left.path << T.reset << T.statusBar;
        }
        out << accent << sep << left.estado << T.reset << T.statusBar;
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
        const std::string style = messageStyle(T, data.message.kind);
        out << style;
        out << utf8::truncate(data.message.text,
                              std::max(0, width - msgPadL - msgPadR));
        if (!style.empty()) out << T.reset;
        for (int i = 0; i < msgPadR; ++i) out << ' ';
    }

    return out.str();
}