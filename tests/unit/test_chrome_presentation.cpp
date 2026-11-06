#include <chrono>

#include "app/ChromePresentation.h"
#include "test_framework.h"

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

/*
*  El test crea un mensaje con expiracion de 30. 
*  Se verifica si no es persistente, el mensaje y el tipo.
*/
TEST(chrome_presentation_message_drops_expiry) {
    const auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    Message m("hola", MessageKind::Error, expiry);
    const MessageBarData bar = toMessageBar(m);
    
    CHECK(!m.persistent());
    CHECK(bar.text == "hola");
    CHECK(bar.kind == MessageKind::Error);
}

/*
*  El test verfica que el prefjo sea bold y el resto del mensaje no
*/
TEST(chrome_presentation_message_propagates_cursor_and_bold_prefix) {
    Message m(std::string("Find: ho"), MessageKind::Prompt, std::nullopt, 8, 6);
    const MessageBarData bar = toMessageBar(m);
    
    CHECK(bar.text == "Find: ho");
    CHECK(bar.kind == MessageKind::Prompt);
    CHECK(bar.cursor.has_value());
    CHECK_EQ(*bar.cursor, 8);
    CHECK(bar.boldPrefix.has_value());
    CHECK_EQ(*bar.boldPrefix, 6);

    const MessageBarData plain = toMessageBar(Message("hola"));
    CHECK(!plain.boldPrefix.has_value());
}

TEST(chrome_presentation_busqueda_all_fields) {
    Message m("nota", MessageKind::Prompt, std::nullopt);
    
    const ChromeRequest r = makeChromeRequest(m, State::Busqueda);
    CHECK(r.message.text == "nota");
    CHECK(r.message.kind == MessageKind::Prompt);
    CHECK(r.estado == "BUSQUEDA");
    CHECK(r.accent == StyleRole::AccentGuardar);
    CHECK(r.cursorShape == FrameCursorShape::Bar);
    CHECK(r.cursorVisibleByMode);
    CHECK(r.cursorInMessageBar);
}

TEST(chrome_presentation_interaction_all_fields) {
    const ChromeRequest r2 = makeChromeRequest(Message{}, State::Interaccion);
    CHECK(r2.message.text.empty());
    CHECK(r2.message.kind == MessageKind::Info);
    CHECK(r2.estado == "INTERACCION");
    CHECK(r2.accent == StyleRole::AccentInteraccion);
    CHECK(r2.cursorShape == FrameCursorShape::Bar);
    CHECK(r2.cursorVisibleByMode);
    CHECK(!r2.cursorInMessageBar);
}

TEST(chrome_presentation_navegacion_all_fields) {
    const ChromeRequest r = makeChromeRequest(Message{}, State::Navegacion);
    CHECK(r.message.text.empty());
    CHECK(r.message.kind == MessageKind::Info);
    CHECK(r.estado == "NAVEGACION");
    CHECK(r.accent == StyleRole::AccentNavegacion);
    CHECK(r.cursorShape == FrameCursorShape::Block);
    CHECK(r.cursorVisibleByMode);
    CHECK(!r.cursorInMessageBar);
}

TEST(chrome_presentation_seleccion_all_fields) {
    const ChromeRequest r = makeChromeRequest(Message{}, State::Seleccion);
    CHECK(r.message.text.empty());
    CHECK(r.message.kind == MessageKind::Info);
    CHECK(r.estado == "SELECCION");
    CHECK(r.accent == StyleRole::AccentSeleccion);
    CHECK(r.cursorShape == FrameCursorShape::Block);
    CHECK(r.cursorVisibleByMode);
    CHECK(!r.cursorInMessageBar);
}

TEST(chrome_presentation_comando_all_fields) {
    const ChromeRequest r = makeChromeRequest(Message{}, State::Prefix);
    CHECK(r.message.text.empty());
    CHECK(r.message.kind == MessageKind::Info);
    CHECK(r.estado == "COMANDO");
    CHECK(r.accent == StyleRole::AccentComando);
    CHECK(r.cursorShape == FrameCursorShape::Block);
    CHECK(r.cursorVisibleByMode);
    CHECK(!r.cursorInMessageBar);
}

TEST(chrome_presentation_buffers_all_fields) {
    const ChromeRequest r = makeChromeRequest(Message{}, State::BufferSelector);
    CHECK(r.message.text.empty());
    CHECK(r.message.kind == MessageKind::Info);
    CHECK(r.estado == "BUFFERS");
    CHECK(r.accent == StyleRole::AccentBuffers);
    CHECK(r.cursorShape == FrameCursorShape::Block);
    CHECK(r.cursorVisibleByMode);
    CHECK(!r.cursorInMessageBar);
}

TEST(chrome_presentation_abrir_all_fields) {
    const ChromeRequest r = makeChromeRequest(Message{}, State::FileBrowser);
    CHECK(r.message.text.empty());
    CHECK(r.message.kind == MessageKind::Info);
    CHECK(r.estado == "ABRIR");
    CHECK(r.accent == StyleRole::AccentAbrir);
    CHECK(r.cursorShape == FrameCursorShape::Block);
    CHECK(r.cursorVisibleByMode);
    CHECK(!r.cursorInMessageBar);
}

TEST(chrome_presentation_ir_a_fila_all_fields) {
    Message m("10", MessageKind::Prompt, std::nullopt);
    const ChromeRequest r = makeChromeRequest(m, State::IrAFila);
    CHECK(r.message.text == "10");
    CHECK(r.message.kind == MessageKind::Prompt);
    CHECK(r.estado == "IR A FILA");
    CHECK(r.accent == StyleRole::AccentNavegacion);
    CHECK(r.cursorShape == FrameCursorShape::Bar);
    CHECK(r.cursorVisibleByMode);
    CHECK(r.cursorInMessageBar);
}

TEST(chrome_presentation_guardar_como_all_fields) {
    Message m("archivo.txt", MessageKind::Prompt, std::nullopt);
    const ChromeRequest r = makeChromeRequest(m, State::SaveAsFileBrowser);
    CHECK(r.message.text == "archivo.txt");
    CHECK(r.message.kind == MessageKind::Prompt);
    CHECK(r.estado == "GUARDAR COMO");
    CHECK(r.accent == StyleRole::AccentGuardar);
    CHECK(r.cursorShape == FrameCursorShape::Bar);
    CHECK(r.cursorVisibleByMode);
    CHECK(r.cursorInMessageBar);
}

TEST(chrome_presentation_renombrar_all_fields) {
    Message m("nuevo_nombre.txt", MessageKind::Prompt, std::nullopt);
    const ChromeRequest r = makeChromeRequest(m, State::Renombrar);
    CHECK(r.message.text == "nuevo_nombre.txt");
    CHECK(r.message.kind == MessageKind::Prompt);
    CHECK(r.estado == "RENOMBRAR");
    CHECK(r.accent == StyleRole::AccentGuardar);
    CHECK(r.cursorShape == FrameCursorShape::Bar);
    CHECK(r.cursorVisibleByMode);
    CHECK(r.cursorInMessageBar);
}

TEST(chrome_presentation_state_count) {
    CHECK_EQ(getStateEditor().size(), size_t(10));
}
