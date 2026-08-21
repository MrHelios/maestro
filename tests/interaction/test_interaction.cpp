#include <string>
#include <vector>

#include "test_framework.h"

#include <string>
#include <vector>
#define private public
#include "ui/Editor.h"
#undef private

// ===========================================================================
// P0: INTERACTION TEST - Seleccion -> Delete -> Undo -> Redo
// ===========================================================================
// Proposito: un unico flujo redundante que se verifica DESPUES de CADA
// operacion y comprueba TODO el estado de un buffer:
//   document | cursor | anchor | selection | modified
//
// Escenario base:
//   "hello world"
//         ^^^^^   <- "world" seleccionado ([6, 11))
//   select "world" -> delete -> undo -> redo
//
//   initial: "hello world"  cursor(0,11)  sel[6,11)  modified=false
//   delete : "hello "       cursor(0,6)   sin sel     modified=true
//   undo   : "hello world"  cursor(0,11)  sel[6,11)  modified=false
//   redo   : "hello "       cursor(0,6)   sin sel     modified=true
//
// Se programa directo el estado de seleccion/anecla en el buffer (igual que
// editorOfLines en test_selection: la creacion de la seleccion por flechas
// ya esta cubierta alli). Aqui lo que importa es el COMPORTAMIENTO de la
// edicion Delete/Undo/Redo sobre una seleccion establecida.
// ---------------------------------------------------------------------------

static void press(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    ed.handleEvent(e);
}

static Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

// Prepara un editor con el documento dado, una seleccion [anchor, position]
// ya establecida (modo Seleccion, cursor en el extremo position) y marca el
// estado actual como "guardado" (modified=false) para poder verificar que
// modified solo cambia cuando difiere del guardado.
static void prepareScenario(Editor& ed,
                            const std::vector<std::string>& lines,
                            Position anchor,
                            Position position) {
    ed.active().document.restore(lines);
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    ed.active().selection = Selection{anchor, position};
    ed.active().cursor.line = position.line;
    ed.active().cursor.col = position.col;
    ed.state_ = State::Seleccion;
}

// Variante para pegado SIN seleccion: marca el documento como guardado y
// deja el cursor en `pos`, en Navegacion y sin seleccion.
static void preparePaste(Editor& ed,
                         const std::vector<std::string>& lines,
                         Position pos) {
    ed.active().document.restore(lines);
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    ed.active().selection.reset();
    ed.active().cursor.line = pos.line;
    ed.active().cursor.col = pos.col;
    ed.state_ = State::Navegacion;
}

// Verifica el estado COMPLETO del buffer:
//   lines | cursor(line,col) | hasSelection | anchor | position | modified.
// Cuando hasSelection es false, anchor/position se ignoran.
static void expectState(Editor& ed,
                        const std::vector<std::string>& lines,
                        Position cursor,
                        bool hasSel,
                        Position anchor,
                        Position position,
                        bool modified) {
    CHECK(ed.active().document.snapshot() == lines);
    CHECK_EQ(ed.active().cursor.line, cursor.line);
    CHECK_EQ(ed.active().cursor.col, cursor.col);
    CHECK_EQ(ed.active().modified, modified);
    if (!hasSel) {
        CHECK(!ed.hasSelection());
    } else {
        CHECK(ed.hasSelection());
        CHECK(ed.active().selection.has_value());
        CHECK(ed.active().selection->anchor == anchor);
        CHECK(ed.active().selection->position == position);
    }
}

// El cuerpo de cada caso: con una seleccion [a, p] sobre `lines`, ejecuta
// select -> delete -> undo -> redo verificando el estado lleno en cada paso.
static void runCase(
                    const std::vector<std::string>& lines,
                    Position a,
                    Position p,
                    const std::vector<std::string>& afterDelete,
                    Position cursorAfterDelete) {
    // selection vigente antes de borrar (estado de partida).
    // DELETE --------------------------------------------------------
    {
        Editor ed;
        prepareScenario(ed, lines, a, p);
        expectState(ed, lines, p, true, a, p, false); // inicial

        press(ed, EventType::Delete);
        expectState(ed, afterDelete, cursorAfterDelete, false,
                    {0, 0}, {0, 0}, true);             // despues de delete

        press(ed, EventType::Undo);
        // undo restaura seleccion y cursor originales; modified vuelve al
        // guardado (indiferente respecto a savedLines).
        expectState(ed, lines, p, true, a, p, false);  // despues de undo

        press(ed, EventType::Redo);
        expectState(ed, afterDelete, cursorAfterDelete, false,
                    {0, 0}, {0, 0}, true);             // despues de redo
    }
}

// La misma bateria con Backspace (comportamiento identico al Delete).
static void runCaseBackspace(
                             const std::vector<std::string>& lines,
                             Position a,
                             Position p,
                             const std::vector<std::string>& afterDelete,
                             Position cursorAfterDelete) {
    {
        Editor ed;
        prepareScenario(ed, lines, a, p);
        expectState(ed, lines, p, true, a, p, false);

        press(ed, EventType::Backspace);
        expectState(ed, afterDelete, cursorAfterDelete, false,
                    {0, 0}, {0, 0}, true);

        press(ed, EventType::Undo);
        expectState(ed, lines, p, true, a, p, false);

        press(ed, EventType::Redo);
        expectState(ed, afterDelete, cursorAfterDelete, false,
                    {0, 0}, {0, 0}, true);
    }
}

// ---------------------------------------------------------------------------
// P0 v2: Seleccion -> Cut ('x') -> Undo -> Redo, con CLIPBOARD.
// El corte copia el rango al portapapeles y borra el texto (como Delete,
// pero dejando el contenido cortado). El clipboard es estado GLOBAL de la
// UI y NO participa del undo/redo: deshacer el corte restaura documento,
// cursor y seleccion, pero el portapapeles CONSERVA lo cortado.
// ---------------------------------------------------------------------------
static void runCutCase(
    const std::vector<std::string>& lines,
    Position a,
    Position p,
    const std::vector<std::string>& afterCut,
    Position cursorAfterCut,
    const std::vector<std::string>& clipboard) {
    Editor ed;
    prepareScenario(ed, lines, a, p);
    expectState(ed, lines, p, true, a, p, false);   // inicial
    CHECK(ed.getClipboardBlock().empty());                    // sin portapapeles previo

    ed.handleEvent(insert('x'));                     // cortar
    expectState(ed, afterCut, cursorAfterCut, false,
                {0, 0}, {0, 0}, true);               // despues de cut
    CHECK(ed.getClipboardBlock() == clipboard);

    press(ed, EventType::Undo);
    // undo restaura documento, cursor y seleccion; modified vuelve al
    // guardado; el clipboard NO se deshace y conserva lo cortado.
    expectState(ed, lines, p, true, a, p, false);    // despues de undo
    CHECK(ed.getClipboardBlock() == clipboard);

    press(ed, EventType::Redo);
    // redo reproduce EXACTAMENTE el estado tras el corte.
    expectState(ed, afterCut, cursorAfterCut, false,
                {0, 0}, {0, 0}, true);               // despues de redo
    CHECK(ed.getClipboardBlock() == clipboard);
}

// ---------------------------------------------------------------------------
// Los casos pedidos (P0):
//   seleccion de un caracter / parcial / hasta EOF / desde BOF /
//   multilinea / lineas vacias / completa.
// ---------------------------------------------------------------------------
TEST(interaction_delete_selection_single_char) {
    runCase(
            {"hello world"},
            {0, 6}, {0, 7},            // [6,7): la 'w'
            {"hello orld"}, {0, 6});
}

TEST(interaction_delete_selection_partial) {
    runCase(
            {"hello world"},
            {0, 6}, {0, 11},           // [6,11): "world"
            {"hello "}, {0, 6});
}

TEST(interaction_delete_selection_to_eof) {
    // Desde el medio de la linea 0 hasta el final del archivo.
    runCase(
            {"hello", "world"},
            {0, 3}, {1, 5},            // [0,3)..(1,5): "lo" + "\n" + "world"
            {"hel"}, {0, 3});
}

TEST(interaction_delete_selection_from_bof) {
    runCase(
            {"hello"},
            {0, 0}, {0, 3},            // [0,3): "hel"
            {"lo"}, {0, 0});
}

TEST(interaction_delete_selection_multiline) {
    runCase(
            {"aaa", "bbb", "ccc"},
            {0, 0}, {2, 2},            // "aaa"+"\n"+"bbb"+"\n"+"cc"
            {"c"}, {0, 0});
}

TEST(interaction_delete_selection_empty_lines) {
    // La seleccion abarca dos lineas vacias [1] y [2].
    runCase(
            {"a", "", "", "b"},
            {0, 1}, {3, 1},            // "a"(final).."b"(final)
            {"a"}, {0, 1});
}

TEST(interaction_delete_selection_complete_document) {
    runCase(
            {"hola", "mundo"},
            {0, 0}, {1, 5},            // [0,0]..(1,5): todo el documento
            {""}, {0, 0});
}

// ---------------------------------------------------------------------------
// Backspace sobre una seleccion: identico al Delete (P0).
// ---------------------------------------------------------------------------
TEST(interaction_backspace_selection_partial) {
    runCaseBackspace(
                     {"hello world"},
                     {0, 6}, {0, 11},
                     {"hello "}, {0, 6});
}

TEST(interaction_backspace_selection_multiline) {
    runCaseBackspace(
                     {"aaa", "bbb", "ccc"},
                     {0, 0}, {2, 2},
                     {"c"}, {0, 0});
}

// ---------------------------------------------------------------------------
// Paridad Delete/Backspace: sobre la MISMA seleccion, ambas teclas deben
// producir EXACTAMENTE el mismo documento (y cursor) finales, y el undo de
// cada una debe restaurar por completo texto + cursor + seleccion.
// ---------------------------------------------------------------------------
TEST(interaction_delete_and_backspace_produce_same_result) {
    struct Case {
        std::vector<std::string> lines;
        Position a, p;
    };
    const Case cases[] = {
        {{"hello world"}, {0, 6}, {0, 11}},          // "world"
        {{"hello world"}, {0, 6}, {0, 7}},           // un caracter
        {{"uno", "dos", "tres"}, {0, 0}, {2, 4}},    // multilinea
        {{"a", "", "", "b"}, {0, 1}, {3, 1}},        // lineas vacias
        {{"hola", "mundo"}, {0, 0}, {1, 5}},         // documento entero
    };
    for (const Case& c : cases) {
        // DELETE
        Editor edDel;
        prepareScenario(edDel, c.lines, c.a, c.p);
        press(edDel, EventType::Delete);
        // BACKSPACE
        Editor edBs;
        prepareScenario(edBs, c.lines, c.a, c.p);
        press(edBs, EventType::Backspace);

        // Mismo documento y misma posicion de cursor finales.
        CHECK(edDel.active().document.snapshot() == edBs.active().document.snapshot());
        CHECK_EQ(edDel.active().cursor.line, edBs.active().cursor.line);
        CHECK_EQ(edDel.active().cursor.col, edBs.active().cursor.col);

        // undo restaura todo por igual en ambas.
        press(edDel, EventType::Undo);
        press(edBs, EventType::Undo);
        CHECK(edDel.active().document.snapshot() == c.lines);
        CHECK(edBs.active().document.snapshot() == c.lines);
        CHECK(edDel.active().cursor.line == c.p.line);
        CHECK(edDel.active().cursor.col == c.p.col);
        CHECK(edBs.active().cursor.line == c.p.line);
        CHECK(edBs.active().cursor.col == c.p.col);
        CHECK(edDel.hasSelection());
        CHECK(edBs.hasSelection());
        CHECK(edDel.active().selection->anchor == c.a);
        CHECK(edDel.active().selection->position == c.p);
        CHECK(edBs.active().selection->anchor == c.a);
        CHECK(edBs.active().selection->position == c.p);
    }
}

// ---------------------------------------------------------------------------
// Inconsistencias claves del contrato:
// ---------------------------------------------------------------------------

// 1) Delete sin seleccion sigue borrando UN caracter (comportamiento previo
//    intacto): no regresion al activar la seleccion.
TEST(interaction_delete_without_selection_still_single_char) {
    Editor ed;
    ed.active().document.restore({"abc"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1;
    ed.state_ = State::Interaccion;

    press(ed, EventType::Delete); // borra 'b' en (0,1)

    CHECK_EQ(ed.active().document.lineAt(0), "ac");
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK(ed.active().modified);
    CHECK(!ed.hasSelection());
}

// 2) Undo/Redo del borrado de seleccion: la seleccion se restaura fiel al
//    momento en que se borro (no se pierde el rango al deshacer).
TEST(interaction_delete_selection_undo_restores_selection) {
    Editor ed;
    prepareScenario(ed, {"hello world"}, {0, 6}, {0, 11});

    press(ed, EventType::Delete);
    CHECK(!ed.hasSelection());

    press(ed, EventType::Undo);
    CHECK(ed.hasSelection());
    CHECK((ed.active().selection->anchor == Position{0, 6}));
    CHECK((ed.active().selection->position == Position{0, 11}));

    press(ed, EventType::Redo);
    CHECK(!ed.hasSelection());
}

// 3) En Interaccion una seleccion vigente tambien se borra con Delete (no
//    solo desde el modo Seleccion) y NO se sale del modo de edicion.
TEST(interaction_delete_selection_from_interaccion) {
    Editor ed;
    ed.active().document.restore({"hello world"});
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    ed.active().selection = Selection{{0, 6}, {0, 11}};
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 11;
    ed.state_ = State::Interaccion;

    press(ed, EventType::Delete);

    CHECK_EQ(ed.active().document.lineAt(0), "hello ");
    CHECK_EQ(ed.active().cursor.col, 6);
    CHECK(ed.active().modified);
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));

    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "hello world");
    CHECK(ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
}

// ---------------------------------------------------------------------------
// P0 v2: Selection -> Cut -> Undo -> Redo (con clipboard).
// ---------------------------------------------------------------------------
TEST(interaction_cut_selection_partial) {
    runCutCase(
            {"hello world"},
            {0, 6}, {0, 11},          // [6,11): "world"
            {"hello "}, {0, 6},       // despues del corte
            {"world"});               // clipboard
}

TEST(interaction_cut_selection_multiline) {
    runCutCase(
            {"aaa", "bbb", "ccc"},
            {0, 0}, {2, 2},           // [0,0)..(2,2)
            {"c"}, {0, 0},
            {"aaa", "bbb", "cc"});    // clipboard (cola de sl, media, cabeza de el)
}

TEST(interaction_cut_selection_complete_document) {
    runCutCase(
            {"hola", "mundo"},
            {0, 0}, {1, 5},           // todo el documento
            {""}, {0, 0},
            {"hola", "mundo"});       // clipboard = archivo entero
}

// ---------------------------------------------------------------------------
// P0 v3: Selection -> Paste -> Undo (con clipboard).
// El pegado inserta el clipboard y, si hay una seleccion, la REPLAZA.
// Undo debe eliminar exactamente el texto insertado y restaurar ademas
// cursor y seleccion (no solamente el texto). El clipboard se conserva.
// ---------------------------------------------------------------------------
static void runPasteCase(
    const std::vector<std::string>& lines,
    Position cursorPos,
    const std::vector<std::string>& clipboard,
    const std::vector<std::string>& afterPaste,
    Position cursorAfterPaste) {
    Editor ed;
    preparePaste(ed, lines, cursorPos);
    ed.setClipboardBlock(clipboard);
    expectState(ed, lines, cursorPos, false, {0, 0}, {0, 0}, false); // inicial
    CHECK(ed.getClipboardBlock() == clipboard);

    ed.handleEvent(insert('p'));              // pegar en el cursor
    expectState(ed, afterPaste, cursorAfterPaste, false,
                {0, 0}, {0, 0}, true);        // despues de paste
    CHECK(ed.getClipboardBlock() == clipboard);        // clipboard intacto

    press(ed, EventType::Undo);
    // undo elimina EXACTAMENTE el texto insertado y restaura cursor;
    // modified vuelve al guardado; no habia seleccion que restaurar.
    expectState(ed, lines, cursorPos, false,
                {0, 0}, {0, 0}, false);       // despues de undo
    CHECK(ed.getClipboardBlock() == clipboard);
}

// Peatado REPLAZANDO una seleccion activa: undo debe restaurar ademas el
// rango seleccionado (seleccion restaurada).
static void runPasteReplaceCase(
    const std::vector<std::string>& lines,
    Position a,
    Position p,
    const std::vector<std::string>& clipboard,
    const std::vector<std::string>& afterPaste,
    Position cursorAfterPaste) {
    Editor ed;
    prepareScenario(ed, lines, a, p);         // seleccion [a,p], cursor=p, Seleccion
    ed.setClipboardBlock(clipboard);
    expectState(ed, lines, p, true, a, p, false);  // inicial
    CHECK(ed.getClipboardBlock() == clipboard);

    ed.handleEvent(insert('p'));              // reemplaza la seleccion
    expectState(ed, afterPaste, cursorAfterPaste, false,
                {0, 0}, {0, 0}, true);        // despues de paste
    CHECK(ed.getClipboardBlock() == clipboard);

    press(ed, EventType::Undo);
    // restaure documento, cursor y SELECCION; modified vuelve al guardado.
    expectState(ed, lines, p, true, a, p, false);    // despues de undo
    CHECK(ed.getClipboardBlock() == clipboard);
}

TEST(interaction_paste_at_cursor_then_undo) {
    // El caso base del enunciado: "abc" ^ -> paste "XYZ" -> "abcXYZ",
    // undo -> exactamente "abc" (texto + cursor, sin seleccion residual).
    runPasteCase(
            {"abc"},
            {0, 3},                    // cursor al final
            {"XYZ"},
            {"abcXYZ"}, {0, 6});       // despues del paste
}

TEST(interaction_paste_at_start_then_undo) {
    runPasteCase(
            {"abc"},
            {0, 0},                    // cursor al inicio
            {"XYZ"},
            {"XYZabc"}, {0, 3});
}

TEST(interaction_paste_replaces_selection_then_undo) {
    // select "world" -> paste "XYZ" reemplaza el rango; undo restaura
    // exactamente "hello world" y la seleccion [6,11).
    runPasteReplaceCase(
            {"hello world"},
            {0, 6}, {0, 11},           // "world" seleccionado
            {"XYZ"},
            {"hello XYZ"}, {0, 9});    // despues del paste
}

// ---------------------------------------------------------------------------
// P0 v4: Seleccion -> escribir sobre el rango -> Undo (UNA operacion).
// La decision arquitectonica mas importante de la interaccion: escribir
// letras sobre una seleccion activa la REEMPLAZA (no la borra ni la ignora)
// y entra a Interaccion. Toda la escritura consecutiva que sigue se agrupa
// en la MISMA entrada de undo: el "reemplazo + teclado" se deshace entero
// con UN solo Ctrl+U (no caracter a caracter).
// ---------------------------------------------------------------------------
static void runReplaceTypeCase(const std::vector<std::string>& lines,
                               Position a,
                               Position p,
                               const std::string& typed,
                               const std::vector<std::string>& afterType,
                               Position cursorAfterType) {
    Editor ed;
    prepareScenario(ed, lines, a, p);
    expectState(ed, lines, p, true, a, p, false);   // inicial: sel, Seleccion
    const size_t undoBefore = ed.active().undoStack.size();

    // Escribir `typed`: el primer caracter reemplaza el rango; el resto se
    // continua en Interaccion coalescido dentro de la misma edicion.
    for (char c : typed)
        ed.handleEvent(insert(c));
    expectState(ed, afterType, cursorAfterType, false,
                {0, 0}, {0, 0}, true);             // despues de escribir

    // Toda la escritura fue UNA sola edicion: una unica entrada de undo.
    CHECK_EQ(ed.active().undoStack.size(), undoBefore + 1);

    press(ed, EventType::Undo);
    // UN solo undo devuelve exactamente texto + cursor + seleccion previos
    // (y modified vuelve al guardado).
    expectState(ed, lines, p, true, a, p, false);  // despues de undo
}

TEST(interaction_type_over_selection_replace_undo_one_operation) {
    // El caso del enunciado:
    //   "hello world", "world"=[6,11) seleccionado, escribir "Maestro" ->
    //   "hello Maestro"; un solo undo -> "hello world" (texto + cursor +
    //   seleccion), no "hello Maestr" ni caracter a caracter.
    runReplaceTypeCase(
            {"hello world"},
            {0, 6}, {0, 11},           // "world" seleccionado
            "Maestro",
            {"hello Maestro"}, {0, 13});   // despues de escribir
}

TEST(interaction_type_over_multiline_selection_replace_undo_one_operation) {
    // Reemplazo spanning varias lineas: "dos\ntres" (de (1,0) a (2,4)) se
    // reemplaza por "X" dejando "uno\nX\ncuatro". Un solo undo debe
    // reconstruir EXACTAMENTE "uno\ndos\ntres\ncuatro" con cursor y
    // seleccion restaurados. Ejercita a la vez Document (deleteRange
    // multilinea + newline), Selection, Cursor y el historial de undo.
    runReplaceTypeCase(
            {"uno", "dos", "tres", "cuatro"},
            {1, 0}, {2, 4},            // selecciona "dos\ntres"
            "X",
            {"uno", "X", "cuatro"}, {1, 1});   // despues del reemplazo
}

TEST(interaction_cut_selection_utf8_undo_redo) {
    // UTF-8 multibyte: 'é' = 2 bytes, '—' = 3 bytes, '😀' = 4 bytes. Las
    // coordenadas de Position.col son OFFSETS DE BYTES. Seleccion [3,22) =
    // "é — mañana 😀"; cut la quita dejando "caf", undo debe restaurar los
    // MISMOS bytes (longitud byte a byte identica) y redo volver al corte.
    const std::string line =
        "caf" "\xC3\xA9" " " "\xE2\x80\x94" " " "ma" "\xC3\xB1" "ana"
        " " "\xF0\x9F\x98\x80"; // "café — mañana 😀" (22 bytes)
    const std::string selText =
        "\xC3\xA9" " " "\xE2\x80\x94" " " "ma" "\xC3\xB1" "ana"
        " " "\xF0\x9F\x98\x80"; // "é — mañana 😀" (19 bytes)

    Editor ed;
    prepareScenario(ed, {line}, {0, 3}, {0, 22});
    expectState(ed, {line}, {0, 22}, true, {0, 3}, {0, 22}, false);
    CHECK(ed.getClipboardBlock().empty());

    ed.handleEvent(insert('x'));                  // cortar "é — mañana 😀"
    expectState(ed, {"caf"}, {0, 3}, false, {0, 0}, {0, 0}, true);
    CHECK(ed.getClipboardBlock() == (std::vector<std::string>{selText}));
    CHECK(ed.active().document.lineAt(0) == std::string("caf"));

    press(ed, EventType::Undo);
    // Verificar que los bytes quedaron EXACTAMENTE iguales (no solo el
    // contenido "logico"): misma longitud byte a byte y mismo contenido.
    expectState(ed, {line}, {0, 22}, true, {0, 3}, {0, 22}, false);
    CHECK(ed.active().document.lineAt(0) == line);
    CHECK(ed.active().document.lineAt(0).size() == 22u);   // cuenta de bytes
    CHECK(ed.getClipboardBlock() == (std::vector<std::string>{selText})); // conservado

    press(ed, EventType::Redo);
    expectState(ed, {"caf"}, {0, 3}, false, {0, 0}, {0, 0}, true);
    CHECK(ed.active().document.lineAt(0) == std::string("caf"));
    CHECK(ed.active().document.lineAt(0).size() == 3u);
}

// ===========================================================================
// P0: UTF-8 + cursor + seleccion.
// Invariante: NINGUNA combinacion de Cursor + Selection (movimientos Right/
// Left sobre codepoints multibyte) puede dejar el cursor DENTRO de un
// codepoint UTF-8. Como Position.col son OFFSETS DE BYTE, un cursor esta en
// borde de codepoint si el byte que apunta NO es un byte de continuacion
// (0x80-0xBF): los ASCII y los bytes lead de multibyte quedan fuera de ese
// rango. Verificar esa condicion tras CADA movimiento detecta fallos de
// mover cursor por BYTES individuales en lugar de por celdas completas.
// ---------------------------------------------------------------------------
static void assertCursorOnCodepointBoundary(Editor& ed) {
    const int line = ed.active().cursor.line;
    const int col = ed.active().cursor.col;
    CHECK(col >= 0);
    CHECK(col <= ed.active().document.lineLength(line));
    if (col >= 0 && col < ed.active().document.lineLength(line)) {
        const unsigned char b =
            static_cast<unsigned char>(ed.active().document.lineAt(line)[col]);
        CHECK(!(b >= 0x80 && b <= 0xBF));   // no puede ser byte de continuacion
    }
}

// Una posicion (linea,col) esta en borde de codepoint si el byte que apunta
// no es un byte de continuacion UTF-8 (0x80-0xBF).
static void assertPositionOnCodepointBoundary(Editor& ed, Position p) {
    const int len = ed.active().document.lineLength(p.line);
    CHECK(p.col >= 0);
    CHECK(p.col <= len);
    if (p.col >= 0 && p.col < len) {
        const unsigned char b =
            static_cast<unsigned char>(ed.active().document.lineAt(p.line)[p.col]);
        CHECK(!(b >= 0x80 && b <= 0xBF));
    }
}

static void setCursor(Editor& ed, int line, int col) {
    ed.active().cursor.line = line;
    ed.active().cursor.col = col;
}

// Recorre la linea entera con MoveRight hasta el fin y luego MoveLeft hasta
// el inicio, verificando tras CADA paso que el cursor quede en borde de
// codepoint (nunca dentro de uno).
static void runCursorUtf8Tour(const std::string& line, int startCol) {
    Editor ed;
    ed.active().document.restore({line});
    setCursor(ed, 0, startCol);
    const int len = ed.active().document.lineLength(0);
    assertCursorOnCodepointBoundary(ed);

    while (ed.active().cursor.col < len) {
        press(ed, EventType::MoveRight);
        assertCursorOnCodepointBoundary(ed);
    }
    while (ed.active().cursor.col > 0) {
        press(ed, EventType::MoveLeft);
        assertCursorOnCodepointBoundary(ed);
    }
}

TEST(interaction_cursor_utf8_never_inside_codepoint_full_tour) {
    // "abc café — 😀 xyz": contiene 'é' (2 bytes), '—' (3) y '😀' (4).
    // Recorrer toda la linea en ambas direcciones debe quedar siempre en
    // borde de codepoint.
    const std::string line =
        "abc" " " "caf" "\xC3\xA9" " " "\xE2\x80\x94" " "
        "\xF0\x9F\x98\x80" " " "xyz";
    runCursorUtf8Tour(line, 0);
}

TEST(interaction_cursor_utf8_ascii_to_utf8) {
    // Mover de ASCII ('f') hacia dentro de un multibyte: cae en el INICIO
    // del 'é' (col 7), no dentro de sus 2 bytes (7,8).
    const std::string line = "abc caf\xC3\xA9";
    Editor ed;
    ed.active().document.restore({line});
    setCursor(ed, 0, 6);                 // despues de la 'f', antes del 'é'
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.active().cursor.col, 7); // inicio del 'é'
    assertCursorOnCodepointBoundary(ed);
}

TEST(interaction_cursor_utf8_utf8_to_ascii) {
    // Desde el inicio del 'é' (2 bytes) al mover a la derecha se cruza el
    // codepoint completo y se cae en el ASCII siguiente (col 9, el espacio).
    const std::string line = "abc caf\xC3\xA9 xyz";
    Editor ed;
    ed.active().document.restore({line});
    setCursor(ed, 0, 7);                 // inicio del 'é'
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.active().cursor.col, 9); // salto 2 bytes, borde en el espacio
    assertCursorOnCodepointBoundary(ed);
}

TEST(interaction_cursor_utf8_utf8_to_utf8) {
    // Dos multibyte adyacentes: 'é' (2 bytes) seguido de '😀' (4 bytes).
    // Mover a la derecha cruza el 'é' entero y cae en el inicio del '😀'.
    const std::string line = "\xC3\xA9" "\xF0\x9F\x98\x80" "z";
    Editor ed;
    ed.active().document.restore({line});
    setCursor(ed, 0, 0);                 // inicio del 'é'
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.active().cursor.col, 2); // inicio del '😀' (0 + 2 bytes)
    assertCursorOnCodepointBoundary(ed);
}

TEST(interaction_cursor_utf8_emoji_to_ascii) {
    // Desde el inicio de '😀' (4 bytes), a la derecha se salta el emoji
    // completo y se cae en el ASCII siguiente ('x').
    const std::string line = "abc \xF0\x9F\x98\x80" "xyz";
    Editor ed;
    ed.active().document.restore({line});
    setCursor(ed, 0, 4);                 // inicio del '😀' (despues de "abc ")
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.active().cursor.col, 8); // tras los 4 bytes del '😀'
    assertCursorOnCodepointBoundary(ed);
}

TEST(interaction_cursor_utf8_ascii_to_emoji) {
    // Desde un ASCII (espacio), a la derecha se cae en el INICIO del emoji,
    // nunca dentro.
    const std::string line = "abc \xF0\x9F\x98\x80" "xyz";
    Editor ed;
    ed.active().document.restore({line});
    setCursor(ed, 0, 3);                 // el espacio antes del '😀'
    press(ed, EventType::MoveRight);
    CHECK_EQ(ed.active().cursor.col, 4); // inicio del '😀'
    assertCursorOnCodepointBoundary(ed);
}

TEST(interaction_selection_utf8_boundaries_never_inside_codepoint) {
    // Con seleccion activa, el desplazamiento estira el rango y sincroniza
    // selection.position con el cursor; ambos deben quedar siempre en borde
    // de codepoint (nunca dentro), en ambas direcciones.
    const std::string line = "abc caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x98\x80" "xyz";
    Editor ed;
    ed.active().document.restore({line});
    ed.state_ = State::Seleccion;
    setCursor(ed, 0, 0);
    ed.active().selection = Selection{{0, 0}, {0, 0}};
    // Seleccion inicial degenerada (sin rango); el primer MoveRight la
    // convierte en un rango real (anchor != position).

    // Hacia la derecha hasta el fin de la linea: ambos extremos del rango
    // (anchor y position) y el cursor deben quedar en borde de codepoint.
    while (ed.active().cursor.col < ed.active().document.lineLength(0)) {
        press(ed, EventType::MoveRight);
        assertCursorOnCodepointBoundary(ed);
        if (ed.active().selection.has_value()) {
            assertPositionOnCodepointBoundary(ed,
                ed.active().selection->position);
            assertPositionOnCodepointBoundary(ed,
                ed.active().selection->anchor);
        }
    }
    // Hacia la izquierda hasta el inicio.
    while (ed.active().cursor.col > 0) {
        press(ed, EventType::MoveLeft);
        assertCursorOnCodepointBoundary(ed);
        if (ed.active().selection.has_value())
            assertPositionOnCodepointBoundary(ed, ed.active().selection->position);
    }
}

// ===========================================================================
// P0: Save -> Edit A -> Undo -> Edit B  (branching history).
// Tras un undo, hacer una EDICION NUEVA descarta la rama que quedaba por
// rehacer: el historial de redo anterior debe desaparecer y el documento
// queda marcado como modificado. Deteccion del error clasico de Branching
// History (que la rama deshecha siga viva para "saltar" a un estado obsoleto).
// ---------------------------------------------------------------------------
TEST(interaction_save_edit_undo_edit_b_clears_redo) {
    Editor ed;
    ed.active().document.restore({"hola"});      // documento guardado
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;                // estado "despues de save"
    // restore deja el cursor en BOF; lo movemos al final de la linea para
    // que la edicion anexe en lugar de anteponer.
    ed.active().cursor.col = ed.active().document.lineLength(0);

    ed.handleEvent(insert('i'));                 // -> Interaccion
    ed.handleEvent(insert('A'));                 // Edit A: "hola" -> "holaA"
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "holaA");

    press(ed, EventType::Undo);                  // -> "hola" guardado
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK(!ed.active().modified);
    CHECK(ed.active().redoStack.size() > 0);     // la edicion A quedo rehacible

    ed.handleEvent(insert('i'));                 // -> Interaccion de nuevo
    ed.handleEvent(insert('B'));                 // Edit B (nueva rama)
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "holaB");
    CHECK(ed.active().redoStack.empty());        // la rama de A MURIO
}

// ===========================================================================
// P0: Save -> Edit -> Save -> Undo.
//   A --save--> A --edit--> B --save--> B --undo--> A : modified == TRUE.
// El undo vuelve a un estado ANTERIOR al ultimo save (B), asi que el doc
// actual ya no coincide con lo guardado. Demuestra que el estado "saved"
// (savedLines) y el historial de undo son dos conceptos INDEPENDIENTES: el
// save ancla lo que se considera "no modificado", no el tope del historial.
// ---------------------------------------------------------------------------
TEST(interaction_save_edit_save_undo_modified_true) {
    Editor ed;
    ed.active().document.restore({"A"});
    ed.active().savedLines = ed.active().document.snapshot(); // save(A)
    ed.active().modified = false;

    // edit -> "AB"
    ed.active().cursor.col = ed.active().document.lineLength(0);
    ed.handleEvent(insert('i'));             // -> Interaccion
    ed.handleEvent(insert('B'));
    CHECK_EQ(ed.active().document.lineAt(0), "AB");
    CHECK(ed.active().modified);

    // save -> savedLines se ancla a "AB"; modified = false
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    CHECK(!ed.active().modified);

    // undo -> vuelve a "A": el estado actual queda ANTERIOR al ultimo save.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "A");
    CHECK(ed.active().document.snapshot() != ed.active().savedLines);
    CHECK(ed.active().modified);             // A != ultimo save (B)
}

// ===========================================================================
// P0: Save -> Edit -> Save -> Undo -> Redo.
//   A --save--> A --edit--> B --save--> B --undo--> A --redo--> B
// Debe terminar: document == B, modified == false, porque el estado final
// (B) coincide EXACTAMENTE con el ultimo guardado (savedLines == B).
// Excelente para validar la relacion historia + saved state + modified: el
// save ancla el "no modificado" y undo/redo solo re-calculan modified contra
// ese ancla, sin mostrarse como modificado si se re-alcanza el guardado.
// ---------------------------------------------------------------------------
TEST(interaction_save_edit_save_undo_redo_modified_false) {
    Editor ed;
    ed.active().document.restore({"A"});
    ed.active().savedLines = ed.active().document.snapshot(); // save(A)
    ed.active().modified = false;

    // edit: "A" -> "AB"
    ed.active().cursor.col = ed.active().document.lineLength(0);
    ed.handleEvent(insert('i'));             // -> Interaccion
    ed.handleEvent(insert('B'));
    CHECK_EQ(ed.active().document.lineAt(0), "AB");

    // save(B): savedLines se ancla a "AB"
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;

    press(ed, EventType::Undo);              // -> "A" (anterior al save B)
    CHECK_EQ(ed.active().document.lineAt(0), "A");

    press(ed, EventType::Redo);              // -> vuelve a "AB" (el save B)
    CHECK_EQ(ed.active().document.lineAt(0), "AB");
    // El estado final coincide con el ultimo guardado -> NO modificado.
    CHECK(ed.active().document.snapshot() == ed.active().savedLines);
    CHECK(!ed.active().modified);
}

// Escribe `c` al final de la linea del buffer activo (entrando a Interaccion
// si hace falta).
static void appendChar(Editor& ed, char c) {
    if (ed.state_ != State::Interaccion)
        ed.handleEvent(insert('i'));
    ed.active().cursor.col = ed.active().document.lineLength(0);
    ed.handleEvent(insert(c));
}

// ===========================================================================
// P0: Buffers independientes (BufferManager no mezcla estados).
// A="hello", B="world". Editar A->, B->, volver a A->; cada buffer conserva
// su propio contenido e HISTORIAL sin contaminarse con el otro.
// ---------------------------------------------------------------------------
TEST(interaction_buffers_do_not_mix_edit_states) {
    Editor ed;
    // Buffer A (indice 0) = "hello"
    ed.active().document.restore({"hello"});
    CHECK_EQ(ed.active().document.lineAt(0), "hello");

    // Buffer B (indice 1) = "world"  (se crea y queda activo)
    ed.createBuffer();
    CHECK(ed.buffers.count() >= 2);
    ed.active().document.restore({"world"});
    CHECK_EQ(ed.active().document.lineAt(0), "world");

    // B -> "world?"
    appendChar(ed, '?');
    CHECK_EQ(ed.active().document.lineAt(0), "world?");

    // volver a A -> "hello" -> "hello!!"
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "hello");
    appendChar(ed, '!');
    appendChar(ed, '!');
    CHECK_EQ(ed.active().document.lineAt(0), "hello!!");

    // Al volver, cada buffer conserva su estado.
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().document.lineAt(0), "world?");
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "hello!!");

    // El HISTORIAL tambien es independiente: deshacer A (-1 '!') y B (-'?')
    // no se "ven" entre si.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "hello!");   // A
    ed.activateBuffer(1);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "world");    // B
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "hello!");   // A intacto
}

// ===========================================================================
// P0: Buffers con seleccion INDEPENDIENTE.
// La seleccion (anchor + cursor + position) vive en CADA Buffer, no en el
// Editor. Cambiar a otro buffer y hacer otra seleccion no debe tocar la de
// A; al volver, A conserva exactamente anchor, cursor y selection.
// ---------------------------------------------------------------------------
TEST(interaction_buffers_independent_selection) {
    Editor ed;
    // A: "hello world", seleccion "world" -> anchor{0,6}, cursor{0,11}
    ed.active().document.restore({"hello world"});
    ed.active().selection = Selection{{0, 6}, {0, 11}};
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 11;
    ed.state_ = State::Seleccion;

    // B: "abcdef", seleccion propia "bcd" -> anchor{0,1}, cursor{0,4}
    ed.createBuffer();
    CHECK(ed.buffers.count() >= 2);
    ed.active().document.restore({"abcdef"});
    ed.active().selection = Selection{{0, 1}, {0, 4}};
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 4;
    ed.state_ = State::Seleccion;

    // Volver a A: conserva EXACTAMENTE su anchor/cursor/seleccion.
    ed.activateBuffer(0);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());
    CHECK(ed.active().selection.has_value());
    CHECK(ed.active().selection->anchor == (Position{0, 6}));
    CHECK(ed.active().selection->position == (Position{0, 11}));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 11);
    CHECK(ed.active().document.lineAt(0) == "hello world");

    // B conserva su seleccion, independiente de la de A.
    ed.activateBuffer(1);
    CHECK(ed.active().selection.has_value());
    CHECK(ed.active().selection->anchor == (Position{0, 1}));
    CHECK(ed.active().selection->position == (Position{0, 4}));
    CHECK_EQ(ed.active().cursor.col, 4);
    CHECK(ed.active().document.lineAt(0) == "abcdef");

    // A sigue intacta al volver una vez mas.
    ed.activateBuffer(0);
    CHECK(ed.active().selection->anchor == (Position{0, 6}));
    CHECK(ed.active().selection->position == (Position{0, 11}));
}

// ===========================================================================
// P0/P1: Buffers con viewport INDEPENDIENTE.
// El viewport.top vive en cada Buffer. Scroll a la linea 800 en A, cambiar
// a B (10 lineas) y volver a A: el viewport de A debe seguir donde estaba
// (aprox. 800), NO volver a la linea 0. Verifica que activar un buffer no
// reinicia el scroll del otro.
// ---------------------------------------------------------------------------
TEST(interaction_buffers_independent_viewport) {
    Editor ed;

    // A: 1000 lineas, con scroll hasta la linea 800.
    ed.active().document.restore(std::vector<std::string>(1000, "linea"));
    ed.active().viewport.top = 800;
    ed.active().viewport.height = 24;
    ed.active().cursor.line = 800;
    ed.active().cursor.col = 0;
    ed.active().viewport.scrollToCursor(ed.active().cursor);
    CHECK_EQ(ed.active().viewport.top, 800);

    // B: 10 lineas (cabe entero: top 0).
    ed.createBuffer();
    ed.active().document.restore(std::vector<std::string>(10, "b"));
    CHECK_EQ(ed.active().document.lineCount(), 10);

    // Volver a A: el viewport debe conservar aprox. la linea 800.
    ed.activateBuffer(0);
    CHECK(ed.active().viewport.top >= 700);    // no volvio al principio
    CHECK(ed.active().viewport.top < 900);
    CHECK_EQ(ed.active().document.lineCount(), 1000);
}

// ===========================================================================
// P1: Compuesto seleccion + cursor + viewport aislados por buffer.
// A = 1000 lineas, scroll a 500, seleccion 500->700 con cursor en el extremo.
// A -> B -> A: A conserva seleccion (anchor+position), cursor y viewport.top.
// ---------------------------------------------------------------------------
TEST(interaction_viewport_selection_cursor_preserved_across_switch) {
    Editor ed;

    // A: 1000 lineas, scroll a 500, seleccion 500->700, cursor (700,0).
    ed.active().document.restore(std::vector<std::string>(1000, "linea"));
    ed.active().viewport.top = 500;
    ed.active().viewport.height = 24;
    ed.active().cursor.line = 700;
    ed.active().cursor.col = 0;
    ed.active().selection = Selection{Position{500, 0}, Position{700, 0}};
    CHECK(ed.hasSelection());

    // B: 10 lineas, sin seleccion.
    ed.createBuffer();
    ed.active().document.restore(std::vector<std::string>(10, "b"));
    CHECK(!ed.hasSelection());
    CHECK(ed.state_ == State::Navegacion);

    // A -> B -> A: A conserva seleccion, cursor, anchor y viewport.
    ed.activateBuffer(0);
    CHECK_EQ(ed.buffers.activeBuffer_, 0);
    CHECK_EQ(ed.active().document.lineCount(), 1000);
    CHECK(ed.state_ == State::Seleccion);            // con rango -> Seleccion
    CHECK(ed.hasSelection());
    CHECK(ed.active().selection->anchor == (Position{500, 0}));
    CHECK(ed.active().selection->position == (Position{700, 0}));
    CHECK_EQ(ed.active().cursor.line, 700);          // cursor del extremo
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().viewport.top, 500);         // scroll intacto

    // B sigue siendo ajeno: sin seleccion, pocas lineas.
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().document.lineCount(), 10);
    CHECK(!ed.hasSelection());
    CHECK(ed.state_ == State::Navegacion);
}

// ===========================================================================
// P0: Buffers con modified INDEPENDIENTE.
// El flag modified (y savedLines) vive en cada Buffer. A modificado, B
// guardado; al conmutar, cada indicador se conserva.
// ---------------------------------------------------------------------------
TEST(interaction_buffers_independent_modified) {
    Editor ed;
    // A: modificar una linea ("a" -> "ax")-> modified true
    ed.active().document.restore({"a"});
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    ed.active().cursor.col = ed.active().document.lineLength(0);
    ed.handleEvent(insert('i'));               // -> Interaccion
    ed.handleEvent(insert('x'));
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "ax");

    // B: guardado (modified false)
    ed.createBuffer();
    ed.active().document.restore({"b"});
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    CHECK(!ed.active().modified);

    // Conmutar no mezcla los indicadores.
    ed.activateBuffer(0);
    CHECK(ed.active().modified);               // A sigue modificado
    ed.activateBuffer(1);
    CHECK(!ed.active().modified);              // B sigue guardado
    ed.activateBuffer(0);
    CHECK(ed.active().modified);               // A intacto otra vez
}

// ===========================================================================
// P0: Undo despues de cambiar de buffer actua SOLO sobre el buffer activo.
//   A edit -> B edit -> A (undo) ---> solo A
//   -> B (undo) ----------------> solo B
// El historial de undo/redo vive en CADA Buffer (Buffer.undoStack); el
// Editor.undo() deshace sobre active(). Deshacer en A no debe rehacer/alterar
// la pila de B, y viceversa.
// ---------------------------------------------------------------------------
TEST(interaction_undo_switching_buffers_isolated) {
    Editor ed;

    // A edit: "a" -> "ab"
    ed.active().document.restore({"a"});
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    appendChar(ed, 'b');                       // A = "ab"
    CHECK_EQ(ed.active().document.lineAt(0), "ab");

    // buffer B edit: "x" -> "xy"
    ed.createBuffer();
    ed.active().document.restore({"x"});
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    appendChar(ed, 'y');                       // B = "xy"
    CHECK_EQ(ed.active().document.lineAt(0), "xy");

    // volver a A y deshacer: A "ab" -> "a", B "xy" intacto.
    ed.activateBuffer(0);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "a");     // solo A se deshizo
    CHECK_EQ(ed.buffers.count(), 2);

    // pasar a B y deshacer: B "xy" -> "x", A "a" intacto.
    ed.activateBuffer(1);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "x");     // solo B se deshizo

    // cada buffer conserva su propio estado final tras el undo del otro.
    ed.activateBuffer(0);
    CHECK_EQ(ed.active().document.lineAt(0), "a");     // A no fue tocado por B
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().document.lineAt(0), "x");     // B no fue tocado por A
}

// ===========================================================================
// P0: Redo despues de cambiar de buffer actua SOLO sobre el buffer activo.
//   A edit -> undo(A) -> B edit -> volver a A -> redo -> A rehace.
// B (editar B) NO debe acumularse en la pila de redo de A ni invalidarla:
// cada Buffer (Buffer.redoStack via Buffer.pushHistory) es independiente, y
// editar B solo limpia el redo de B, no el de A. Por tanto A conserva su
// redo pendiente entre el undo y el cambio de buffer.
// ---------------------------------------------------------------------------
TEST(interaction_redo_switching_buffers_isolated) {
    Editor ed;

    // A edit: "a" -> "ab"
    ed.active().document.restore({"a"});
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    appendChar(ed, 'b');                       // A = "ab"
    CHECK_EQ(ed.active().document.lineAt(0), "ab");

    // undo(A): A "ab" -> "a", dejando redo pendiente en A.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    CHECK(!ed.active().redoStack.empty());     // A tiene redo pendiente

    // buffer B edit: "x" -> "xy". Editar B NO debe tocar el redo de A.
    ed.createBuffer();
    ed.active().document.restore({"x"});
    ed.active().savedLines = ed.active().document.snapshot();
    ed.active().modified = false;
    appendChar(ed, 'y');                       // B = "xy"
    CHECK_EQ(ed.active().document.lineAt(0), "xy");

    // volver a A: sigue teniendo su redo (B no lo piso).
    ed.activateBuffer(0);
    CHECK(!ed.active().redoStack.empty());     // redo de A intacto

    // redo(A): A vuelve "a" -> "ab"; B "xy" intacto.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");     // A rehizo
    CHECK(ed.active().redoStack.empty());

    // B no fue alterado por el redo de A.
    ed.activateBuffer(1);
    CHECK_EQ(ed.active().document.lineAt(0), "xy");     // B intacto
    // y B sigue teniendo su propio historial sin rastro del redo de A.
    CHECK(ed.active().redoStack.empty());
    CHECK(!ed.active().undoStack.empty());     // B aun puede deshacer su edit
}

// ===========================================================================
// P0: Save + cambio de buffer: guardar es por-buffer.
//   A edit -> B edit -> A save -> B save => ambos modified=false.
//   Luego editar A: A.modified=true, B.modified=false.
// save() guarda SOLO el buffer activo (Editor::save sobre active()), fija
// su modified=false y su savedLines; no toca el estado del otro buffer.
// ---------------------------------------------------------------------------
TEST(interaction_save_switching_buffers_isolated_modified) {
    Editor ed;
    testfw::TempFile fileA;
    testfw::TempFile fileB;

    // dos buffers con nombre (save no se desvia a SaveAs).
    ed.openFile(fileA.path);                           // buffer A
    ed.createBuffer();                                 // -> nuevo buffer activo
    ed.openFile(fileB.path);                           // buffer B (activo)

    // A edit: "originalA" -> "originalA+"
    ed.activateBuffer(0);
    ed.active().document.restore({"originalA"});
    appendChar(ed, '+');
    CHECK_EQ(ed.active().document.lineAt(0), "originalA+");
    CHECK(ed.active().modified);

    // B edit: "originalB" -> "originalB+"
    ed.activateBuffer(1);
    ed.active().document.restore({"originalB"});
    appendChar(ed, '+');
    CHECK_EQ(ed.active().document.lineAt(0), "originalB+");
    CHECK(ed.active().modified);

    // A save: solo A queda no-modificado.
    ed.activateBuffer(0);
    ed.save();
    CHECK(!ed.active().modified);                      // A modified=false
    CHECK(ed.active().document.snapshot() == ed.active().savedLines);
    ed.activateBuffer(1);
    CHECK(ed.active().modified);                       // B sigue modificado

    // B save: ambos quedan no-modificados.
    ed.save();
    CHECK(!ed.active().modified);                      // B modified=false
    CHECK(ed.active().document.snapshot() == ed.active().savedLines);
    ed.activateBuffer(0);
    CHECK(!ed.active().modified);                      // A sigue sin modificar

    // Resultado pedido: A.modified == false y B.modified == false.
    ed.activateBuffer(0);
    CHECK(!ed.active().modified);                      // A
    ed.activateBuffer(1);
    CHECK(!ed.active().modified);                      // B

    // Luego editar A: solo A se vuelve modificado; B no cambia.
    ed.activateBuffer(0);
    appendChar(ed, 'x');                               // A edit tras guardar
    CHECK(ed.active().modified);                       // A.modified=true
    CHECK_EQ(ed.active().document.lineAt(0), "originalA+x");
    ed.activateBuffer(1);
    CHECK(!ed.active().modified);                      // B.modified=false
}