#pragma once

// A — Frontier (3): Resize como dato, no como señal global.
//
// El resize viaja como EventType::Resize con su payload (filas/columnas)
// y el Editor lo aplica de forma autónoma en
// Editor::handleResize(rows, cols), sin consultar ningún backend.
// Vale para TTY (TtyRunLoop traduce SIGWINCH) y para la futura GUI,
// que puede inyectar el evento directamente.
struct ResizeEvent {
    int rows = 0;
    int cols = 0;
};
