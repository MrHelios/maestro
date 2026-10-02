#pragma once

#include <memory>

#include "rendering/ScreenRenderer.h"
#include "rendering/tty/TtyRenderer.h"

// ---------------------------------------------------------------------------
// Helper canónico para tests que necesitan el backend real de pantalla.
//
// El default del Editor es Null neutro; los tests que afirman bytes ANSI
// (renderFrame con clear-screen, diff/invalidate, temas) inyectan el TTY
// explícito. Los tests de lógica no lo necesitan.
//
// Uso en tests: `Editor ed(makeTtyTestRenderer());` o, si el Editor ya
// existe, `ed.setRenderer(makeTtyTestRenderer());`.
//
// Vive acá (helpers/, sin Editor.h ni hack de private) para que cualquier
// suite lo incluya: test_support.h lo reexpone, y los .cpp que no pueden
// incluir test_support.h (choque de helpers locales) lo incluyen directo.
// ---------------------------------------------------------------------------
inline std::unique_ptr<ScreenRenderer> makeTtyTestRenderer() {
    return std::make_unique<TtyRenderer>();
}
