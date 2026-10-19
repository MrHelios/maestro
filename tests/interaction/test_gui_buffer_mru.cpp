// Integracion GUI del selector de buffers con la MRU compartida.
//
// No duplica la logica MRU: solo verifica que el estado compartido
// (BufferManager/Editor) y el backend GuiRenderer coinciden al abrir el
// selector y al activar con Enter.

#include "test_support.h"

#include "rendering/gui/GuiRenderer.h"

namespace {

class NullGuiSink : public Sink {
public:
    bool writeStdout(const std::string&) override { return true; }
};

// Abre el selector y baja hasta la posicion MRU `pos` antes de confirmar.
void activateAtMruPosition(Editor& ed, int pos) {
    openSelector(ed);
    CHECK_EQ(ed.bufferSelectorIndex_, 1);
    for (int i = 1; i < pos; ++i) press(ed, InputEventType::MoveDown);
    press(ed, InputEventType::InsertNewline);
}

}  // namespace

TEST(gui_buffer_selector_highlight_and_activation_follow_mru) {
    auto gui = std::make_unique<GuiRenderer>();
    GuiRenderer& guiRef = *gui;
    Editor ed(std::move(gui));
    NullGuiSink sink;
    ed.setSink(sink);

    // Creacion a, b, c, d: la MRU queda [d, c, b, a] con d activo.
    ed.active().filename = "/tmp/a.txt";
    newBuffer(ed);
    ed.active().filename = "/tmp/b.txt";
    newBuffer(ed);
    ed.active().filename = "/tmp/c.txt";
    newBuffer(ed);
    ed.active().filename = "/tmp/d.txt";

    // Historial de uso b, d, a -> MRU [a, d, b, c].
    activateAtMruPosition(ed, 2);  // [d, c, b, a] -> b
    activateAtMruPosition(ed, 1);  // [b, d, c, a] -> d
    activateAtMruPosition(ed, 3);  // [d, b, c, a] -> a
    CHECK_EQ(ed.active().displayName(), "a.txt");

    const std::vector<std::string> names = ed.bufferNames();
    CHECK_EQ(names.size(), 4u);
    CHECK_EQ(names[0], "a.txt");
    CHECK_EQ(names[1], "d.txt");
    CHECK_EQ(names[2], "b.txt");
    CHECK_EQ(names[3], "c.txt");

    // El selector GUI presenta la lista MRU y resalta al anterior (d).
    openSelector(ed);
    CHECK_EQ(ed.bufferSelectorIndex_, 1);
    ed.renderFrame();

    const auto& lines = guiRef.lastListLines();
    CHECK_EQ(lines[0].text, "  a.txt");
    CHECK_EQ(lines[1].text, "  d.txt");
    CHECK_EQ(lines[2].text, "  b.txt");
    CHECK_EQ(lines[3].text, "  c.txt");
    CHECK_EQ(guiRef.lastListSelected(), ed.bufferSelectorIndex_);
    CHECK_EQ(guiRef.lastListSelected(), 1);

    // Enter activa el buffer resaltado por la GUI (d), no otro.
    press(ed, InputEventType::InsertNewline);
    CHECK_EQ(ed.active().displayName(), "d.txt");
}
