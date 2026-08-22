#include <string>
#include <vector>
#include "test_framework.h"
#define private public
#include "ui/Editor.h"
#undef private

static Event ins(char c){ Event e; e.type=EventType::InsertChar; e.text=std::string(1,c); return e; }
static Event ev(EventType t){ Event e; e.type=t; return e; }
static void typeQ(Editor& ed, const std::string& s){ for(unsigned char c: s) ed.handleEvent(ins(c)); }

// 1 Entrada y salida
TEST(busqueda_f_entra) {
    Editor ed; ed.active().document.restore({"abc"});
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    ed.handleEvent(ins('f'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
    CHECK_EQ(ed.searchQuery_, "");
    CHECK_EQ(ed.statusMessage_.text, "Find word: ");
}
TEST(busqueda_esc_vacia) {
    Editor ed; ed.active().document.restore({"abc"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    Position orig{1,1}; orig.line=0; orig.col=1;
    ed.handleEvent(ins('f'));
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
    ed.handleEvent(ins('f'));
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
}

// 2 Búsqueda básica
TEST(busqueda_unica_coincidencia) {
    Editor ed; ed.active().document.restore({"hello world"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(ins('f')); typeQ(ed,"world");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 6);
}
TEST(busqueda_varias_primera) {
    Editor ed; ed.active().document.restore({"hello","world","hello again"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_desde_cursor) {
    Editor ed; ed.active().document.restore({"hello","world","hello"});
    ed.active().cursor.line=1; ed.active().cursor.col=0;
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_exacta_sobre_coincidencia) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_no_coincidencias) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().cursor.line=0; ed.active().cursor.col=2;
    Position orig{0,2};
    ed.handleEvent(ins('f')); typeQ(ed,"zzz");
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
}
TEST(busqueda_query_vacia_vuelve_origen) {
    Editor ed; ed.active().document.restore({"hello world"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    Position orig{0,1};
    ed.handleEvent(ins('f')); typeQ(ed,"hi");
    ed.handleEvent(ev(EventType::Backspace)); ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "");
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(ed.statusMessage_.text, "Find word: ");
}

// 3 Navegación
TEST(busqueda_navegacion_down) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 4);
}
TEST(busqueda_navegacion_up) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown)); ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 4);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 0);
}
TEST(busqueda_wrap_down) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown)); ed.handleEvent(ev(EventType::MoveDown));
    CHECK_EQ(ed.active().cursor.line, 4);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
}
TEST(busqueda_wrap_up) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 4);
}
TEST(busqueda_una_sola_no_cambia) {
    Editor ed; ed.active().document.restore({"hello","abc","xyz"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
}
TEST(busqueda_sin_coincidencias_up_down_no_cambia) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"zzz");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hola");
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.col, 5);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.col, 10);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.col, 0);
}

// 4 Actualización incremental
TEST(busqueda_incremental_cada_caracter) {
    Editor ed; ed.active().document.restore({"hello","help","hero"});
    ed.handleEvent(ins('f'));
    ed.handleEvent(ins('h')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ins('e')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ins('l')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ins('l')); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ins('o')); CHECK_EQ(ed.active().cursor.line, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}
TEST(busqueda_backspace_recalcula) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "hell");
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}
TEST(busqueda_notfound_a_encontrado) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(ins('f')); ed.handleEvent(ins('x'));
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    ed.handleEvent(ev(EventType::Backspace)); ed.handleEvent(ins('h'));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}
TEST(busqueda_encontrado_a_notfound) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    Position p{ed.active().cursor.line, ed.active().cursor.col};
    ed.handleEvent(ins('x'));
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    CHECK_EQ(ed.active().cursor.line, p.line);
    CHECK_EQ(ed.active().cursor.col, p.col);
}

// 5 ESC / ENTER
TEST(busqueda_esc_vuelve_origen) {
    Editor ed; ed.active().document.restore({"abc hola","xxxx","abc hola"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    Position orig{0,0};
    ed.handleEvent(ins('f')); typeQ(ed,"hola");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hola");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hola");
    CHECK_EQ(ed.active().cursor.col, 10);
    ed.handleEvent(ev(EventType::Escape));
    ed.handleEvent(ins('f')); typeQ(ed,"café");
    CHECK_EQ(ed.active().cursor.col, 4);
    ed.handleEvent(ev(EventType::Escape));
    ed.handleEvent(ins('f')); typeQ(ed,"é");
    CHECK_EQ(ed.active().cursor.col, 7);
}
TEST(busqueda_utf8_backspace) {
    Editor ed; ed.active().document.restore({"café café"});
    ed.handleEvent(ins('f')); typeQ(ed,"café");
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(ev(EventType::Backspace));
    CHECK_EQ(ed.searchQuery_, "caf");
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_archivo_vacio) {
    Editor ed; ed.active().document.restore({""});
    ed.handleEvent(ins('f')); typeQ(ed,"a");
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}
TEST(busqueda_abc_hola_wrap) {
    Editor ed; ed.active().document.restore({"abc hola","xxxx","abc hola"});
    ed.active().cursor.line=0; ed.active().cursor.col=0;
    ed.handleEvent(ins('f')); typeQ(ed,"hola");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
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
    ed.handleEvent(ins('f')); typeQ(ed,"zzz");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    ed.handleEvent(ev(EventType::Backspace));
    typeQ(ed,"o");
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::Escape));
    CHECK_EQ(ed.active().cursor.line, orig.line);
    CHECK_EQ(ed.active().cursor.col, orig.col);
    CHECK_EQ(ed.searchQuery_, "");
}

TEST(busqueda_enter_conserva_primera) {
    Editor ed; ed.active().document.restore({"hello world"});
    ed.handleEvent(ins('f')); typeQ(ed,"world");
    Position match{ed.active().cursor.line, ed.active().cursor.col};
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, match.line);
    CHECK_EQ(ed.active().cursor.col, match.col);
}

TEST(busqueda_enter_conserva_navegada) {
    Editor ed; ed.active().document.restore({"hello","abc","hello","abc","hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
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
    ed.handleEvent(ins('f')); typeQ(ed,"zzz");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    ed.handleEvent(ev(EventType::MoveDown));
    ed.handleEvent(ev(EventType::MoveUp));
    ed.handleEvent(ev(EventType::InsertNewline));
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK(ed.active().redoStack.empty() || ed.active().redoStack.size()==0);
}

TEST(busqueda_ctrl_u_no_modifica) {
    Editor ed; ed.active().document.restore({"hello"});
    ed.active().document.insertText(0,5,"x");
    ed.active().pushHistory();
    auto before = ed.active().document.snapshot();
    size_t undoBefore = ed.active().undoStack.size();
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
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
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    ed.handleEvent(ev(EventType::Redo));
    CHECK(before == ed.active().document.snapshot());
    CHECK_EQ(ed.active().undoStack.size(), undoBefore);
    CHECK_EQ(ed.active().redoStack.size(), redoBefore);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
}

TEST(busqueda_utf8_caracter_e_acento) {
    Editor ed; ed.active().document.restore({"café"});
    ed.handleEvent(ins('f')); typeQ(ed,"é");
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}

TEST(busqueda_utf8_palabra_programacion) {
    Editor ed; ed.active().document.restore({"programación"});
    ed.handleEvent(ins('f')); typeQ(ed,"programación");
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}

TEST(busqueda_utf8_backspace_progresivo) {
    Editor ed; ed.active().document.restore({"café"});
    ed.handleEvent(ins('f')); typeQ(ed,"café");
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
    ed.handleEvent(ins('f')); typeQ(ed,"—");
    CHECK_EQ(ed.active().cursor.col, 5);
    CHECK(ed.statusMessage_.text.find("- not found")==std::string::npos);
}

TEST(busqueda_utf8_varias_cafe) {
    Editor ed; ed.active().document.restore({"café","café","café"});
    ed.handleEvent(ins('f')); typeQ(ed,"café");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 1);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 2);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 2);
}

TEST(busqueda_multilinea_inicio) {
    Editor ed; ed.active().document.restore({"hello world","xxx"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0); CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_multilinea_medio) {
    Editor ed; ed.active().document.restore({"abc hello xyz","xxx"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0); CHECK_EQ(ed.active().cursor.col, 4);
}
TEST(busqueda_multilinea_final) {
    Editor ed; ed.active().document.restore({"abc hello","xxx"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0); CHECK_EQ(ed.active().cursor.col, 4);
}
TEST(busqueda_multilinea_ultima) {
    Editor ed; ed.active().document.restore({"xxx","xxx","hello"});
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 2); CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(busqueda_documento_vacio) {
    Editor ed; ed.active().document.restore({""});
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    ed.handleEvent(ins('f')); typeQ(ed,"a");
    CHECK(ed.statusMessage_.text.find("- not found")!=std::string::npos);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
}
TEST(busqueda_muchas_lineas_circular) {
    std::vector<std::string> lines(100,"xxx");
    lines[0]="hello"; lines[25]="hello"; lines[50]="hello"; lines[75]="hello"; lines[99]="hello";
    Editor ed; ed.active().document.restore(lines);
    ed.handleEvent(ins('f')); typeQ(ed,"hello");
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 25);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 50);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 75);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 99);
    ed.handleEvent(ev(EventType::MoveDown)); CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::MoveUp)); CHECK_EQ(ed.active().cursor.line, 99);
}

static bool validUtf8Busq(const std::string& s){ size_t i=0; while(i<s.size()){ unsigned char c=(unsigned char)s[i]; int need; if((c&0x80)==0) need=0; else if((c&0xE0)==0xC0) need=1; else if((c&0xF0)==0xE0) need=2; else if((c&0xF8)==0xF0) need=3; else return false; if(i+need>=s.size()) return false; for(int k=1;k<=need;++k) if(((unsigned char)s[i+k]&0xC0)!=0x80) return false; i+=need+1;} return true; }
static void assertConsistentBusq(Editor& ed){
    const Document& d=ed.active().document;
    CHECK(d.lineCount()>=1);
    for(int i=0;i<d.lineCount();++i) CHECK_EQ(d.lineAt(i).size(), (size_t)d.lineLength(i));
    CHECK(ed.active().cursor.line>=0); CHECK(ed.active().cursor.col>=0);
    CHECK(ed.active().cursor.line<d.lineCount());
    CHECK(ed.active().cursor.col<=d.lineLength(ed.active().cursor.line));
    CHECK(ed.active().undoStack.size()<=Editor::MAX_UNDO);
    CHECK(ed.active().redoStack.size()<=Editor::MAX_UNDO);
    if(ed.hasSelection()) CHECK(ed.active().selection.has_value());
    if(!ed.active().selection.has_value()) CHECK(!ed.hasSelection());
    if(ed.active().selection.has_value()){
        const Position& a=ed.active().selection->anchor;
        const Position& p=ed.active().selection->position;
        CHECK(a.line>=0 && a.line<d.lineCount()); CHECK(a.col>=0 && a.col<=d.lineLength(a.line));
        CHECK(p.line>=0 && p.line<d.lineCount()); CHECK(p.col>=0 && p.col<=d.lineLength(p.line));
    }
    if(ed.active().selection.has_value() && ed.active().selection->anchor==ed.active().selection->position) CHECK(!ed.hasSelection());
    if(auto norm=ed.selection()) CHECK(norm->start.line<norm->end.line || (norm->start.line==norm->end.line && norm->start.col<=norm->end.col));
    CHECK(ed.state_==State::Navegacion||ed.state_==State::Interaccion||ed.state_==State::Seleccion||ed.state_==State::Prefix||ed.state_==State::BufferSelector||ed.state_==State::SaveAs||ed.state_==State::FileBrowser||ed.state_==State::Busqueda);
    if(ed.hasSelection()) CHECK(ed.state_==State::Seleccion||ed.state_==State::Prefix||ed.state_==State::BufferSelector||ed.state_==State::SaveAs||ed.state_==State::FileBrowser||ed.state_==State::Busqueda);
    if(ed.state_==State::Seleccion) CHECK(ed.active().selection.has_value());
    for(auto &l: ed.getClipboardBlock()) CHECK(validUtf8Busq(l));
}

TEST(busqueda_invariant_entrar) { Editor ed; ed.active().document.restore({"hello"}); assertConsistentBusq(ed); ed.handleEvent(ins('f')); assertConsistentBusq(ed); }
TEST(busqueda_invariant_escribir) { Editor ed; ed.active().document.restore({"hello world"}); ed.handleEvent(ins('f')); for(char c: std::string("hello")){ ed.handleEvent(ins(c)); assertConsistentBusq(ed); } }
TEST(busqueda_invariant_backspace) { Editor ed; ed.active().document.restore({"hello"}); ed.handleEvent(ins('f')); typeQ(ed,"hello"); assertConsistentBusq(ed); ed.handleEvent(ev(EventType::Backspace)); assertConsistentBusq(ed); ed.handleEvent(ev(EventType::Backspace)); assertConsistentBusq(ed); }
TEST(busqueda_invariant_up) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(ins('f')); typeQ(ed,"hello"); assertConsistentBusq(ed); ed.handleEvent(ev(EventType::MoveUp)); assertConsistentBusq(ed); }
TEST(busqueda_invariant_down) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(ins('f')); typeQ(ed,"hello"); assertConsistentBusq(ed); ed.handleEvent(ev(EventType::MoveDown)); assertConsistentBusq(ed); }
TEST(busqueda_invariant_esc) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(ins('f')); typeQ(ed,"hello"); ed.handleEvent(ev(EventType::MoveDown)); assertConsistentBusq(ed); ed.handleEvent(ev(EventType::Escape)); assertConsistentBusq(ed); }
TEST(busqueda_invariant_enter) { Editor ed; ed.active().document.restore({"hello","hello"}); ed.handleEvent(ins('f')); typeQ(ed,"hello"); ed.handleEvent(ev(EventType::MoveDown)); assertConsistentBusq(ed); ed.handleEvent(ev(EventType::InsertNewline)); assertConsistentBusq(ed); }

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
        if(ed.state_!=State::Busqueda && rnd()%3==0) ed.handleEvent(ins('f'));
        else ed.handleEvent(e);
        assertConsistentBusq(ed);
    }
}
