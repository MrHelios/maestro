#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include "base/utf8.h"
#include "rendering/RenderUtil.h"
#include "rendering/StatusBarData.h"

// ---------------------------------------------------------------------------
// ChromeLayout: política COMPARTIDA del StatusBar (zona pura, sin ANSI/RGB).
//
// Una sola implementación de la composición del bloque izquierdo
// (nombre/ruta/estado + [*]) y del bloque derecho (override o % + (fila,col))
// para los dos backends:
//
//   ChromeLayout (layout: fragmentos + presupuestos)
//       ├── TtyChrome  (presentación: fragmentos -> ANSI)
//       └── GuiChrome  (presentación: línea plana -> RGB)
//
// REGLA: ningún backend duplica topes ni presupuestos. Si cambia la política
// (topes, orden de sacrificio, pads), se cambia acá y ambos backends la
// heredan. El drift TTY/GUI queda bloqueado por el test de paridad
// (stripAnsi(TtyChrome) == guichrome::statusLine).
//
// No incluye nada de app/ (ver docs/architecture.md §6): solo tipos puros.
// ---------------------------------------------------------------------------

namespace chrome {

// ---- Topes fijos del StatusBar (bloque izquierdo) ----
inline constexpr int kStatusNameMax = 30;      // columnas máximas del nombre
inline constexpr int kStatusPathMax = 40;      // columnas máximas de la ruta
inline constexpr int kStatusNamePathMax = 60;  // tope combinado nombre + ruta
inline constexpr std::string_view kStatusSeparator = " - ";
inline constexpr std::string_view kStatusModifiedMarker = " [*]";

// Fragmentos ya presupuestados del bloque izquierdo. `name`/`status`
// separados para que cada backend aplique su estilo (ANSI en TTY, RGB en
// GUI); el orden de emisión es siempre:
//   name [+marker si showMarker] [+sep+path] +sep+status
// o solo `status` si statusOnly.
struct StatusLeft {
    std::string name;
    std::string path;
    std::string status;
    bool modified = false;
    bool showMarker = false;
    bool statusOnly = false;
};

// Construye el bloque izquierdo respetando los límites de nombre/ruta y el
// presupuesto disponible. La ruta se sacrifica antes que el nombre; si
// tampoco cabe el nombre, solo conserva el estado.
//
// CONTRATO DE ANCHO: garantiza que el bloque que el backend construya a
// partir de StatusLeft no supera `budget` columnas visibles.
inline StatusLeft layoutStatusLeft(const std::string& rawName,
                                   const std::string& rawPath,
                                   const std::string& status, bool modified,
                                   int budget) {
    if (budget <= 0) return {"", "", utf8::truncate(status, 0), false, false, true};

    std::string name = rawName;
    if (name.empty()) name = "[sin nombre]";

    std::string path = rawPath;

    int markerW = modified ? colCount(kStatusModifiedMarker) : 0;
    int nameBudget = kStatusNameMax - markerW;
    name = utf8::truncate(name, nameBudget);
    if (colCount(path) > kStatusPathMax)
        path = utf8TruncateFront(path, kStatusPathMax);
    if (colCount(name) + markerW + colCount(path) > kStatusNamePathMax)
        path = utf8TruncateFront(
            path, std::max(0, kStatusNamePathMax - colCount(name) - markerW));

    int statusW = colCount(status);
    int sepW = colCount(kStatusSeparator);
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
    if (path.empty() || pathBudget <= 0)
        return {name, "", status, modified, modified, false};
    return {name, utf8TruncateFront(path, pathBudget), status, modified,
            modified, false};
}

// Bloque derecho del StatusBar: si hay un `right` explícito (pantallas sin
// documento: selector, explorador) se usa tal cual; si no, posición vertical
// del cursor como porcentaje (0% inicio, 100% final; una línea => 0%) y
// luego (fila,columna), anclado a la derecha.
inline std::string statusRightBlock(const StatusBarData& st) {
    if (!st.right.empty()) return st.right;
    const int pct = st.totalLines <= 1
                        ? 0
                        : (st.cursorLine * 100) / (st.totalLines - 1);
    return std::to_string(pct) + "% (" + std::to_string(st.cursorLine + 1) +
           "," + std::to_string(st.cursorCol + 1) + ")";
}

// Texto plano del bloque izquierdo en orden de emisión (sin pads ni fill).
// Es lo que TTY emite con ANSI entre fragmentos y lo que la GUI pinta como
// textura: la igualdad está congelada por el test de paridad.
inline std::string statusLeftPlainText(const StatusLeft& left) {
    if (left.statusOnly) return left.status;
    std::string out = left.name;
    if (left.showMarker) out.append(kStatusModifiedMarker.data(),
                                    kStatusModifiedMarker.size());
    if (!left.path.empty()) {
        out.append(kStatusSeparator.data(), kStatusSeparator.size());
        out += left.path;
    }
    out.append(kStatusSeparator.data(), kStatusSeparator.size());
    out += left.status;
    return out;
}

}  // namespace chrome
