#include "platform/tty/TtyKeymap.h"
#include "platform/InputEvent.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// Paso: traduccion input -> Evento externalizada en un TtyKeymap remapeable.
//
// Terminal solo lee/ensambla bytes; el SIGNIFICADO de cada tecla vive en
// TtyKeymap como datos. Estas pruebas verifican los enlaces por defecto y que
// el TtyKeymap acepte rebindearse (reconfiguracion en tiempo de ejecucion) sin
// que el Editor tenga que cambiar. Tambien cubren el fallback de
// modificadores: "[1;2A" (flecha arriba con modificador) se resuelve igual
// que "[A".
// ---------------------------------------------------------------------------

namespace {

// Comprueba que el enlace de un byte de control produzca el Evento
// esperado, y que un byte sin enlazar no produzca nada.
void checkControl(unsigned char byte, InputEventType expected) {
    TtyKeymap km;
    const auto t = km.control(byte);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(expected));
}

// Idem para secuencias de escape (contenido tras el ESC).
void checkSequence(const std::string& contents, InputEventType expected) {
    TtyKeymap km;
    const auto t = km.sequence(contents);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(expected));
}

} // namespace

// --- Enlaces por defecto: teclas de control de un byte ---
TEST(keymap_ctrl_q)   { checkControl(17, InputEventType::Quit); }
TEST(keymap_ctrl_s)   { checkControl(19, InputEventType::Save); }
TEST(keymap_ctrl_k)   { checkControl(11, InputEventType::Prefix); }
TEST(keymap_ctrl_u)   { checkControl(21, InputEventType::Undo); }
TEST(keymap_ctrl_y)   { checkControl(25, InputEventType::Redo); }
TEST(keymap_ctrl_z)   { checkControl(26, InputEventType::Suspend); }
TEST(keymap_backspace){ checkControl(127, InputEventType::Backspace); }
TEST(keymap_backspace_bs){ checkControl(8, InputEventType::Backspace); }
TEST(keymap_enter)    { checkControl(13, InputEventType::InsertNewline); }
TEST(keymap_enter_lf) { checkControl(10, InputEventType::InsertNewline); }

// --- Enlaces por defecto: secuencias de escape ---
TEST(keymap_seq_arrows) {
    checkSequence("[A", InputEventType::MoveUp);
    checkSequence("[B", InputEventType::MoveDown);
    checkSequence("[C", InputEventType::MoveRight);
    checkSequence("[D", InputEventType::MoveLeft);
}
TEST(keymap_seq_ss3) {
    checkSequence("OA", InputEventType::MoveUp);
    checkSequence("OB", InputEventType::MoveDown);
    checkSequence("OC", InputEventType::MoveRight);
    checkSequence("OD", InputEventType::MoveLeft);
    checkSequence("OH", InputEventType::MoveEnd);
    checkSequence("OF", InputEventType::MoveHome);
}
TEST(keymap_seq_home_end) {
    checkSequence("[H", InputEventType::MoveHome);
    checkSequence("[F", InputEventType::MoveEnd);
    checkSequence("[1~", InputEventType::MoveHome);
    checkSequence("[7~", InputEventType::MoveHome);
    checkSequence("[4~", InputEventType::MoveEnd);
    checkSequence("[8~", InputEventType::MoveEnd);
}
TEST(keymap_seq_delete_pages) {
    checkSequence("[3~", InputEventType::Delete);
    checkSequence("[5~", InputEventType::PageUp);
    checkSequence("[6~", InputEventType::PageDown);
}

TEST(keymap_seq_arrow_with_modifier_falls_back) {
    // Los modificadores de las secuencias de escape se ignoran (ver
    // Terminal.cpp: simpleEscapeForm): una flecha con Shift/Alt/Ctrl
    // (formato xterm "[1;2A") debe resolver igual que la flecha sin modificador.
    checkSequence("[1;2A", InputEventType::MoveUp);
    checkSequence("[1;5D", InputEventType::MoveLeft);
    checkSequence("[1;2B", InputEventType::MoveDown);
    checkSequence("[1;3C", InputEventType::MoveRight);
    checkSequence("[1;2H", InputEventType::MoveHome);
    checkSequence("[1;6F", InputEventType::MoveEnd);
}

// --- Rebindeo en tiempo de ejecucion ---
TEST(keymap_remap_control) {
    TtyKeymap km;
    km.bindControl(18, InputEventType::PageDown); // Ctrl+R -> AvPag (reconfigurado)
    const auto t = km.control(18);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(InputEventType::PageDown));
}

TEST(keymap_remap_sequence) {
    TtyKeymap km;
    // El Editor no depende de las teclas fisicas: le da igual que MoveLeft
    // venga de la flecha o de otra secuencia remapeada. La clave es la que
    // realmente pasa Terminal (con '[' y modificador), no "1;2D" pelado.
    km.bindSequence("[1;2D", InputEventType::PageDown);
    CHECK_EQ(static_cast<int>(*km.sequence("[1;2D")),
             static_cast<int>(InputEventType::PageDown));
    // Sin binding explicito, el fallback de modificadores resuelve "[1;2D"
    // como "[D" (MoveLeft); con binding, gana el valor remapeado.
    TtyKeymap km2;
    CHECK_EQ(static_cast<int>(*km2.sequence("[1;2D")),
             static_cast<int>(InputEventType::MoveLeft));
}

TEST(keymap_unbound_control) {
    TtyKeymap km;
    CHECK(!km.control(9).has_value()); // Tab sin enlazar por defecto
}

TEST(keymap_unbound_sequence) {
    TtyKeymap km;
    CHECK(!km.sequence("[9~").has_value()); // secuencia desconocida
}

TEST(keymap_reset_defaults) {
    TtyKeymap km;
    km.bindControl(17, InputEventType::PageDown);    // romper Ctrl+Q
    km.bindSequence("[A", InputEventType::MoveRight); // romper flecha arriba
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.control(17)), static_cast<int>(InputEventType::Quit));
    CHECK_EQ(static_cast<int>(*km.sequence("[A")), static_cast<int>(InputEventType::MoveUp));
}
