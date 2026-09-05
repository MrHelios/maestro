#include "test_support.h"

// 1 Entrada y salida
TEST(busqueda_f_entra) {
    Editor ed; ed.active().document.restore({"abc"});
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    ed.handleEvent(insert('f'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
    CHECK_EQ(ed.searchQuery_, "");
    CHECK_EQ(ed.statusMessage_.text, "Find: ");
}
TEST(busqueda_esc_vacia) {
    Editor ed; ed.active().document.restore({"abc"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    Position orig{1,1}; orig.line=0; orig.col=1;
    ed.handleEvent(insert('f'));
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(ed.searchQuery_, "");
}
TEST(busqueda_enter_vacia) {
    Editor ed; ed.active().document.restore({"abc"});
    ed.active().cursor.line=0; ed.active().cursor.col=2;
    Position orig{0,2};
    ed.handleEvent(insert('f'));
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
}

// 2 Búsqueda básica
TEST(busqueda_unica_coincidencia) {
    Editor ed; ed.active().document.restore({"hello world"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(insert('f')); typeBytes(ed,"world");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 6);
}
TEST(busqueda_varias_primera) {
    Editor ed; ed.active().document.restore({"hello","world","hello again"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_desde_cursor) {
    Editor ed; ed.active().document.restore({"hello","world","hello"});
    ed.active().cursor.line=1; ed.active().cursor.col=0;
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_exacta_sobre_coincidencia) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_no_coincidencias) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().cursor.line=0; ed.active().cursor.col=2;
    Position orig{0,2};
    ed.handleEvent(insert('f')); typeBytes(ed,"zzz");
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
}
TEST(busqueda_query_vacia_vuelve_origen) {
    Editor ed; ed.active().document.restore({"hello world"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    Position orig{0,1};
    ed.handleEvent(insert('f')); typeBytes(ed,"hi");
    ed.handleEvent(ev(EventType::Backspace)); ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "");
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(ed.statusMessage_.text, "Find: ");
}

// 3 Navegación
TEST(busqueda_navegacion_down) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 4);
}
TEST(busqueda_navegacion_up) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown)); ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 4);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 0);
}
TEST(busqueda_wrap_down) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown)); ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 4);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
}
TEST(busqueda_wrap_up) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 4);
}
TEST(busqueda_una_sola_no_cambia) {
    Editor ed; ed.active().document.restore({"hello","abc","xyz"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
}
TEST(busqueda_sin_coincidencias_up_down_no_cambia) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"zzz");
    Position p{ed.active().cursor.line, ed.active().cursor.col};
    ed.handleEvent(ev(EventType::MoveUp));
    CHECK_EQ(ed.active().cursor.line, p.line);
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, p.line);
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
}
TEST(busqueda_varias_misma_linea) {
    Editor ed; ed.active().document.restore({"hola hola hola"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hola");
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.col, 5);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.col, 10);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_navegacion_tres_matches_wrap) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK(ed.statusMessage_.text.find("(1/3)") != std::string::npos);
    CHECK(ed.searchHighlight_.has_value());
    CHECK_EQ(ed.searchHighlight_->anchor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK(ed.statusMessage_.text.find("(2/3)") != std::string::npos);
    CHECK_EQ(ed.searchHighlight_->anchor.line, 2);
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 4);
    CHECK(ed.statusMessage_.text.find("(3/3)") != std::string::npos);
    CHECK_EQ(ed.searchHighlight_->anchor.line, 4);
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK(ed.statusMessage_.text.find("(1/3)") != std::string::npos);
    CHECK_EQ(ed.searchHighlight_->anchor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp));
    CHECK_EQ(ed.active().cursor.line, 4);
    CHECK(ed.statusMessage_.text.find("(3/3)") != std::string::npos);
    CHECK_EQ(ed.searchHighlight_->anchor.line, 4);
}

TEST(busqueda_contador_1) {
    Editor ed; ed.active().document.restore({"ola"});
    ed.handleEvent(insert('f')); typeBytes(ed,"ola");
    CHECK(ed.statusMessage_.text.find("(1/1)") != std::string::npos);
}
TEST(busqueda_contador_2) {
    Editor ed; ed.active().document.restore({"ola","ola"});
    ed.handleEvent(insert('f')); typeBytes(ed,"ola");
    CHECK(ed.statusMessage_.text.find("(1/2)") != std::string::npos);
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK(ed.statusMessage_.text.find("(2/2)") != std::string::npos);
}
TEST(busqueda_contador_12) {
    std::vector<std::string> lines(12,"ola");
    Editor ed; ed.active().document.restore(lines);
    ed.handleEvent(insert('f')); typeBytes(ed,"ola");
    CHECK(ed.statusMessage_.text.find("(1/12)") != std::string::npos);
    CHECK(ed.statusMessage_.text.find("(100+)") == std::string::npos);
}
TEST(busqueda_contador_100) {
    std::vector<std::string> lines(100,"ola");
    Editor ed; ed.active().document.restore(lines);
    ed.handleEvent(insert('f')); typeBytes(ed,"ola");
    CHECK(ed.statusMessage_.text.find("(1/100)") != std::string::npos);
    CHECK(ed.statusMessage_.text.find("(100+)") == std::string::npos);
}
TEST(busqueda_contador_101) {
    std::vector<std::string> lines(101,"ola");
    Editor ed; ed.active().document.restore(lines);
    ed.handleEvent(insert('f')); typeBytes(ed,"ola");
    CHECK(ed.statusMessage_.text.find("(100+)") != std::string::npos);
    CHECK(ed.statusMessage_.text.find("(1/101)") == std::string::npos);
}
TEST(busqueda_contador_101_navega) {
    std::vector<std::string> lines(101,"ola");
    Editor ed; ed.active().document.restore(lines);
    ed.handleEvent(insert('f')); typeBytes(ed,"ola");
    CHECK(ed.statusMessage_.text.find("(100+)") != std::string::npos);
    for (int i = 0; i < 100; ++i) ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 100);
    CHECK(ed.statusMessage_.text.find("(100+)") != std::string::npos);
    CHECK(ed.searchHighlight_.has_value());
    CHECK_EQ(ed.searchHighlight_->anchor.line, 100);
}

// 4 Actualización incremental
TEST(busqueda_incremental_cada_caracter) {
    Editor ed; ed.active().document.restore({"hello","help","hero"});
    ed.handleEvent(insert('f'));
    ed.handleEvent(insert('h')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(insert('e')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(insert('l')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(insert('l')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(insert('o')); CHECK_EQ(ed.active().cursor.line, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}
TEST(busqueda_backspace_recalcula) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "hell");
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}
TEST(busqueda_notfound_a_encontrado) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(insert('f')); ed.handleEvent(insert('x'));
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    ed.handleEvent(ev(EventType::Backspace)); ed.handleEvent(insert('h'));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}
TEST(busqueda_encontrado_a_notfound) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    Position p{ed.active().cursor.line, ed.active().cursor.col};
    ed.handleEvent(insert('x'));
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    CHECK_EQ(ed.active().cursor.line, p.line);
    CHECK_EQ(ed.active().cursor.col, p.col);
}

// 5 ESC / ENTER
TEST(busqueda_esc_vuelve_origen) {
    Editor ed; ed.active().document.restore({"abc hola","xxxx","abc hola"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    Position orig{0,0};
    ed.handleEvent(insert('f')); typeBytes(ed,"hola");
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(ed.searchQuery_, "");
}
TEST(busqueda_enter_deja_posicion) {
    Editor ed; ed.active().document.restore({"abc hola","xxxx","abc hola"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hola");
    ed.handleEvent(ev(EventType::MoveDown));
    Position last{ed.active().cursor.line, ed.active().cursor.col};
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, last.line);
    CHECK_EQ(ed.active().cursor.col, last.col);
}

// 6 UTF-8 y casos límite
TEST(busqueda_utf8) {
    Editor ed; ed.active().document.restore({"abc café hola"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(insert('f')); typeBytes(ed,"hola");
    CHECK_EQ(ed.active().cursor.col, 10);
    ed.handleEvent(ev(EventType::Escape));
    ed.handleEvent(insert('f')); typeBytes(ed,"café");
    CHECK_EQ(ed.active().cursor.col, 4);
    ed.handleEvent(ev(EventType::Escape));
    ed.handleEvent(insert('f')); typeBytes(ed,"é");
    CHECK_EQ(ed.active().cursor.col, 7);
}
TEST(busqueda_utf8_backspace) {
    Editor ed; ed.active().document.restore({"café café"});
    ed.handleEvent(insert('f')); typeBytes(ed,"café");
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "caf");
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_archivo_vacio) {
    Editor ed; ed.active().document.restore({""});
    ed.handleEvent(insert('f')); typeBytes(ed,"a");
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}
TEST(busqueda_abc_hola_wrap) {
    Editor ed; ed.active().document.restore({"abc hola","xxxx","abc hola"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(insert('f')); typeBytes(ed,"hola");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 2);
}

TEST(busqueda_esc_restaura_linea5_col10) {
    Editor ed;
    std::vector<std::string> lines(6,"");
    lines[5]="          hello";
    ed.active().document.restore(lines);
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    Position orig{0,0};
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 5);
    CHECK_EQ(ed.active().cursor.col, 10);
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(busqueda_esc_varias_navegaciones_vuelve_origen) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    Position orig{0,0};
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 4);
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
}

TEST(busqueda_esc_notfound_vuelve_origen) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    Position orig{0,1};
    ed.handleEvent(insert('f')); typeBytes(ed,"zzz");
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(busqueda_esc_tras_modificar_query_vuelve_origen) {
    Editor ed; ed.active().document.restore({"hello","hello world"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    Position orig{0,0};
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::Backspace));
    typeBytes(ed,"o");
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(ed.searchQuery_, "");
}

TEST(busqueda_enter_conserva_primera) {
    Editor ed; ed.active().document.restore({"hello world"});
    ed.handleEvent(insert('f')); typeBytes(ed,"world");
    Position match{ed.active().cursor.line, ed.active().cursor.col};
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, match.line);
    CHECK_EQ(ed.active().cursor.col, match.col);
}

TEST(busqueda_enter_conserva_navegada) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::MoveDown));
    Position match{ed.active().cursor.line, ed.active().cursor.col};
    CHECK_EQ(match.line, 4);
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, match.line);
    CHECK_EQ(ed.active().cursor.col, match.col);
}

TEST(busqueda_enter_despues_up) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp));
    Position match{ed.active().cursor.line, ed.active().cursor.col};
    CHECK_EQ(match.line, 4);
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(ed.active().cursor.line, match.line);
    CHECK_EQ(ed.active().cursor.col, match.col);
}

TEST(busqueda_enter_notfound_conserva_origen) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().cursor.line=0; ed.active().cursor.col=2;
    Position orig{0,2};
    ed.handleEvent(insert('f')); typeBytes(ed,"zzz");
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
}

TEST(busqueda_no_modifica_documento) {
    Editor ed; ed.active().document.restore({"hello","world"});
    auto before = ed.active().document.snapshot();
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::MoveUp));
    ed.handleEvent(ev(EventType::Backspace));
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK(before == ed.active().document.snapshot());
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
}

TEST(busqueda_no_genera_undo) {
    Editor ed; ed.active().document.restore({"hello","world","hello"});
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::MoveUp));
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK(ed.active().redoStack.empty() || ed.active().redoStack.size()==0);
}

TEST(busqueda_ctrl_u_no_modifica) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().document.insertText(0,5,"x");
    {   // Sembrar el historial a la manera nueva: una entrada con la edit.
        HistoryEntry e = ed.active().beginHistoryEntry();
        e.edits.push_back({EditType::Insert, {0, 5}, {0, 6}, "x"});
        ed.active().commitHistoryEntry(std::move(e));
    }
    auto before = ed.active().document.snapshot();
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::Undo));
    CHECK(before == ed.active().document.snapshot());
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
}

TEST(busqueda_ctrl_y_no_modifica) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(ev(EventType::Undo));
    auto before = ed.active().document.snapshot();
    size_t undoBefore = ed.active().undoStack.size();
    size_t redoBefore = ed.active().redoStack.size();
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    ed.handleEvent(ev(EventType::Redo));
    CHECK(before == ed.active().document.snapshot());
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().redoStack.size(), redoBefore);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
}

TEST(busqueda_utf8_caracter_e_acento) {
    Editor ed; ed.active().document.restore({"café"});
    ed.handleEvent(insert('f')); typeBytes(ed,"é");
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}

TEST(busqueda_utf8_palabra_programacion) {
    Editor ed; ed.active().document.restore({"programación"});
    ed.handleEvent(insert('f')); typeBytes(ed,"programación");
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}

TEST(busqueda_utf8_backspace_progresivo) {
    Editor ed; ed.active().document.restore({"café"});
    ed.handleEvent(insert('f')); typeBytes(ed,"café");
    CHECK_EQ(ed.searchQuery_, "café");
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "caf");
    CHECK(ed.searchQuery_.find("caf")!=std::string::npos);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "ca");
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "c");
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "");
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "");
}

TEST(busqueda_utf8_simbolo_emdash) {
    Editor ed; ed.active().document.restore({"hola — mundo"});
    ed.handleEvent(insert('f')); typeBytes(ed,"—");
    CHECK_EQ(ed.active().cursor.col, 5);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}

TEST(busqueda_utf8_varias_cafe) {
    Editor ed; ed.active().document.restore({"café","café","café"});
    ed.handleEvent(insert('f')); typeBytes(ed,"café");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 1);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 2);
}

TEST(busqueda_multilinea_inicio) {
    Editor ed; ed.active().document.restore({"hello world","xxx"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0); CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_multilinea_medio) {
    Editor ed; ed.active().document.restore({"abc hello xyz","xxx"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0); CHECK_EQ(ed.active().cursor.col, 4);
}
TEST(busqueda_multilinea_final) {
    Editor ed; ed.active().document.restore({"abc hello","xxx"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0); CHECK_EQ(ed.active().cursor.col, 4);
}
TEST(busqueda_multilinea_ultima) {
    Editor ed; ed.active().document.restore({"xxx","xxx","hello"});
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 2); CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_documento_vacio) {
    Editor ed; ed.active().document.restore({""});
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    ed.handleEvent(insert('f')); typeBytes(ed,"a");
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
}
TEST(busqueda_muchas_lineas_circular) {
    std::vector<std::string> lines(100,"xxx");
    lines[0]="hello"; lines[25]="hello"; lines[50]="hello"; lines[75]="hello"; lines[99]="hello";
    Editor ed; ed.active().document.restore(lines);
    ed.handleEvent(insert('f')); typeBytes(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 25);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 50);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 75);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 99);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 99);
}

TEST(busqueda_cambio_query_recalcula) {
    std::vector<std::string> lines(30,"xxx");
    lines[5]="ola"; lines[10]="ol"; lines[15]="ola ol"; lines[20]="ol";
    Editor ed; ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 0;
    ed.active().cursor.line = 0; ed.active().cursor.col = 0;
    ed.handleEvent(insert('f')); typeBytes(ed,"ola");
    CHECK(ed.searchHighlight_.has_value());
    CHECK_EQ(ed.active().cursor.line, 5);
    CHECK(ed.statusMessage_.text.find("(1/2)") != std::string::npos);
    {
        auto hl = *ed.searchHighlight_;
        CHECK_EQ(hl.anchor.line, 5); CHECK_EQ(hl.position.line, 5);
        CHECK_EQ(hl.anchor.col, 0); CHECK_EQ(hl.position.col, 3);
    }
    CHECK_EQ(ed.active().viewport.top, 0);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "ol");
    CHECK(ed.searchHighlight_.has_value());
    CHECK_EQ(ed.active().cursor.line, 5);
    CHECK(ed.statusMessage_.text.find("(1/5)") != std::string::npos);
    {
        auto hl = *ed.searchHighlight_;
        CHECK_EQ(hl.anchor.col, 0); CHECK_EQ(hl.position.col, 2);
    }
    ed.handleEvent(insert('a'));
    CHECK_EQ(ed.searchQuery_, "ola");
    CHECK_EQ(ed.active().cursor.line, 5);
    CHECK(ed.statusMessage_.text.find("(1/2)") != std::string::npos);
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 15);
    CHECK(ed.statusMessage_.text.find("(2/2)") != std::string::npos);
    CHECK_EQ(ed.active().viewport.top, 10);
    ed.handleEvent(ev(EventType::Backspace));
    ed.handleEvent(ev(EventType::Backspace));
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "");
    CHECK(!ed.searchHighlight_.has_value());
    CHECK_EQ(ed.statusMessage_.text, "Find: ");
    CHECK_EQ(ed.active().cursor.line, 0);
}

TEST(busqueda_cafe_cafe_dos_matches_highlight) {
    Editor ed; ed.active().document.restore({"café café"});
    ed.handleEvent(insert('f')); typeBytes(ed,"café");
    auto m = ed.collectMatches("café");
    CHECK_EQ((int)m.size(), 2);
    CHECK_EQ(m[0].line, 0); CHECK_EQ(m[0].col, 0);
    CHECK_EQ(m[1].line, 0); CHECK_EQ(m[1].col, 6);
    CHECK(ed.searchHighlight_.has_value());
    {
        auto hl = *ed.searchHighlight_;
        CHECK_EQ(hl.anchor.line, 0); CHECK_EQ(hl.anchor.col, 0);
        CHECK_EQ(hl.position.line, 0); CHECK_EQ(hl.position.col, 5);
    }
    CHECK(ed.statusMessage_.text.find("(1/2)") != std::string::npos);
    ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.col, 6);
    CHECK(ed.statusMessage_.text.find("(2/2)") != std::string::npos);
    {
        auto hl = *ed.searchHighlight_;
        CHECK_EQ(hl.anchor.col, 6); CHECK_EQ(hl.position.col, 11);
    }
}
TEST(busqueda_cafe_e_un_columna_highlight) {
    Editor ed; ed.active().document.restore({"café"});
    ed.handleEvent(insert('f')); typeBytes(ed,"é");
    auto m = ed.collectMatches("é");
    CHECK_EQ((int)m.size(), 1);
    CHECK_EQ(m[0].col, 3);
    CHECK(ed.searchHighlight_.has_value());
    {
        auto hl = *ed.searchHighlight_;
        CHECK_EQ(hl.anchor.line, 0); CHECK_EQ(hl.anchor.col, 3);
        CHECK_EQ(hl.position.line, 0); CHECK_EQ(hl.position.col, 5);
        int sc = utf8::columnOf(ed.active().document.lineAt(0), hl.anchor.col);
        int ec = utf8::columnOf(ed.active().document.lineAt(0), hl.position.col);
        CHECK_EQ(ec - sc, 1);
    }
    CHECK(ed.statusMessage_.text.find("(1/1)") != std::string::npos);
}

TEST(busqueda_invariant_entrar) { Editor ed; ed.active().document.restore({"hello"}); assertStateConsistent(ed); ed.handleEvent(insert('f')); assertStateConsistent(ed); }
TEST(busqueda_invariant_escribir) { Editor ed; ed.active().document.restore({"hello world"}); ed.handleEvent(insert('f')); for(char c: std::string("hello")){ ed.handleEvent(insert(c)); assertStateConsistent(ed); } }
TEST(busqueda_invariant_backspace) { Editor ed; ed.active().document.restore({"hello"}); ed.handleEvent(insert('f')); typeBytes(ed,"hello"); assertStateConsistent(ed); ed.handleEvent(ev(EventType::Backspace)); assertStateConsistent(ed); ed.handleEvent(ev(EventType::Backspace)); assertStateConsistent(ed); }
TEST(busqueda_invariant_up) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(insert('f')); typeBytes(ed,"hello"); assertStateConsistent(ed); ed.handleEvent(ev(EventType::MoveUp)); assertStateConsistent(ed); }
TEST(busqueda_invariant_down) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(insert('f')); typeBytes(ed,"hello"); assertStateConsistent(ed); ed.handleEvent(ev(EventType::MoveDown)); assertStateConsistent(ed); }
TEST(busqueda_invariant_esc) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(insert('f')); typeBytes(ed,"hello"); ed.handleEvent(ev(EventType::MoveDown)); assertStateConsistent(ed); ed.handleEvent(ev(EventType::Escape)); assertStateConsistent(ed); }
TEST(busqueda_invariant_enter) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(insert('f')); typeBytes(ed,"hello"); ed.handleEvent(ev(EventType::MoveDown)); assertStateConsistent(ed); ed.handleEvent(ev(EventType::InsertNewline)); assertStateConsistent(ed); }

TEST(busqueda_match_hola_ola) {
    Editor ed; ed.active().document.restore({"hola"});
    auto m = ed.collectMatches("ola");
    CHECK_EQ((int)m.size(), 1);
    CHECK_EQ(m[0].line, 0); CHECK_EQ(m[0].col, 1);
}
TEST(busqueda_match_ola_ola_dos) {
    Editor ed; ed.active().document.restore({"ola ola"});
    auto m = ed.collectMatches("ola");
    CHECK_EQ((int)m.size(), 2);
    CHECK_EQ(m[0].col, 0); CHECK_EQ(m[1].col, 4);
}
TEST(busqueda_match_olaola_dos) {
    Editor ed; ed.active().document.restore({"olaola"});
    auto m = ed.collectMatches("ola");
    CHECK_EQ((int)m.size(), 2);
    CHECK_EQ(m[0].col, 0); CHECK_EQ(m[1].col, 3);
}
TEST(busqueda_match_sin_coincidencia) {
    Editor ed; ed.active().document.restore({"abc"});
    auto m = ed.collectMatches("xyz");
    CHECK_EQ((int)m.size(), 0);
}
TEST(busqueda_match_multilineas) {
    Editor ed; ed.active().document.restore({"ola","xxx","ola","ola"});
    auto m = ed.collectMatches("ola");
    CHECK_EQ((int)m.size(), 3);
    CHECK_EQ(m[0].line, 0); CHECK_EQ(m[1].line, 2); CHECK_EQ(m[2].line, 3);
}
TEST(busqueda_match_query_vacio) {
    Editor ed; ed.active().document.restore({"hola"});
    auto m = ed.collectMatches("");
    CHECK_EQ((int)m.size(), 0);
}
TEST(busqueda_match_query_mas_largo) {
    Editor ed; ed.active().document.restore({"hi"});
    auto m = ed.collectMatches("hello");
    CHECK_EQ((int)m.size(), 0);
}
TEST(busqueda_match_inicio) {
    Editor ed; ed.active().document.restore({"olamundo"});
    auto m = ed.collectMatches("ola");
    CHECK_EQ((int)m.size(), 1);
    CHECK_EQ(m[0].col, 0);
}
TEST(busqueda_match_final) {
    Editor ed; ed.active().document.restore({"mundoola"});
    auto m = ed.collectMatches("ola");
    CHECK_EQ((int)m.size(), 1);
    CHECK_EQ(m[0].col, 5);
}

TEST(busqueda_property_random) {
    Editor ed; ed.active().document.restore({"hello","world","hello world","abc"});
    unsigned long seed=123456;
    auto rnd=[&seed](){ seed=seed*6364136223846793005ULL+1442695040888963407ULL; return (int)((seed>>33)&0xFFFFFFFF); };
    for(int i=0;i<500;++i){
        int k=rnd()%6;
        Event e;
        switch(k){
            case 0: e.type=EventType::InsertChar; e.text=std::string(1, char('a'+ rnd()%26)); if(rnd()%10==0) e.text="f"; break;
            case 1: e.type=EventType::Backspace; break;
            case 2: e.type=EventType::MoveUp; break;
            case 3: e.type=EventType::MoveDown; break;
            case 4: e.type=EventType::Escape; break;
            default: e.type=EventType::InsertNewline; break;
        }
        if(ed.state_!=State::Busqueda && rnd()%3==0) ed.handleEvent(insert('f'));
        else ed.handleEvent(e);
        assertStateConsistent(ed);
    }
}
