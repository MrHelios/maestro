#include <chrono>

#include "app/ChromePresentation.h"
#include "test_framework.h"

// Cobertura directa del punto común de traducción State -> presentación
// (app/ChromePresentation.h). Congela las reglas que el rendering ya no
// puede ver (recibe ChromeRequest resuelto):
//   - etiqueta de la StatusBar por modo,
//   - acento por modo (Busqueda comparte GUARDAR, IrAFila comparte
//     NAVEGACION, SaveAs comparte GUARDAR),
//   - forma del cursor (Interacción + 4 prompts con input parpadean),
//   - visibilidad por modo (siempre visible; los prompts van al MessageBar),
//   - cursor en MessageBar (Busqueda/IrAFila/SaveAs/Renombrar),
//   - Message -> MessageBarData suelta el `expiry` temporal.

namespace {

struct Expected {
    State state;
    const char* label;
    StyleRole accent;
    FrameCursorShape shape;
    bool visible;
    bool inMessageBar;
};

} // namespace

TEST(chrome_presentation_state_mapping) {
    const Expected cases[] = {
        {State::Navegacion, "NAVEGACION", StyleRole::AccentNavegacion,
         FrameCursorShape::Block, true, false},
        {State::Interaccion, "INTERACCION", StyleRole::AccentInteraccion,
         FrameCursorShape::Bar, true, false},
        {State::Seleccion, "SELECCION", StyleRole::AccentSeleccion,
         FrameCursorShape::Block, true, false},
        {State::Prefix, "COMANDO", StyleRole::AccentComando,
         FrameCursorShape::Block, true, false},
        {State::BufferSelector, "BUFFERS", StyleRole::AccentBuffers,
         FrameCursorShape::Block, true, false},
        {State::FileBrowser, "ABRIR", StyleRole::AccentAbrir,
         FrameCursorShape::Block, true, false},
        {State::Busqueda, "BUSQUEDA", StyleRole::AccentGuardar,
         FrameCursorShape::Bar, true, true},
        {State::IrAFila, "IR A FILA", StyleRole::AccentNavegacion,
         FrameCursorShape::Bar, true, true},
        {State::SaveAsFileBrowser, "GUARDAR COMO", StyleRole::AccentGuardar,
         FrameCursorShape::Bar, true, true},
        {State::Renombrar, "RENOMBRAR", StyleRole::AccentGuardar,
         FrameCursorShape::Bar, true, true},
    };
    for (const auto& c : cases) {
        CHECK(stateLabelForPresentation(c.state) == c.label);
        CHECK(accentRoleFor(c.state) == c.accent);
        CHECK(cursorShapeFor(c.state) == c.shape);
        CHECK(cursorVisibleForMode(c.state) == c.visible);
        CHECK(cursorInMessageBarFor(c.state) == c.inMessageBar);
    }
}

TEST(chrome_presentation_message_drops_expiry) {
    const auto expiry =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    Message m("hola", MessageKind::Error, expiry);
    CHECK(!m.persistent());

    const MessageBarData bar = toMessageBar(m);
    CHECK(bar.text == "hola");
    CHECK(bar.kind == MessageKind::Error);
}

TEST(chrome_presentation_request_assembles_all_fields) {
    Message m("nota", MessageKind::Prompt, std::nullopt);
    const ChromeRequest r = makeChromeRequest(m, State::Busqueda);
    CHECK(r.message.text == "nota");
    CHECK(r.message.kind == MessageKind::Prompt);
    CHECK(r.estado == "BUSQUEDA");
    CHECK(r.accent == StyleRole::AccentGuardar);
    CHECK(r.cursorShape == FrameCursorShape::Bar);
    CHECK(r.cursorVisibleByMode);
    CHECK(r.cursorInMessageBar);

    const ChromeRequest r2 = makeChromeRequest(Message{}, State::Interaccion);
    CHECK(r2.message.text.empty());
    CHECK(r2.message.kind == MessageKind::Info);
    CHECK(r2.estado == "INTERACCION");
    CHECK(r2.accent == StyleRole::AccentInteraccion);
    CHECK(r2.cursorShape == FrameCursorShape::Bar);
    CHECK(r2.cursorVisibleByMode);
    CHECK(!r2.cursorInMessageBar);
}
