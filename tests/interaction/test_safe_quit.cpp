#include "test_framework.h"

#define private public
#include "ui/Editor.h"
#undef private
#include "clipboard/FakeClipboard.h"

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

static void press(Editor& ed, EventType t) {
    Event e;
    e.type = t;
    ed.handleEvent(e);
}

static void pressEvent(Editor& ed, const Event& ev) {
    ed.handleEvent(ev);
}

static void type(Editor& ed, const std::string& s) {
    if (s.empty()) return;
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            Event esc; esc.type = EventType::Escape; ed.handleEvent(esc);
        }
        ed.handleEvent(insert('i'));
    }
    for (char c : s) ed.handleEvent(insert(c));
}

static void safeQuit(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('q'));
}

static void forcedQuit(Editor& ed) {
    press(ed, EventType::Prefix);
    press(ed, EventType::Quit);
}

static void saveViaS(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('s'));
}

static void newBuffer(Editor& ed) {
    press(ed, EventType::Prefix);
    pressEvent(ed, insert('n'));
}

TEST(safe_quit_single_saved_exits) {
    Editor ed(std::make_unique<FakeClipboard>());
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_single_modified_blocks) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "hola");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
}

TEST(safe_quit_one_saved_one_modified_from_A) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "modB");
    press(ed, EventType::Escape);
    ed.buffers.activate(0);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(ed.buffers.at(1).modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_active_saved_inactive_modified) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "x");
    press(ed, EventType::Escape);
    ed.buffers.activate(0);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_active_modified_others_saved) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    type(ed, "modActive");
    press(ed, EventType::Escape);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(!ed.buffers.at(1).modified);
    CHECK(ed.buffers.at(2).modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_varios_modificados) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    for (int i = 0; i < 3; ++i) ed.buffers.at(i).modified = true;
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
}

TEST(safe_quit_todos_guardados_despues) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    for (int i = 0; i < 3; ++i) {
        ed.buffers.at(i).modified = true;
        ed.buffers.at(i).originalSnapshot_ = ed.buffers.at(i).document.snapshot();
        ed.buffers.at(i).modified = false;
    }
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_no_destruye_buffers) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "abc");
    press(ed, EventType::Escape);
    int cnt = ed.buffers.count();
    auto snap = ed.active().document.snapshot();
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK_EQ(ed.buffers.count(), cnt);
    CHECK(ed.active().document.snapshot() == snap);
}

TEST(safe_quit_reintentar_despues_de_guardar) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "xyz");
    press(ed, EventType::Escape);
    safeQuit(ed);
    CHECK(ed.running_);
    ed.active().modified = false;
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_bloquea_forzado_permite) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "hola");
    press(ed, EventType::Escape);
    safeQuit(ed);
    CHECK(ed.running_);
    forcedQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_forzado_varios_modificados) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    for (int i = 0; i < 3; ++i) ed.buffers.at(i).modified = true;
    safeQuit(ed);
    CHECK(ed.running_);
    forcedQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_buffer_nuevo_vacio) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_buffer_nuevo_con_contenido) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "contenido");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(safe_quit_buffer_nuevo_guardado_posterior) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "contenido");
    press(ed, EventType::Escape);
    ed.active().modified = false;
    ed.active().originalSnapshot_ = ed.active().document.snapshot();
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(safe_quit_mixto_integracion) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "modB");
    press(ed, EventType::Escape);
    newBuffer(ed);
    newBuffer(ed);
    type(ed, "modD");
    press(ed, EventType::Escape);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(ed.buffers.at(1).modified);
    CHECK(!ed.buffers.at(2).modified);
    CHECK(ed.buffers.at(3).modified);
    safeQuit(ed);
    CHECK(ed.running_);
    ed.buffers.at(1).modified = false;
    ed.buffers.at(3).modified = false;
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(save_as_copy_prefill_editable_and_moves) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f;
    f.write("hola");
    CHECK(ed.openFile(f.path));
    type(ed, "X");
    press(ed, EventType::Escape);
    press(ed, EventType::Prefix);
    Event e; e.type = EventType::Save; ed.handleEvent(e);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));
    CHECK_EQ(ed.saveAsPath_, f.path);
    Event esc; esc.type = EventType::Escape; ed.handleEvent(esc);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().modified);
    press(ed, EventType::Prefix);
    e.type = EventType::Save; ed.handleEvent(e);
    testfw::TempFile g;
    for (size_t i = 0; i < f.path.size(); ++i) { Event b; b.type = EventType::Backspace; ed.handleEvent(b); }
    for (char c : g.path) ed.handleEvent(insert(c));
    CHECK_EQ(ed.saveAsPath_, g.path);
    Event ent; ent.type = EventType::InsertNewline; ed.handleEvent(ent);
    CHECK_EQ(ed.active().filename, g.path);
    CHECK(!ed.active().modified);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(principal_01_salir_sin_modificados) {
    Editor ed(std::make_unique<FakeClipboard>());
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
    CHECK(ed.statusMessage_.text.find("sin guardar") == std::string::npos);
}

TEST(principal_02_un_buffer_guardado) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("contenido");
    CHECK(ed.openFile(f.path));
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(principal_03_un_buffer_modificado_cancela) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("a");
    CHECK(ed.openFile(f.path));
    type(ed, "X");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    auto snap = ed.active().document.snapshot();
    int stateBefore = static_cast<int>(ed.state_);
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK_EQ(static_cast<int>(ed.state_), stateBefore);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
    CHECK(ed.active().document.snapshot() == snap);
}

TEST(principal_04_varios_todos_guardados) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("a");
    CHECK(ed.openFile(f.path));
    newBuffer(ed);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(!ed.buffers.at(1).modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(principal_05_varios_uno_sin_guardar) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("A");
    CHECK(ed.openFile(f.path));
    newBuffer(ed);
    type(ed, "modB");
    press(ed, EventType::Escape);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(ed.buffers.at(1).modified);
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.buffers.at(1).modified);
}

TEST(principal_06_varios_varios_sin_guardar) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    newBuffer(ed);
    for (int i = 0; i < 3; ++i) ed.buffers.at(i).modified = true;
    auto snaps = ed.buffers.at(0).document.snapshot();
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
    for (int i = 0; i < 3; ++i) CHECK(ed.buffers.at(i).modified);
}

TEST(caso_07_buffer_nuevo_sin_modificaciones) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(caso_08_buffer_nuevo_con_contenido) {
    Editor ed(std::make_unique<FakeClipboard>());
    newBuffer(ed);
    type(ed, "hola");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(caso_09_modificar_guardar_q) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("a");
    CHECK(ed.openFile(f.path));
    type(ed, "x");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    saveViaS(ed);
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(caso_10_modificar_guardar_modificar_nuevamente) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("a");
    CHECK(ed.openFile(f.path));
    type(ed, "X");
    press(ed, EventType::Escape);
    saveViaS(ed);
    CHECK(!ed.active().modified);
    type(ed, "Y");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
}

TEST(caso_11_q_no_guarda_automaticamente) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("orig");
    CHECK(ed.openFile(f.path));
    type(ed, "MOD");
    press(ed, EventType::Escape);
    auto snap = ed.active().document.snapshot();
    std::string before = snap[0];
    safeQuit(ed);
    CHECK(ed.running_);
    CHECK(ed.active().modified);
    CHECK(ed.active().document.snapshot() == snap);
    std::ifstream in(f.path);
    std::string disk((std::istreambuf_iterator<char>(in)), {});
    CHECK(disk == "orig");
}

TEST(caso_12_salir_despues_de_guardar_manualmente) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("a");
    CHECK(ed.openFile(f.path));
    type(ed, "X");
    press(ed, EventType::Escape);
    safeQuit(ed);
    CHECK(ed.running_);
    saveViaS(ed);
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(caso_13_activo_guardado_inactivo_modificado) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("A");
    CHECK(ed.openFile(f.path));
    newBuffer(ed);
    type(ed, "Bmod");
    press(ed, EventType::Escape);
    ed.buffers.activate(0);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(ed.buffers.at(1).modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(caso_14_activo_modificado_resto_guardado) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("A");
    CHECK(ed.openFile(f.path));
    newBuffer(ed);
    newBuffer(ed);
    ed.buffers.activate(2);
    type(ed, "X");
    press(ed, EventType::Escape);
    CHECK(ed.buffers.at(2).modified);
    CHECK(!ed.buffers.at(0).modified);
    CHECK(!ed.buffers.at(1).modified);
    safeQuit(ed);
    CHECK(ed.running_);
}

TEST(caso_15_cambiar_buffer_despues_cancelada) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "Amod");
    press(ed, EventType::Escape);
    CHECK(ed.buffers.at(0).modified);
    safeQuit(ed);
    CHECK(ed.running_);
    newBuffer(ed);
    CHECK(ed.buffers.at(0).modified);
    ed.buffers.activate(0);
    CHECK(ed.buffers.at(0).modified);
    CHECK(ed.running_);
}

TEST(caso_16_mensaje_aparece) {
    Editor ed(std::make_unique<FakeClipboard>());
    type(ed, "x");
    press(ed, EventType::Escape);
    safeQuit(ed);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
    CHECK(ed.running_);
}

TEST(caso_17_mensaje_desaparece) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("a");
    CHECK(ed.openFile(f.path));
    type(ed, "x");
    press(ed, EventType::Escape);
    safeQuit(ed);
    CHECK(ed.statusMessage_.text.find("sin guardar") != std::string::npos);
    saveViaS(ed);
    CHECK(!ed.active().modified);
    safeQuit(ed);
    CHECK(!ed.running_);
}

TEST(caso_18_regresion_ctrl_k_s) {
    Editor ed(std::make_unique<FakeClipboard>());
    testfw::TempFile f; f.write("orig");
    CHECK(ed.openFile(f.path));
    type(ed, "MOD");
    press(ed, EventType::Escape);
    CHECK(ed.active().modified);
    saveViaS(ed);
    CHECK(!ed.active().modified);
    std::ifstream in(f.path);
    std::string disk((std::istreambuf_iterator<char>(in)), {});
    CHECK(disk.find("MOD") != std::string::npos);
    CHECK(ed.statusMessage_.text.find("Guardado") != std::string::npos);
}
