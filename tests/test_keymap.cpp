#include "terminal/Keymap.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// Paso: traduccion input -> Evento externalizada en un Keymap remapeable.
//
// Terminal solo lee/ensambla bytes; el SIGNIFICADO de cada tecla vive en
// Keymap como datos. Estas pruebas verifican los enlaces por defecto y que
// el Keymap acepte rebindearse (reconfiguracion en tiempo de ejecucion) sin
// que el Editor tenga que cambiar. Tambien cubren el fallback de
// modificadores: "[1;2A" (flecha arriba con modificador) se resuelve igual
// que "[A".
// ---------------------------------------------------------------------------

namespace {

// Comprueba que el enlace de un byte de control produzca el Evento
// esperado, y que un byte sin enlazar no produzca nada.
void checkControl(unsigned char byte, EventType expected) {
    Keymap km;
    const auto t = km.control(byte);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(expected));
}

// Idem para secuencias de escape (contenido tras el ESC).
void checkSequence(const std::string& contents, EventType expected) {
    Keymap km;
    const auto t = km.sequence(contents);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(expected));
}

} // namespace

// --- Enlaces por defecto: teclas de control de un byte ---
TEST(keymap_ctrl_q)   { checkControl(17, EventType::Quit); }
TEST(keymap_ctrl_s)   { checkControl(19, EventType::Save); }
TEST(keymap_ctrl_k)   { checkControl(11, EventType::Prefix); }
TEST(keymap_ctrl_u)   { checkControl(21, EventType::Undo); }
TEST(keymap_ctrl_y)   { checkControl(25, EventType::Redo); }
TEST(keymap_backspace){ checkControl(127, EventType::Backspace); }
TEST(keymap_backspace_bs){ checkControl(8, EventType::Backspace); }
TEST(keymap_enter)    { checkControl(13, EventType::InsertNewline); }
TEST(keymap_enter_lf) { checkControl(10, EventType::InsertNewline); }

// --- Enlaces por defecto: secuencias de escape ---
TEST(keymap_seq_arrows) {
    checkSequence("[A", EventType::MoveUp);
    checkSequence("[B", EventType::MoveDown);
    checkSequence("[C", EventType::MoveRight);
    checkSequence("[D", EventType::MoveLeft);
}
TEST(keymap_seq_ss3) {
    checkSequence("OA", EventType::MoveUp);
    checkSequence("OB", EventType::MoveDown);
    checkSequence("OC", EventType::MoveRight);
    checkSequence("OD", EventType::MoveLeft);
    checkSequence("OH", EventType::MoveEnd);
    checkSequence("OF", EventType::MoveHome);
}
TEST(keymap_seq_home_end) {
    checkSequence("[H", EventType::MoveHome);
    checkSequence("[F", EventType::MoveEnd);
    checkSequence("[1~", EventType::MoveHome);
    checkSequence("[7~", EventType::MoveHome);
    checkSequence("[4~", EventType::MoveEnd);
    checkSequence("[8~", EventType::MoveEnd);
}
TEST(keymap_seq_delete_pages) {
    checkSequence("[3~", EventType::Delete);
    checkSequence("[5~", EventType::PageUp);
    checkSequence("[6~", EventType::PageDown);
}

// --- Rebindeo en tiempo de ejecucion ---
TEST(keymap_remap_control) {
    Keymap km;
    km.bindControl(18, EventType::PageDown); // Ctrl+R -> AvPag (reconfigurado)
    const auto t = km.control(18);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(EventType::PageDown));
}

TEST(keymap_remap_sequence) {
    Keymap km;
    // El Editor no depende de las teclas fisicas: le da igual que MoveLeft
    // venga de la flecha o de otra secuencia remapeada.
    km.bindSequence("1;2D", EventType::MoveLeft);
    CHECK_EQ(static_cast<int>(*km.sequence("1;2D")),
             static_cast<int>(EventType::MoveLeft));
}

TEST(keymap_unbound_control) {
    Keymap km;
    CHECK(!km.control(9).has_value()); // Tab sin enlazar por defecto
}

TEST(keymap_unbound_sequence) {
    Keymap km;
    CHECK(!km.sequence("[9~").has_value()); // secuencia desconocida
}

TEST(keymap_reset_defaults) {
    Keymap km;
    km.bindControl(17, EventType::PageDown);    // romper Ctrl+Q
    km.bindSequence("[A", EventType::MoveRight); // romper flecha arriba
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.control(17)), static_cast<int>(EventType::Quit));
    CHECK_EQ(static_cast<int>(*km.sequence("[A")), static_cast<int>(EventType::MoveUp));
}