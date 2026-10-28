#include "test_framework.h"

#include "platform/gui/GuiWindowGeometry.h"

// Tests para la lógica de hover/resize usando las FUNCIONES REALES de producción
// (no duplicar lógica - verificar la implementación)

TEST(HoverResize_edge_to_cursor_mapping) {
    // Usa la función REAL de producción: cursorTypeForEdge() (en GuiWindowGeometry.h)

    // Esquinas
    CHECK(cursorTypeForEdge(GuiWindowEdge::TopLeft) == ResizeCursorType::SizeNWSE);
    CHECK(cursorTypeForEdge(GuiWindowEdge::TopRight) == ResizeCursorType::SizeNESW);
    CHECK(cursorTypeForEdge(GuiWindowEdge::BottomLeft) == ResizeCursorType::SizeNESW);
    CHECK(cursorTypeForEdge(GuiWindowEdge::BottomRight) == ResizeCursorType::SizeNWSE);

    // Lados
    CHECK(cursorTypeForEdge(GuiWindowEdge::Left) == ResizeCursorType::SizeWE);
    CHECK(cursorTypeForEdge(GuiWindowEdge::Right) == ResizeCursorType::SizeWE);
    CHECK(cursorTypeForEdge(GuiWindowEdge::Top) == ResizeCursorType::SizeNS);
    CHECK(cursorTypeForEdge(GuiWindowEdge::Bottom) == ResizeCursorType::SizeNS);

    // Centro / ninguno
    CHECK(cursorTypeForEdge(GuiWindowEdge::None) == ResizeCursorType::Arrow);
}

TEST(HoverResize_guiWindowEdgeAt_esquinas) {
    const int w = 400, h = 300;
    const int border = 8;  // kResizeBorderPx

    // Esquinas exactas (0-based, dentro del borde)
    CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
    CHECK(guiWindowEdgeAt(w - 1, 0, w, h, border) == GuiWindowEdge::TopRight);
    CHECK(guiWindowEdgeAt(0, h - 1, w, h, border) == GuiWindowEdge::BottomLeft);
    CHECK(guiWindowEdgeAt(w - 1, h - 1, w, h, border) == GuiWindowEdge::BottomRight);

    // Esquinas: un pixel dentro del borde
    CHECK(guiWindowEdgeAt(border - 1, border - 1, w, h, border) == GuiWindowEdge::TopLeft);
    CHECK(guiWindowEdgeAt(w - border, border - 1, w, h, border) == GuiWindowEdge::TopRight);
    CHECK(guiWindowEdgeAt(border - 1, h - border, w, h, border) == GuiWindowEdge::BottomLeft);
    CHECK(guiWindowEdgeAt(w - border, h - border, w, h, border) == GuiWindowEdge::BottomRight);
}

TEST(HoverResize_guiWindowEdgeAt_lados) {
    const int w = 400, h = 300;
    const int border = 8;

    // Lados verticales (excluyendo esquinas)
    CHECK(guiWindowEdgeAt(0, h / 2, w, h, border) == GuiWindowEdge::Left);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border) == GuiWindowEdge::Right);
    CHECK(guiWindowEdgeAt(border - 1, h / 2, w, h, border) == GuiWindowEdge::Left);
    CHECK(guiWindowEdgeAt(w - border, h / 2, w, h, border) == GuiWindowEdge::Right);

    // Lados horizontales (excluyendo esquinas)
    CHECK(guiWindowEdgeAt(w / 2, 0, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, h - 1, w, h, border) == GuiWindowEdge::Bottom);
    CHECK(guiWindowEdgeAt(w / 2, border - 1, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, h - border, w, h, border) == GuiWindowEdge::Bottom);
}

TEST(HoverResize_guiWindowEdgeAt_centro_y_fuera) {
    const int w = 400, h = 300;
    const int border = 8;

    // Centro de la ventana -> None
    CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) == GuiWindowEdge::None);

    // Justo fuera del borde interior -> None
    CHECK(guiWindowEdgeAt(border, border, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w - border - 1, border, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(border, h - border - 1, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w - border - 1, h - border - 1, w, h, border) == GuiWindowEdge::None);

    // Fuera de la ventana
    CHECK(guiWindowEdgeAt(-1, 0, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, -1, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w, 0, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, h, w, h, border) == GuiWindowEdge::None);
}

TEST(HoverResize_guiWindowEdgeAt_degenerado) {
    // Ventana degenerada -> None
    CHECK(guiWindowEdgeAt(0, 0, 0, 300, 8) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, 0, 400, 0, 8) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, 0, 400, 300, 0) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(0, 0, 400, 300, -1) == GuiWindowEdge::None);
}

TEST(HoverResize_barra_titulo_excluida) {
    // guiWindowEdgeAt puro NO conoce la barra de título.
    // La exclusión del borde Top dentro de la barra se hace en hoverCursorType()
    // y en el hit-test nativo.

    const int w = 400, h = 300;
    const int border = 8;
    const int titleH = 28;

    // guiWindowEdgeAt puro: y < border (8) -> Top
    CHECK(guiWindowEdgeAt(w / 2, 0, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, 5, w, h, border) == GuiWindowEdge::Top);
    CHECK(guiWindowEdgeAt(w / 2, 7, w, h, border) == GuiWindowEdge::Top);

    // y >= border -> None (aunque esté en la barra de título)
    CHECK(guiWindowEdgeAt(w / 2, 8, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w / 2, titleH - 1, w, h, border) == GuiWindowEdge::None);

    // Justo debajo de la barra de título -> None
    CHECK(guiWindowEdgeAt(w / 2, titleH, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(w / 2, titleH + border - 1, w, h, border) == GuiWindowEdge::None);
}

TEST(HoverResize_scrollbar_excluida) {
    const int w = 400, h = 300;
    const int border = 8;
    const int trackWidth = 8;

    int trackX = w - border - trackWidth;  // 400 - 8 - 8 = 384
    int resizeBorderStartX = w - border;    // 400 - 8 = 392

    // Track del scrollbar: [384, 392)
    // Borde de resize: [392, 400)
    // No hay solape

    // Dentro del track -> None (no es borde de ventana)
    CHECK(guiWindowEdgeAt(trackX + 2, h / 2, w, h, border) == GuiWindowEdge::None);
    CHECK(guiWindowEdgeAt(trackX + trackWidth - 1, h / 2, w, h, border) == GuiWindowEdge::None);

    // En el borde de resize -> Right
    CHECK(guiWindowEdgeAt(resizeBorderStartX, h / 2, w, h, border) == GuiWindowEdge::Right);
    CHECK(guiWindowEdgeAt(w - 1, h / 2, w, h, border) == GuiWindowEdge::Right);
}

TEST(HoverResize_HiDPI_coordenadas_en_puntos) {
    const int border = 8;

    // Ventana pequeña
    {
        const int w = 200, h = 150;
        CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
        CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) == GuiWindowEdge::None);
    }

    // Ventana grande
    {
        const int w = 1920, h = 1080;
        CHECK(guiWindowEdgeAt(0, 0, w, h, border) == GuiWindowEdge::TopLeft);
        CHECK(guiWindowEdgeAt(w / 2, h / 2, w, h, border) == GuiWindowEdge::None);
    }
}

TEST(HoverResize_limites_y_transiciones_criticas) {
    // Verifica límites y transiciones CRÍTICAS que detectan regresiones reales.
    // Estos checks cubren las fronteras entre zonas (resize vs arrastre vs contenido).

    const int w = 400, h = 300;
    const int border = 8;
    const int titleH = 28;

    auto ht = [&](int x, int y) {
        return hoverCursorType(x, y, w, h, border);
    };

    // --- Esquinas de la ventana ---
    CHECK(ht(0, 0) == ResizeCursorType::SizeNWSE);       // TopLeft
    CHECK(ht(w - 1, 0) == ResizeCursorType::SizeNESW);    // TopRight
    CHECK(ht(0, h - 1) == ResizeCursorType::SizeNESW);    // BottomLeft
    CHECK(ht(w - 1, h - 1) == ResizeCursorType::SizeNWSE); // BottomRight

    // --- Borde superior (solo en y < border) ---
    CHECK(ht(w / 2, 0) == ResizeCursorType::SizeNS);
    CHECK(ht(w / 2, border - 1) == ResizeCursorType::SizeNS);
    // y = border ya NO es borde superior -> Arrow (zona arrastrable)
    CHECK(ht(w / 2, border) == ResizeCursorType::Arrow);

    // --- Barras laterales (resize en TODA la altura, incluyendo barra de título) ---
    // Izquierda
    CHECK(ht(0, 0) == ResizeCursorType::SizeNWSE);        // esquina
    CHECK(ht(0, border) == ResizeCursorType::SizeWE);     // EN barra
    CHECK(ht(0, titleH - 1) == ResizeCursorType::SizeWE); // EN barra
    CHECK(ht(0, titleH) == ResizeCursorType::SizeWE);     // fuera de barra
    CHECK(ht(0, h / 2) == ResizeCursorType::SizeWE);      // centro ventana
    CHECK(ht(0, h - 1) == ResizeCursorType::SizeNESW);    // esquina inf

    // Derecha
    CHECK(ht(w - 1, 0) == ResizeCursorType::SizeNESW);    // esquina
    CHECK(ht(w - 1, border) == ResizeCursorType::SizeWE); // EN barra
    CHECK(ht(w - 1, titleH - 1) == ResizeCursorType::SizeWE); // EN barra
    CHECK(ht(w - 1, titleH) == ResizeCursorType::SizeWE);     // fuera de barra
    CHECK(ht(w - 1, h / 2) == ResizeCursorType::SizeWE);      // centro ventana
    CHECK(ht(w - 1, h - 1) == ResizeCursorType::SizeNWSE);    // esquina inf

    // --- Zona arrastrable (centro barra de título) ---
    CHECK(ht(w / 2, border) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH / 2) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH - 1) == ResizeCursorType::Arrow);

    // --- Justo debajo de la barra de título -> contenido (Arrow) ---
    CHECK(ht(w / 2, titleH) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH + border - 1) == ResizeCursorType::Arrow);

    // --- Centro de la ventana (contenido) ---
    CHECK(ht(w / 2, h / 2) == ResizeCursorType::Arrow);

    // --- Borde inferior (y >= h - border) ---
    CHECK(ht(w / 2, h - 1) == ResizeCursorType::SizeNS);
    CHECK(ht(w / 2, h - border) == ResizeCursorType::SizeNS);
    CHECK(ht(w / 2, h - border + 1) == ResizeCursorType::SizeNS);
    // y = h - border - 1 está en contenido (NO en borde)
    CHECK(ht(w / 2, h - border - 1) == ResizeCursorType::Arrow);

    // --- Transiciones horizontales en la barra de título (y = border) ---
    // Izquierda: x=0 -> SizeWE, x=border-1 -> SizeWE, x=border -> Arrow, x=w-border-1 -> Arrow, x=w-1 -> SizeWE
    CHECK(ht(1, border) == ResizeCursorType::SizeWE);  // en borde izquierdo
    CHECK(ht(border - 1, border) == ResizeCursorType::SizeWE); // en borde izquierdo
    CHECK(ht(border, border) == ResizeCursorType::Arrow); // inicio zona arrastrable
    CHECK(ht(w - border - 1, border) == ResizeCursorType::Arrow); // fin zona arrastrable
    CHECK(ht(w - border, border) == ResizeCursorType::SizeWE); // en borde derecho
    CHECK(ht(w - 2, border) == ResizeCursorType::SizeWE); // en borde derecho

    // --- Transiciones verticales en el borde izquierdo (x = 0) ---
    // y = border-1 -> SizeNWSE (esquina), y = border -> SizeWE, y = titleH-1 -> SizeWE, y = titleH -> SizeWE
    CHECK(ht(0, border - 1) == ResizeCursorType::SizeNWSE);
    CHECK(ht(0, border) == ResizeCursorType::SizeWE);
    CHECK(ht(0, titleH - 1) == ResizeCursorType::SizeWE);
    CHECK(ht(0, titleH) == ResizeCursorType::SizeWE);
}

TEST(HoverResize_barra_titulo_hover_cursor) {
    // Test específico de reglas de la barra de título (sin duplicar límites generales).
    // Verifica que la barra de título prioriza botones/arrastre sobre resize
    // PERO permite resize en bordes Left/Right y en Top (solo y < border).

    const int w = 400, h = 300;
    const int border = 8;
    const int titleH = 28;

    auto ht = [&](int x, int y) {
        return hoverCursorType(x, y, w, h, border);
    };

    // En la barra de título, PERO en el borde superior (y < border = 8)
    // Las esquinas deben dar cursor de resize
    CHECK(ht(0, 0) == ResizeCursorType::SizeNWSE);     // TopLeft
    CHECK(ht(w - 1, 0) == ResizeCursorType::SizeNESW);   // TopRight
    CHECK(ht(border - 1, border - 1) == ResizeCursorType::SizeNWSE);
    CHECK(ht(w - border, border - 1) == ResizeCursorType::SizeNESW);

    // En el borde superior pero NO en esquinas (y < border, x en medio)
    CHECK(ht(w / 2, 0) == ResizeCursorType::SizeNS);
    CHECK(ht(w / 2, border - 1) == ResizeCursorType::SizeNS);

    // En la barra de título, DEBAJO del borde superior (y >= border)
    // Centro -> arrastrable (Arrow)
    CHECK(ht(w / 2, border) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH - 1) == ResizeCursorType::Arrow);

    // PERO bordes Left/Right -> resize (SizeWE) incluso en la barra de título
    CHECK(ht(0, border) == ResizeCursorType::SizeWE);
    CHECK(ht(w - 1, border) == ResizeCursorType::SizeWE);
    // Esquinas de la barra en y=titleH-1 (BottomLeft/BottomRight relativos a la barra)
    CHECK(ht(0, titleH - 1) == ResizeCursorType::SizeWE);
    CHECK(ht(w - 1, titleH - 1) == ResizeCursorType::SizeWE);

    // Justo debajo de la barra de título -> Arrow (None mapea a Arrow)
    CHECK(ht(w / 2, titleH) == ResizeCursorType::Arrow);
    CHECK(ht(w / 2, titleH + border - 1) == ResizeCursorType::Arrow);

    // Bordes laterales e inferiores (fuera de la barra) - checks mínimos
    CHECK(ht(0, h / 2) == ResizeCursorType::SizeWE);
    CHECK(ht(w - 1, h / 2) == ResizeCursorType::SizeWE);
    CHECK(ht(w / 2, h - 1) == ResizeCursorType::SizeNS);
    CHECK(ht(0, h - 1) == ResizeCursorType::SizeNESW);
    CHECK(ht(w - 1, h - 1) == ResizeCursorType::SizeNWSE);

    // Centro
    CHECK(ht(w / 2, h / 2) == ResizeCursorType::Arrow);
}