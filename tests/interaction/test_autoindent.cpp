#include "test_support.h"

// FEATURE tabulación: Enter debe conservar la indentación de la fila actual
// en la línea nueva, y Backspace debe borrar la tabulación.
//
// Estos tests solo describen el comportamiento esperado. No tocan código
// de producción: deben FALLAR hasta implementar autoindent/smart-backspace.

TEST(autoindent_enter_conserva_tabulacion) {
    Editor ed;
    ed.active().document.restore({"    codigo", "segunda"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = ed.active().document.lineLength(0);
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);

    // La línea nueva debe heredar los 4 espacios y el cursor queda después.
    CHECK_EQ(ed.active().document.lineCount(), 3);
    CHECK_EQ(ed.active().document.lineAt(1), "    ");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 4);
}

TEST(autoindent_backspace_borra_tabulacion) {
    Editor ed;
    ed.active().document.restore({"    codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 4;  // justo después de la tabulación
    enterInteraccion(ed);

    press(ed, EventType::Backspace);

    // Un solo Backspace debe borrar el nivel completo de tabulación.
    CHECK_EQ(ed.active().document.lineAt(0), "codigo");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(autoindent_enter_sin_indent_no_copia) {
    // Guard contra implementación ingenua que copie los primeros 4 chars
    // ("codigo" -> "codi"). Sin indentación lógica, la línea nueva es vacía.
    Editor ed;
    ed.active().document.restore({"codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = ed.active().document.lineLength(0);
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);

    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "codigo");
    CHECK_EQ(ed.active().document.lineAt(1), "");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
}

TEST(autoindent_enter_conserva_tab_real) {
    // Maestro soporta '\t' como carácter (TAB_WIDTH=4 visual). El Enter
    // debe heredar ese '\t' real, no 4 espacios ni otra normalización.
    Editor ed;
    ed.active().document.restore({"\tcodigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = ed.active().document.lineLength(0);
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);

    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(1), "\t");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 1);
}

TEST(autoindent_enter_linea_solo_blancos_conserva_indent) {
    // Línea solo-blancos: indentación lógica = todo el contenido.
    // Enter conserva indent en ambas: original queda, nueva hereda.
    Editor ed;
    ed.active().document.restore({"    "});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 4;
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);

    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "    ");
    CHECK_EQ(ed.active().document.lineAt(1), "    ");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 4);
}

TEST(autoindent_backspace_borra_indent_parcial) {
    // Contrato indentación lógica (igual que '{' / previewIndentDelta con
    // indentLen=4): con solo 2 espacios, un Backspace tras el indent borra
    // esos 2, no 1 solo char ni erase(0, 4) a ciegas.
    // Ver referencia: navegacion_brace_dedent_partial_indent.
    Editor ed;
    ed.active().document.restore({"  codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 2;  // justo después del indent parcial
    enterInteraccion(ed);

    press(ed, EventType::Backspace);

    CHECK_EQ(ed.active().document.lineAt(0), "codigo");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}

// --- Historial: la entrada es Split[+Delete]+Insert y debe deshacerse/
// --- rehacerse como una sola operación (pila de edits en HistoryEntry).

TEST(autoindent_enter_undo_restores_original) {
    Editor ed;
    ed.active().document.restore({"    codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 10;  // fin de "    codigo"
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    press(ed, EventType::Undo);

    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "    codigo");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 10);
}

TEST(autoindent_enter_undo_redo_roundtrip) {
    Editor ed;
    ed.active().document.restore({"    codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 10;
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);
    press(ed, EventType::Undo);
    press(ed, EventType::Redo);

    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "    codigo");
    CHECK_EQ(ed.active().document.lineAt(1), "    ");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 4);
}

TEST(autoindent_enter_mid_code_undo_redo) {
    // Caso "    fo|o": ejercita Split + Insert (sufijo sin blancos).
    Editor ed;
    ed.active().document.restore({"    foo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 6;  // "    fo|o"
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.active().document.lineAt(0), "    fo");
    CHECK_EQ(ed.active().document.lineAt(1), "    o");
    CHECK_EQ(ed.active().cursor.col, 4);

    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "    foo");
    CHECK_EQ(ed.active().cursor.col, 6);

    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "    fo");
    CHECK_EQ(ed.active().document.lineAt(1), "    o");
    CHECK_EQ(ed.active().cursor.col, 4);
}

TEST(autoindent_enter_inside_indent_undo_redo) {
    // Corte dentro del indent: ejercita Split + Delete(k) + Insert
    // ("    codigo" col=2 -> sufijo "  codigo", k=2).
    Editor ed;
    ed.active().document.restore({"    codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 2;
    enterInteraccion(ed);

    press(ed, EventType::InsertNewline);
    CHECK_EQ(ed.active().document.lineAt(0), "  ");
    CHECK_EQ(ed.active().document.lineAt(1), "    codigo");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 4);

    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().document.lineAt(0), "    codigo");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 2);

    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "  ");
    CHECK_EQ(ed.active().document.lineAt(1), "    codigo");
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 4);
}

TEST(autoindent_backspace_undo_restores) {
    Editor ed;
    ed.active().document.restore({"    codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 4;
    enterInteraccion(ed);

    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "codigo");
    press(ed, EventType::Undo);

    CHECK_EQ(ed.active().document.lineAt(0), "    codigo");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 4);
}

TEST(autoindent_backspace_undo_redo) {
    Editor ed;
    ed.active().document.restore({"    codigo"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 4;
    enterInteraccion(ed);

    press(ed, EventType::Backspace);
    press(ed, EventType::Undo);
    press(ed, EventType::Redo);

    CHECK_EQ(ed.active().document.lineAt(0), "codigo");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
}
