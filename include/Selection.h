#pragma once

#include <optional>

#include "Position.h"

// La seleccion de texto ES del Editor: ni Document ni Cursor deben
// conocerla. Es un struct de datos puro: el anchor (fijo mientras se
// usa Shift) y la position del cursor (que cambia).
//
//   - anchor   : donde comenzo la seleccion (no cambia con Shift).
//   - position : posicion actual del cursor (extremo movil).
//
// Si anchor == position, no hay texto seleccionado.
struct Selection {
    Position anchor;
    Position position;
};

// Conveniencia: normalizacion. Devuelve los extremos en orden
// canonico (start <= end) para saber que texto esta seleccionado.
struct Normalized {
    Position start;
    Position end;
};

inline std::optional<Normalized> normalize(const Selection& sel) {
    const Position& a = sel.anchor;
    const Position& p = sel.position;
    if (a == p) return std::nullopt; // no hay nada seleccionado

    if (a.line < p.line || (a.line == p.line && a.col <= p.col)) {
        return Normalized{a, p};
    }
    return Normalized{p, a};
}