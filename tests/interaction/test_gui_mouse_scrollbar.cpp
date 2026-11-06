// Integracion: mapeo de clic de mouse cerca del scrollbar.
// Verifica que un clic en una columna visible cercana al extremo derecho
// mapee correctamente a la posicion logica del documento, sin solaparse
// con el track del scrollbar.
// Los tests de geometria viven en tests/rendering/test_gui_scrollbar.cpp;
// este test cubre el camino Editor -> render -> mapeo de mouse.

#include "test_support.h"

#include "layout/Gutter.h"
#include "layout/Layout.h"
#include "platform/CellPos.h"
#include "platform/MouseEvent.h"
#include "rendering/Sink.h"
#include "rendering/gui/GuiRenderer.h"

namespace {

class NullGuiSink : public Sink {
public:
    bool writeStdout(const std::string&) override { return true; }
};

}  // namespace

// Carga un documento con lineas largas y suficiente scroll vertical para
// que el scrollbar esté habilitado, renderiza y devuelve una referencia al
// renderer para inspeccionar su estado.
void setupDocWithScrollbar(Editor& ed, GuiRenderer& gui, int vpW, int vpH) {
    ed.resize(vpH, vpW);

    ed.active().filename = "/tmp/test.txt";
    std::vector<std::string> lines;
    for (int i = 0; i < 30; ++i) lines.push_back(std::string(200, 'x'));
    ed.active().document.restore(lines);

    // Forzar scrollbar visible (como hace GuiRunLoop::syncScrollbarInfo).
    gui.setScrollInfo({0, vpH, 30});

    ed.renderFrame();
}

// Verifica que un clic en `clickCol` (columna de pantalla, 0-based) mapee
// correctamente a la posicion logica del documento.
void checkMouseClickMapsToDoc(Editor& ed, int clickCol, int clickRow,
                              int expectedLine, int expectedCol) {
    InputEvent e = makeMousePressEvent(CellPos{clickCol, clickRow});
    ed.handleEvent(e);

    CHECK_EQ(ed.active().cursor.line, expectedLine);
    CHECK_EQ(ed.active().cursor.col, expectedCol);
}

// Clic en columna dentro del area util: mapea correctamente.
TEST(gui_mouse_click_inside_useful_area) {
    auto gui = std::make_unique<GuiRenderer>();
    GuiRenderer& guiRef = *gui;
    Editor ed(std::move(gui));
    NullGuiSink sink;
    ed.setSink(sink);

    const int vpW = 80;
    const int vpH = 10;
    setupDocWithScrollbar(ed, guiRef, vpW, vpH);

    CHECK(guiRef.scrollbarEnabled());

    const Layout layout = guiRef.lastLayout();
    CHECK(layout.content.width < vpW);  // area reducida por el scrollbar

    const int gutter = gutterWidth(ed.active().document.lineCount(), vpW);
    const int clickCol = gutter;  // primera columna de texto (dentro del area util)

    // Clic en la primera columna de texto, linea 0 -> posicion (0, 0).
    checkMouseClickMapsToDoc(ed, clickCol, 0, 0, 0);
}

// Clic en columna cerca del extremo derecho (dentro del area util):
// mapea a la ultima columna de texto del area reducida.
TEST(gui_mouse_click_near_right_edge_inside_useful_area) {
    auto gui = std::make_unique<GuiRenderer>();
    GuiRenderer& guiRef = *gui;
    Editor ed(std::move(gui));
    NullGuiSink sink;
    ed.setSink(sink);

    const int vpW = 80;
    const int vpH = 10;
    setupDocWithScrollbar(ed, guiRef, vpW, vpH);

    CHECK(guiRef.scrollbarEnabled());

    const Layout layout = guiRef.lastLayout();
    CHECK(layout.content.width < vpW);

    const int gutter = gutterWidth(ed.active().document.lineCount(), vpW);
    // Ultima columna de pantalla dentro del area reducida (content.width
    // ya incluye el gutter, asi que content.width - 1 es la ultima columna
    // de pantalla del contenido pintado).
    const int lastUsefulCol = layout.content.width - 1;

    // Clic en la ultima columna util -> ultima columna de texto (linea 0).
    checkMouseClickMapsToDoc(ed, lastUsefulCol, 0, 0, lastUsefulCol - gutter);
}

// Clic en el gutter: mapea al inicio de la linea.
TEST(gui_mouse_click_in_gutter) {
    auto gui = std::make_unique<GuiRenderer>();
    GuiRenderer& guiRef = *gui;
    Editor ed(std::move(gui));
    NullGuiSink sink;
    ed.setSink(sink);

    const int vpW = 80;
    const int vpH = 10;
    setupDocWithScrollbar(ed, guiRef, vpW, vpH);

    // Clic en el gutter (columna 0) -> inicio de la linea (col 0).
    checkMouseClickMapsToDoc(ed, 0, 0, 0, 0);
}
