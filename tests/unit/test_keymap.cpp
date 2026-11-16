#include "platform/tty/TtyKeymap.h"
#include "platform/InputEvent.h"
#include "test_framework.h"

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
TEST(keymap_ctrl_q)      { checkControl(17,  InputEventType::Quit); }
TEST(keymap_ctrl_s)      { checkControl(19,  InputEventType::Save); }
TEST(keymap_ctrl_k)      { checkControl(11,  InputEventType::Prefix); }
TEST(keymap_ctrl_u)      { checkControl(21,  InputEventType::Undo); }
TEST(keymap_ctrl_y)      { checkControl(25,  InputEventType::Redo); }
TEST(keymap_ctrl_z)      { checkControl(26,  InputEventType::Suspend); }
TEST(keymap_backspace)   { checkControl(127, InputEventType::Backspace); }
TEST(keymap_backspace_bs){ checkControl(8,   InputEventType::Backspace); }
TEST(keymap_enter)       { checkControl(13,  InputEventType::InsertNewline); }
TEST(keymap_enter_lf)    { checkControl(10,  InputEventType::InsertNewline); }

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
    checkSequence("[H",  InputEventType::MoveHome);
    checkSequence("[F",  InputEventType::MoveEnd);
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

// Los modificadores de las secuencias de escape se ignoran:
// cualquier combinación de shift/alt/ctrl cae a la tecla base
TEST(keymap_seq_arrow_with_modifier_falls_back) {
    // Up
    checkSequence("[1;2A", InputEventType::MoveUp);     // Shift
    checkSequence("[1;3A", InputEventType::MoveUp);     // Alt
    checkSequence("[1;4A", InputEventType::MoveUp);     // Shift+Alt
    checkSequence("[1;5A", InputEventType::MoveUp);     // Ctrl
    checkSequence("[1;6A", InputEventType::MoveUp);     // Shift+Ctrl
    checkSequence("[1;7A", InputEventType::MoveUp);     // Alt+Ctrl
    checkSequence("[1;8A", InputEventType::MoveUp);     // Shift+Alt+Ctrl

    // Down
    checkSequence("[1;2B", InputEventType::MoveDown);
    checkSequence("[1;3B", InputEventType::MoveDown);
    checkSequence("[1;4B", InputEventType::MoveDown);
    checkSequence("[1;5B", InputEventType::MoveDown);
    checkSequence("[1;6B", InputEventType::MoveDown);
    checkSequence("[1;7B", InputEventType::MoveDown);
    checkSequence("[1;8B", InputEventType::MoveDown);

    // Right
    checkSequence("[1;2C", InputEventType::MoveRight);
    checkSequence("[1;3C", InputEventType::MoveRight);
    checkSequence("[1;4C", InputEventType::MoveRight);
    checkSequence("[1;5C", InputEventType::MoveRight);
    checkSequence("[1;6C", InputEventType::MoveRight);
    checkSequence("[1;7C", InputEventType::MoveRight);
    checkSequence("[1;8C", InputEventType::MoveRight);

    // Left
    checkSequence("[1;2D", InputEventType::MoveLeft);
    checkSequence("[1;3D", InputEventType::MoveLeft);
    checkSequence("[1;4D", InputEventType::MoveLeft);
    checkSequence("[1;5D", InputEventType::MoveLeft);
    checkSequence("[1;6D", InputEventType::MoveLeft);
    checkSequence("[1;7D", InputEventType::MoveLeft);
    checkSequence("[1;8D", InputEventType::MoveLeft);

    // Home
    checkSequence("[1;2H", InputEventType::MoveHome);
    checkSequence("[1;3H", InputEventType::MoveHome);
    checkSequence("[1;4H", InputEventType::MoveHome);
    checkSequence("[1;5H", InputEventType::MoveHome);
    checkSequence("[1;6H", InputEventType::MoveHome);
    checkSequence("[1;7H", InputEventType::MoveHome);
    checkSequence("[1;8H", InputEventType::MoveHome);

    // End
    checkSequence("[1;2F", InputEventType::MoveEnd);
    checkSequence("[1;3F", InputEventType::MoveEnd);
    checkSequence("[1;4F", InputEventType::MoveEnd);
    checkSequence("[1;5F", InputEventType::MoveEnd);
    checkSequence("[1;6F", InputEventType::MoveEnd);
    checkSequence("[1;7F", InputEventType::MoveEnd);
    checkSequence("[1;8F", InputEventType::MoveEnd);
}

// Rebindeo en tiempo de ejecucion
TEST(keymap_remap_control) {
    TtyKeymap km;
    
    // Ctrl+R -> PagDown (reconfigurado)
    km.bindControl(18, InputEventType::PageDown); 
    
    const auto t = km.control(18);
    CHECK(t.has_value());
    if (t) CHECK_EQ(static_cast<int>(*t), static_cast<int>(InputEventType::PageDown));
}

// El Editor no depende de las teclas fisicas: 
// le da igual que MoveLeft venga de la flecha o de otra secuencia remapeada.
TEST(keymap_remap_sequence) {
    TtyKeymap km, km2;
    
    km.bindSequence("[1;2D", InputEventType::PageDown);
    CHECK_EQ(static_cast<int>(*km.sequence("[1;2D")),
             static_cast<int>(InputEventType::PageDown));
             
    // Sin binding explicito, 
    // el fallback de modificadores resuelve "[1;2D"
    CHECK_EQ(static_cast<int>(*km2.sequence("[1;2D")),
             static_cast<int>(InputEventType::MoveLeft));
}

// Tab sin enlazar por defecto
TEST(keymap_unbound_control) {
    TtyKeymap km;
    CHECK(!km.control(9).has_value());
}

// Secuencia desconocida
TEST(keymap_unbound_sequence) {
    TtyKeymap km;
    CHECK(!km.sequence("[9~").has_value()); 
}

TEST(keymap_reset_defaults) {
    TtyKeymap km;
    
    km.bindControl(17, InputEventType::PageDown);     // romper Ctrl+Q
    km.bindSequence("[A", InputEventType::MoveRight); // romper flecha arriba
    CHECK_EQ(static_cast<int>(*km.control(17)), static_cast<int>(InputEventType::PageDown));
    CHECK_EQ(static_cast<int>(*km.sequence("[A")), static_cast<int>(InputEventType::MoveRight));
    
    km.resetDefaults();
    CHECK_EQ(static_cast<int>(*km.control(17)), static_cast<int>(InputEventType::Quit));
    CHECK_EQ(static_cast<int>(*km.sequence("[A")), static_cast<int>(InputEventType::MoveUp));
}
