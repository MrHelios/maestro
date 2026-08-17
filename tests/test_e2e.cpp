#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_framework.h"

#include <string>
#include <vector>
#define private public
#include "ui/Editor.h"
#undef private

using testfw::TempFile;

// ===========================================================================
// E2E workflows: flujos de usuario COMPLETOS sobre el editor REAL.
//
// El Editor no es cabeza-sin-terminal (usa Terminal y Renderer concretos, no
// inyectables), asi que no se puede lanzar run() en un test. En su lugar un
// E2E aqui conduce el editor por su API publica (openFile) y su despacho de
// eventos (handleEvent), exactamente como haria la capa de terminal, PERO el
// documento proviene y se persiste en una ruta real del sistema de archivos:
// al final se lee el archivo FUERA del editor y se comparan los BYTES.
//
// Eso convierte cada workflow en una propiedad de extremo a extremo (editar
// + guardar + releer en disco byte a byte), no solo de la logica interna.
// ---------------------------------------------------------------------------
namespace {

Event insert(char c) {
    Event e;
    e.type = EventType::InsertChar;
    e.text = std::string(1, c);
    return e;
}

void press(Editor& ed, EventType type) {
    Event e;
    e.type = type;
    ed.handleEvent(e);
}

// Escribe `typed` como flujo de InsertChar, entrando a Interaccion si hace
// falta (mismo contrato que el modo de edicion real: la letra 'i' desde
// Navegacion entra a Interaccion; despues cada letra es texto).
void type(Editor& ed, const std::string& typed) {
    if (ed.state_ != State::Interaccion) {
        if (ed.state_ == State::Seleccion) {
            Event e;
            e.type = EventType::Escape;
            ed.handleEvent(e);
        }
        ed.handleEvent(insert('i'));
    }
    for (char c : typed)
        ed.handleEvent(insert(c));
}

void prefix(Editor& ed, EventType first, EventType second) {
    press(ed, first);
    press(ed, second);
}

// Lee el archivo fuera del editor como bytes crudos (verificacion externa).
std::string readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void writeBytes(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

} // namespace

// ===========================================================================
// E2E-01 — Editar y guardar (P0)
//
//   open existing file
//   -> insert
//   -> delete
//   -> move
//   -> save
//   -> quit
//   -> read file externally
//   verificar bytes.
//
// Workflow concreto y determinista (se deriva byte a byte):
//   init        : "hola\n"                         (archivo existente, LF)
//   open        : cursor (0,0), Navegacion, modified=false
//   insert      : 'i' + escribir " mundo" al final -> "hola mundo", (0,10)
//   delete      : 3x Backspace  -> "hola mu",   (0,7)
//   move        : MoveHome "Hi" -> "Hihola mu", (0,2); MoveEnd (0,9)
//   newline     : Enter -> ["Hihola mu", ""], (1,0); 'X' -> "X", (1,1)
//   save        : Ctrl+K Ctrl+S -> modified=false
//   quit        : Ctrl+K Ctrl+Q -> running_=false
//   bytes final : "Hihola mu\nX"  (sin '\n' final, ver normalizeEndsWithNewline)
// ---------------------------------------------------------------------------
TEST(e2e_01_edit_and_save_byte_exact) {
    TempFile f;
    writeBytes(f.path, "hola\n");

    Editor ed;
    CHECK(ed.openFile(f.path));                 // open existing file (Success)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);

    // insert: " mundo" al final de la linea.
    press(ed, EventType::MoveEnd);              // (0,4)
    type(ed, " mundo");
    CHECK_EQ(ed.active().document.lineAt(0), "hola mundo");
    CHECK_EQ(ed.active().cursor.col, 10);
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().document.lineCount(), 1);

    // delete: 3x Backspace -> quita "ndo".
    press(ed, EventType::Backspace);
    press(ed, EventType::Backspace);
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "hola mu");
    CHECK_EQ(ed.active().cursor.col, 7);
    CHECK_EQ(ed.active().document.lineCount(), 1);

    // move + insert al inicio.
    press(ed, EventType::MoveHome);             // (0,0)
    type(ed, "Hi");
    CHECK_EQ(ed.active().document.lineAt(0), "Hihola mu");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK_EQ(ed.active().document.lineCount(), 1);

    // move al final + nueva linea + texto en la segunda.
    press(ed, EventType::MoveEnd);              // (0,9)
    Event nl;
    nl.type = EventType::InsertNewline;
    ed.handleEvent(nl);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
    type(ed, "X");
    CHECK_EQ(ed.active().document.lineAt(1), "X");

    // save: Ctrl+K Ctrl+S.
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK(!ed.active().modified);

    // quit: Ctrl+K Ctrl+Q.
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // read file externally y verificar bytes exactos.
    CHECK_EQ(readBytes(f.path), "Hihola mu\nX");
}

// ===========================================================================
// E2E-02 — Undo/Redo completo (P0)
//
//   open -> edit -> edit -> undo -> undo -> redo -> redo -> save -> quit
//   comparar archivo final.
//
// Workflow concreto y determinista (cada caracter es una edicion aparte,
// sin coalescing, porque no hay reemplazo de seleccion):
//   init      : "hola\n"
//   open      : cursor (0,0), Navegacion
//   edit      : 'i' + 'X' al inicio       -> "Xhola", (0,1)
//   edit      : 'Y'                       -> "XYhola", (0,2)
//   undo      :                            -> "Xhola", (0,1)
//   undo      :                            -> "hola", (0,0)
//   redo      :                            -> "Xhola", (0,1)
//   redo      :                            -> "XYhola", (0,2)
//   save+quit :                            -> archivo = "XYhola\n" (LF final
//              conservado por endsWithNewline).
// Tras el ciclo undo/redo el documento vuelve al mismo estado previo al
// guardado; el archivo en disco debe ser EXACTAMENTE "XYhola\n".
// ---------------------------------------------------------------------------
TEST(e2e_02_undo_redo_full_byte_exact) {
    TempFile f;
    writeBytes(f.path, "hola\n");

    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // edit 1: 'X' al inicio -> "Xhola"
    type(ed, "X");
    CHECK_EQ(ed.active().document.lineAt(0), "Xhola");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 1);
    CHECK_EQ(ed.active().undoStack.size(), 1u);

    // edit 2: 'Y' -> "XYhola"
    type(ed, "Y");
    CHECK_EQ(ed.active().document.lineAt(0), "XYhola");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK_EQ(ed.active().undoStack.size(), 2u);

    // undo x2
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "Xhola");
    CHECK_EQ(ed.active().cursor.col, 1);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "hola");
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK_EQ(ed.active().undoStack.size(), 0u);

    // redo x2
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "Xhola");
    CHECK_EQ(ed.active().cursor.col, 1);
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "XYhola");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK_EQ(ed.active().redoStack.size(), 0u);

    // save + quit
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK(!ed.active().modified);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // comparar archivo final (byte a byte, conservando el '\n' final).
    CHECK_EQ(readBytes(f.path), "XYhola\n");
}

// ===========================================================================
// E2E-03 — Selection replacement (P0)
//
//   open -> select text -> type replacement -> undo -> redo -> save
//
// Workflow concreto y determinista:
//   init       : "hello world\n"
//   open       : cursor (0,0), Navegacion
//   navigate   : MoveRight x6        -> cursor (0,6)
//   select     : 's' (entra a Seleccion) + MoveRight x5 -> "world" [6,11)
//   type       : reemplazo "Maestro" -> "hello Maestro", (0,13)
//                (una sola edicion coalescida)
//   undo       :                      -> "hello world" (+ seleccion [6,11))
//   redo       :                      -> "hello Maestro" (0,13)
//   save+quit  :                      -> archivo = "hello Maestro\n"
//
// Tras el ciclo undo/redo el documento vuelve al reemplazo; al guardar el
// archivo debe ser EXACTAMENTE "hello Maestro\n" (el '\n' final original se
// conserva). Ademas verifica que el reemplazo en vivo fue una sola edicion
// (una entrada de undo) y que el single undo restaura la seleccion.
// ---------------------------------------------------------------------------
TEST(e2e_03_selection_replacement_byte_exact) {
    TempFile f;
    writeBytes(f.path, "hello world\n");

    Editor ed;
    CHECK(ed.openFile(f.path));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // navigate hasta la "w" de "world" (col 6).
    for (int i = 0; i < 6; ++i)
        press(ed, EventType::MoveRight);     // (0,6)
    CHECK_EQ(ed.active().cursor.col, 6);

    // seleccionar "world" = [6,11).
    ed.handleEvent(insert('s'));             // Navegacion -> Seleccion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    for (int i = 0; i < 5; ++i)
        press(ed, EventType::MoveRight);     // extiende la seleccion
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 6);
    CHECK_EQ(ed.active().selection->position.line, 0);
    CHECK_EQ(ed.active().selection->position.col, 11);

    // type replacement: "Maestro" reemplaza el rango y coalesce en UNA
    // edicion (una entrada de undo).
    ed.handleEvent(insert('M'));
    const size_t undoAfterFirst = ed.active().undoStack.size();
    for (char c : std::string("aestro"))
        ed.handleEvent(insert(c));
    CHECK_EQ(ed.active().document.lineAt(0), "hello Maestro");
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    // "M" ya empujo 1 entrada; el resto se absorbe: ninguna extra.
    CHECK_EQ(ed.active().undoStack.size(), undoAfterFirst);
    CHECK_EQ(ed.active().undoStack.size(), 1u);

    // undo: restaura "hello world" con la seleccion [6,11).
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "hello world");
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 6);
    CHECK_EQ(ed.active().selection->position.col, 11);

    // redo: vuelve al reemplazo.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "hello Maestro");
    CHECK(!ed.hasSelection());

    // save + quit.
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK(!ed.active().modified);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // comparar archivo final (byte a byte, conservando el '\n' final).
    CHECK_EQ(readBytes(f.path), "hello Maestro\n");
}

// ===========================================================================
// E2E-04 — Multiline selection: delete/undo/redo (P0)
//
//   open -> select across lines -> delete -> undo -> redo -> save -> quit
//   comparar archivo final.
//
// Workflow concreto y determinista:
//   init       : "aaa\nbbb\nccc\n"
//   open       : cursor (0,0), Navegacion, modified=false
//   select     : 's' (entra a Seleccion) + MoveRight x3 + MoveDown x2 +
//                MoveLeft -> seleccion [0,0)..(2,2] = "aaa\nbbb\ncc"
//   delete     : Delete borra el rango multilinea      -> "c\n", (0,0)
//   undo       :                                    -> "aaa\nbbb\nccc\n"
//                (+ seleccion [0,0)..(2,2] restaurada), cursor (2,2)
//   redo       :                                    -> "c\n", (0,0)
//   save+quit  :                                    -> archivo = "c\n"
//
// Es el flujo P0 pedido aplicado a un rango que CRUZA varias lineas: el
// borrado afecta tres lineas a la vez, un solo undo reconstruye el texto,
// el cursor y la seleccion EXACTOS, y el redo reproduce el borrado. Al
// guardar, el archivo en disco debe ser EXACTAMENTE "c\n" (queda un '\n'
// final, el '\n' de la linea "ccc" cortada).
// ---------------------------------------------------------------------------
TEST(e2e_04_multiline_selection_delete_undo_redo_byte_exact) {
    TempFile f;
    writeBytes(f.path, "aaa\nbbb\nccc\n");

    Editor ed;
    CHECK(ed.openFile(f.path));                 // open existing file (Success)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);

    // select: entrar a Seleccion y extender un rango que cruza lineas.
    ed.handleEvent(insert('s'));                // Navegacion -> Seleccion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    for (int i = 0; i < 3; ++i)
        press(ed, EventType::MoveRight);        // (0,3) fin de "aaa"
    press(ed, EventType::MoveDown);             // (1,3) fin de "bbb"
    press(ed, EventType::MoveDown);             // (2,3) fin de "ccc"
    press(ed, EventType::MoveLeft);             // (2,2)
    CHECK(ed.hasSelection());
    // rango [anchor(0,0), position(2,2)]: "aaa\nbbb\ncc".
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 0);
    CHECK_EQ(ed.active().selection->position.line, 2);
    CHECK_EQ(ed.active().selection->position.col, 2);

    // delete: borrar el rango MULTILINEA completo.
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineAt(0), "c");
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);

    // undo: reconstruye texto + cursor + seleccion exactos.
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() ==
          (std::vector<std::string>{"aaa", "bbb", "ccc"}));
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->anchor.col, 0);
    CHECK_EQ(ed.active().selection->position.line, 2);
    CHECK_EQ(ed.active().selection->position.col, 2);
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 2);

    // redo: reproduce el borrado multilinea.
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "c");
    CHECK(!ed.hasSelection());
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);

    // save + quit.
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK(!ed.active().modified);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // comparar archivo final (byte a byte): queda solo "c\n".
    CHECK_EQ(readBytes(f.path), "c\n");
}

// ===========================================================================
// E2E-05 — UTF-8 workflow: navigate/select/delete/undo/copy/paste (P0)
//
//   open -> navigate -> select -> delete -> undo -> select -> copy
//       -> paste -> save -> quit -> comparar bytes.
//
// El archivo mezcla codigos ASCII y secuencias UTF-8 multibyte, y todo el
// viaje (coordenadas de seleccion, borrado, undo/redo del clipboard y pegado)
// opera sobre OFFSETS DE BYTE byte-safe: ninguna operacion aterriza dentro
// de una celda UTF-8, y al guardar los bytes quedan EXACTOS.
//
// Contenido por linea (longitudes en BYTES):
//   line0 "café"      = "caf" + é(0xC3 0xA9)                     -> 5 B
//   line1 "mañana"    = "ma" + ñ(0xC3 0xB1) + "ana"              -> 7 B
//   line2 "€"         = 0xE2 0x82 0xAC                           -> 3 B
//   line3 "—"         = 0xE2 0x80 0x94                           -> 3 B
//   line4 "😀"        = 0xF0 0x9F 0x98 0x80                       -> 4 B
//
// Workflow determinista:
//   init        : "café\nmañana\n€\n—\n😀\n", cursor (0,0), Navegacion
//   navigate    : MoveDown                              -> (1,0) en "mañana"
//   select      : MoveRight x2 -> (1,2); 's' (ancla en (1,2));
//                 MoveRight x2 -> (1,4). sel "ñ" = [1,2)..(1,4]
//   delete      : Delete borra "ñ"                      -> "maana"
//   undo        : restaura "mañana" + cursor (1,4) + sel [1,2..1,4]
//                 (el undo devuelve el modo a Seleccion)
//   (re)select  : Escape limpia la seleccion; MoveHome (1,0); 's' +
//                 MoveEnd -> sel "mañana" = [1,0..1,7]
//   copy        : 'c' -> clipboard ["mañana"], Navegacion, cursor (1,7)
//   paste       : MoveDown x3 hacia la ultima linea ((4,4) fin de "😀")
//                 + 'p' -> insertBlock -> line4 "😀mañana", cursor (4,11)
//   save+quit   : archivo final = "café\nmañana\n€\n—\n😀mañana\n"
// ---------------------------------------------------------------------------
TEST(e2e_05_utf8_workflow_byte_exact) {
    TempFile f;

    // Bytes originales, escritos como una sola cadena byte-exacta.
    const std::string original =
        "caf" "\xC3\xA9" "\n"
        "ma"  "\xC3\xB1" "ana" "\n"
        "\xE2\x82\xAC"   "\n"
        "\xE2\x80\x94"   "\n"
        "\xF0\x9F\x98\x80" "\n";
    writeBytes(f.path, original);

    // Bytes finales (line4 "😀" + clipboard "mañana" pegados, '\n' final).
    const std::string finalBytes =
        "caf" "\xC3\xA9" "\n"
        "ma"  "\xC3\xB1" "ana" "\n"
        "\xE2\x82\xAC"   "\n"
        "\xE2\x80\x94"   "\n"
        "\xF0\x9F\x98\x80" "ma" "\xC3\xB1" "ana" "\n";

    const std::vector<std::string> originalLines = {
        "caf\xC3\xA9",
        "ma" "\xC3\xB1" "ana",
        "\xE2\x82\xAC",
        "\xE2\x80\x94",
        "\xF0\x9F\x98\x80",
    };

    Editor ed;
    CHECK(ed.openFile(f.path));                 // open existing file (Success)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);

    // navigate -> a la linea "mañana" (line1).
    press(ed, EventType::MoveDown);             // (1,0)
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);

    // select "ña" cruzando el borde de un codepoint: primero movemos el
    // cursor al INICIO de la celda multibyte (col 2) y recien ahi entramos a
    // Seleccion (el ancla se fija donde estaba el cursor); luego MoveRight
    // salta la celda completa y el rango queda [1,2..1,4] = "ñ".
    press(ed, EventType::MoveRight);            // (1,1)
    press(ed, EventType::MoveRight);            // (1,2) inicio de "ñ"
    ed.handleEvent(insert('s'));                // Navegacion -> Seleccion, ancla (1,2)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    press(ed, EventType::MoveRight);            // (1,4) fin de "ñ" (salta 2 bytes)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 1);
    CHECK_EQ(ed.active().selection->anchor.col, 2);
    CHECK_EQ(ed.active().selection->position.line, 1);
    CHECK_EQ(ed.active().selection->position.col, 4);

    // delete: borra la celda multibyte "ñ" -> "maana".
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineAt(1), "maana");
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().modified);

    // undo: reconstruye "mañana" EXACTA (mismas 7 bytes) + cursor (1,4) +
    // seleccion [1,2..1,4] restaurada.
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(1), "ma" "\xC3\xB1" "ana");
    CHECK_EQ(ed.active().document.lineAt(1).size(), 7u);   // byte-exacto
    CHECK(ed.active().document.snapshot() == originalLines);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 2);
    CHECK_EQ(ed.active().selection->position.col, 4);

    // Resetear el ancla de la seleccion restaurada: el undo devuelve el
    // editor al modo Seleccion (la seleccion restaurada esta vigente), asi
    // que Escape la limpia directamente (cursor queda en (1,4), Navegacion).
    press(ed, EventType::Escape);               // limpia la seleccion
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // select "mañana" entera para copiarla.
    press(ed, EventType::MoveHome);             // (1,0)
    ed.handleEvent(insert('s'));                // -> Seleccion, anchor (1,0)
    press(ed, EventType::MoveEnd);              // (1,7) = fin de "mañana"
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 0);
    CHECK_EQ(ed.active().selection->position.col, 7);

    // copy: clipboard ["mañana"] (7 bytes), vuelve a Navegacion.
    ed.handleEvent(insert('c'));
    CHECK(ed.clipboard_ == (std::vector<std::string>{"ma" "\xC3\xB1" "ana"}));
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 7);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // paste: bajar hasta la ultima linea (la de "😀") y pegar al final.
    press(ed, EventType::MoveDown);             // (2,3) fin de "€"
    press(ed, EventType::MoveDown);             // (3,3) fin de "—"
    press(ed, EventType::MoveDown);             // (4,4) fin de "😀"
    CHECK_EQ(ed.active().cursor.line, 4);
    CHECK_EQ(ed.active().cursor.col, 4);
    ed.handleEvent(insert('p'));                // pega "mañana" despues de "😀"
    CHECK_EQ(ed.active().document.lineAt(4),
             "\xF0\x9F\x98\x80" "ma" "\xC3\xB1" "ana");
    CHECK(ed.active().modified);

    // save + quit.
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK(!ed.active().modified);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // comparar todo el archivo FINAL byte a byte.
    CHECK_EQ(readBytes(f.path), finalBytes);
}

// ===========================================================================
// E2E-06 — Multi-buffer basico: dos archivos independientes (P0)
//
//   open A -> Ctrl+K n -> escribir B -> volver A -> editar A -> volver B
//       -> editar B -> save A -> save B -> quit
//   comprobar AMBOS archivos en disco.
//
// Workflow concreto y determinista (todo por eventos reales, incluido el
// selector de buffers Ctrl+K t y el prompt Guardar archivo: para B):
//   open A      : "AAA\n"  -> buffer 0 activo, Navegacion, modified=false
//   Ctrl+K n    : buffer B nuevo SIN nombre, activo, Navegacion
//   escribir B  : 'i' + "BBB"                -> B = "BBB", modified
//   volver A    : Ctrl+K t (selector) + MoveUp + Enter -> A activo, "AAA"
//   editar A    : MoveEnd + 'i' + "A2"       -> A = "AAAA2", modified
//   volver B    : Ctrl+K t + MoveDown + Enter -> B activo, "BBB"
//   editar B    : MoveEnd + 'i' + "B2"       -> B = "BBBB2", modified
//   save A      : Ctrl+K Ctrl+S (A tiene nombre)  -> A modified=false
//   save B      : Ctrl+K Ctrl+S -> prompt "Guardar archivo:" + ruta(B)
//                 + Enter -> B guardado en su ruta, modified=false
//   quit        : Ctrl+K Ctrl+Q -> running_=false
//   comprobar   : readBytes(A) == "AAAA2\n" (conserva el '\n' final de A)
//                 readBytes(B) == "BBBB2"   (B empezo vacio: sin '\n' final)
//
// Verifica el aislamiento REAL entre buffers (contenido, cursor, modified,
// nombre) y que el save es por-buffer: guardar A no toca B y viceversa.
// ---------------------------------------------------------------------------
TEST(e2e_06_multibuffer_basic_byte_exact) {
    TempFile fileA;
    TempFile fileB;
    writeBytes(fileA.path, "AAA\n");

    Editor ed;
    CHECK(ed.openFile(fileA.path));             // open A (buffer 0, activo)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);
    CHECK(ed.active().filename == fileA.path);

    // Ctrl+K n: buffer B nuevo SIN nombre, activo, en Navegacion.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));                // buffer.nuevo
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK(ed.active().filename.empty());        // B no tiene nombre aun
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // escribir B: "BBB".
    type(ed, "BBB");
    CHECK_EQ(ed.active().document.lineAt(0), "BBB");
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().cursor.col, 3);

    // volver A: selector Ctrl+K t, subir una posicion, Enter.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // buffer.selector
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::BufferSelector));
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    Event enter;
    enter.type = EventType::InsertNewline;
    ed.handleEvent(enter);                      // activar A
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);               // A intacto (su modified)

    // editar A: mover al final y anexar "A2".
    press(ed, EventType::MoveEnd);              // (0,3)
    type(ed, "A2");
    CHECK_EQ(ed.active().document.lineAt(0), "AAAA2");
    CHECK(ed.active().modified);

    // volver B: selector, bajar una posicion, Enter.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);                      // activar B
    CHECK_EQ(ed.active().document.lineAt(0), "BBB");   // B sigue como quedo
    CHECK_EQ(ed.active().cursor.col, 3);

    // editar B: anexar "B2".
    press(ed, EventType::MoveEnd);              // (0,3)
    type(ed, "B2");
    CHECK_EQ(ed.active().document.lineAt(0), "BBBB2");
    CHECK(ed.active().modified);

    // save A: volver a A (selector, subir) y Ctrl+K Ctrl+S.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "AAAA2");
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK(!ed.active().modified);               // A guardado

    // save B: volver a B y Ctrl+K Ctrl+S -> prompt Guardar archivo.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "BBBB2");
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));
    for (char c : fileB.path)
        ed.handleEvent(insert(c));              // escribir la ruta destino
    ed.handleEvent(enter);                      // Enter: guardar en esa ruta
    CHECK_EQ(ed.active().filename, fileB.path);
    CHECK(!ed.active().modified);               // B guardado

    // quit.
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // comprobar AMBOS archivos en disco (byte a byte, independientes).
    CHECK_EQ(readBytes(fileA.path), "AAAA2\n"); // A conservo su '\n' final
    CHECK_EQ(readBytes(fileB.path), "BBBB2");   // B empezo vacio: sin '\n'
}