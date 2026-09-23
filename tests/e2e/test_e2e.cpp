#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_framework.h"

#include <string>
#include <vector>
#define private public
#include "app/Editor.h"
#undef private
#include "filesystem/FileSystem.h"

using testfw::TempFile;

// ===========================================================================
// E2E workflows: flujos de usuario COMPLETOS sobre el editor REAL.
//
// El Editor no es cabeza-sin-terminal (usa Terminal y Renderer concretos, no
// inyectables), asi que no se puede lanzar run() en un test. En su lugar un
// E2E aqui conduce el editor mediante handleEvent(), como en produccion,
// mientras que algunas verificaciones de estado interno usan acceso
// white-box (#define private public) para comprobar invariantes que no
// forman parte de la API publica (state_, running_, saveAsPath_,
// statusMessage_, fileBrowser._, buffers, etc.). El documento proviene y
// se persiste en una ruta real del sistema de archivos: al final se lee
// el archivo FUERA del editor y se comparan los BYTES.
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

static void saveViaS(Editor& ed) {
    press(ed, EventType::Prefix);
    Event e; e.type = EventType::InsertChar; e.text = "s"; ed.handleEvent(e);
}

// Lee el archivo fuera del editor como bytes crudos (verificacion externa).
std::string readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    CHECK(in.is_open());
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(in.good() || in.eof());
    return s;
}

void writeBytes(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(out.is_open());
    out << content;
    CHECK(out.good());
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
    CHECK(ed.loadIntoActiveBuffer(f.path));                 // open existing file (Success)
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
    saveViaS(ed);
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
    CHECK(ed.loadIntoActiveBuffer(f.path));
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
    saveViaS(ed);
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
    CHECK(ed.loadIntoActiveBuffer(f.path));
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
    saveViaS(ed);
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
    CHECK(ed.loadIntoActiveBuffer(f.path));                 // open existing file (Success)
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
    saveViaS(ed);
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
// El archivo mezcla ASCII y UTF-8 multibyte. Se comprueba navegacion
// por celdas, seleccion, borrado, undo, copy/paste y persistencia
// byte-exacta. Todo opera sobre OFFSETS DE BYTE byte-safe: ninguna
// operacion aterriza dentro de una celda UTF-8, y al guardar los bytes
// quedan EXACTOS.
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
    CHECK(ed.loadIntoActiveBuffer(f.path));                 // open existing file (Success)
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
    CHECK(ed.getClipboardBlock() == (std::vector<std::string>{"ma" "\xC3\xB1" "ana"}));
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
    saveViaS(ed);
    CHECK(!ed.active().modified);
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // comparar todo el archivo FINAL byte a byte.
    CHECK_EQ(readBytes(f.path), finalBytes);
}

// ===========================================================================
// E2E-06 — Multi-buffer basico: dos archivos independientes (P0)
//
//   loadIntoActiveBuffer A -> Ctrl+K n -> escribir B -> volver A -> editar A -> volver B
//       -> editar B -> save A -> save B -> quit
//   comprobar AMBOS archivos en disco.
//
// Workflow concreto y determinista (todo por eventos reales, incluido el
// selector de buffers Ctrl+K t y el prompt Guardar archivo: para B):
//   loadIntoActiveBuffer A      : "AAA\n"  -> buffer 0 activo, Navegacion, modified=false
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
    CHECK(ed.loadIntoActiveBuffer(fileA.path));             // loadIntoActiveBuffer A (buffer 0, activo)
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
    saveViaS(ed);
    CHECK(!ed.active().modified);               // A guardado

    // save B: volver a B y Ctrl+K Ctrl+S -> prompt Guardar archivo.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "BBBB2");
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));
    ed.saveAsPath_.clear();
    for (char c : fileB.path)
        ed.handleEvent(insert(c));
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().filename, fileB.path);
    CHECK(!ed.active().modified);

    // quit.
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // comprobar AMBOS archivos en disco (byte a byte, independientes).
    CHECK_EQ(readBytes(fileA.path), "AAAA2\n"); // A conservo su '\n' final
    CHECK_EQ(readBytes(fileB.path), "BBBB2");   // B empezo vacio: sin '\n'
}

// ===========================================================================
// E2E-07 — Multi-buffer + undo/redo (P0)
//
//   loadIntoActiveBuffer A -> Ctrl+K n (B) -> A edit -> B edit -> A undo -> B undo
//       -> B edit -> A redo -> verificar B
//
// El undo/redo vive en CADA buffer: deshacer A no toca B y viceversa; y
// editar un buffer NO limpia el historial (ni la rama de redo) del otro.
// En particular, editar B tras dejar a A con redo pendiente NO debe
// vaciar el redo de A (solo el propio de B).
//
// Workflow determinista (ediciones de UNA letra para que undo/redo sean 1:1):
//   loadIntoActiveBuffer A     : "AAA\n"        -> buffer 0 activo, cursor (0,0), modified=false
//   Ctrl+K n   : buffer B nuevo (vacio, sin nombre), activo
//   A edit     : volver a A (Ctrl+K t ↑ Enter) + MoveEnd + 'X' -> "AAAX"
//   B edit     : volver a B (Ctrl+K t ↓ Enter) + 'B'          -> "B"
//   A undo     : volver a A + Ctrl+U -> "AAA", modified=false
//   B undo     : volver a B + Ctrl+U -> ""  , modified=false
//   B edit2    : 'C' -> "C" (limpia redo de B, preserva redo de A)
//   A redo     : volver a A + Ctrl+Y -> "AAAX", modified=true
//   verificar B: volver a B -> "C", modified=true, redo vacio
//
// En cada paso se verifica tamien que las pilas undo/redo son por-buffer:
// editar B no vacia el redo pendiente de A, y deshacer A no altera B.
// ---------------------------------------------------------------------------
TEST(e2e_07_multibuffer_undo_redo_isolated) {
    TempFile fileA;
    writeBytes(fileA.path, "AAA\n");

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fileA.path));             // buffer A activo
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);

    // Ctrl+K n: buffer B nuevo (vacio), activo.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));
    CHECK_EQ(ed.buffers.count(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "");

    // ---- A edit: volver a A y anexar 'X' -> "AAAX".
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // selector
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    Event enter;
    enter.type = EventType::InsertNewline;
    ed.handleEvent(enter);                      // activar A
    press(ed, EventType::MoveEnd);              // (0,3)
    type(ed, "X");                              // A = "AAAX"
    CHECK_EQ(ed.active().document.lineAt(0), "AAAX");
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 1u);

    // ---- B edit: volver a B y escribir 'B' -> "B".
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);
    type(ed, "B");                              // B = "B"
    CHECK_EQ(ed.active().document.lineAt(0), "B");
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 1u);

    // ---- A undo: volver a A y deshacer -> "AAA".
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    ed.handleEvent(enter);
    press(ed, EventType::Undo);                 // A deshace
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 0u);
    CHECK_EQ(ed.active().redoStack.size(), 1u); // A dejo una rama rehacible

    // ---- B undo: volver a B y deshacer -> "".
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);
    press(ed, EventType::Undo);                 // B deshace
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 0u);
    CHECK_EQ(ed.active().redoStack.size(), 1u);
    // Editar/deshacer B NO toco la rama de redo pendiente de A.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().redoStack.size(), 1u); // A aun puede rehacer

    // ---- B edit tras undo: editar B NO debe limpiar el redo de A.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);
    type(ed, "C");
    CHECK_EQ(ed.active().document.lineAt(0), "C");
    CHECK_EQ(ed.active().redoStack.size(), 0u); // B perdio su propia rama de redo
    CHECK_EQ(ed.active().undoStack.size(), 1u);
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().redoStack.size(), 1u); // A sigue rehacible pese a editar B
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");

    // ---- A redo: rehacer en A (el redo de A sigue vivo) -> "AAAX".
    press(ed, EventType::Redo);                 // A rehace
    CHECK_EQ(ed.active().document.lineAt(0), "AAAX");
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().redoStack.size(), 0u);

    // ---- B sigue en "C" y sin redo pendiente.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "C");
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().redoStack.size(), 0u);

    // Estado final: A="AAAX", B="C", cada uno con su propio modificado.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "AAAX");
    CHECK(ed.active().modified);
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "C");
    CHECK(ed.active().modified);
}

// ===========================================================================
// E2E-08 — FileBrowser: navegar directorios y abrir un archivo (P1)
//
//   loadIntoActiveBuffer A -> Ctrl+K o -> navegar directorios -> openFileInBuffer B -> editar B
//       -> save -> volver A
//
// El explorador arranca en el cwd del proceso, asi que (igual que los tests
// de interaccion) le sembramos el directorio de arranque a traves del estado
// del FileBrowser (ed.fileBrowser.path_) para que el test sea determinista;
// el resto del flujo (orden de entradas, subir/bajar, Enter, abrir, guardar,
// selector) es 100% por eventos reales.
//
// Arbol temporal (base/):
//   alpha.txt   "AAA\n"     (archivo A, se abre al inicio)
//   beta/
//     gamma.txt "BBB\n"     (archivo B, se abre desde el explorador)
//
//   loadIntoActiveBuffer A      : loadIntoActiveBuffer(alpha.txt) -> buffer 0 activo, "AAA"
//   Ctrl+K o    : entra al explorador; sembramos base/ -> ["..","beta/","alpha.txt"]
//   navegar     : MoveDown -> "beta/"; Enter (entra) -> ["..","gamma.txt"]
//   openFileInBuffer B     : MoveDown -> "gamma.txt"; Enter -> buffer B activo, "BBB"
//   editar B    : MoveEnd + 'X' -> "BBBX", modified
//   save        : Ctrl+K Ctrl+S (B tiene nombre) -> modified=false, disco="BBBX\n"
//   volver A    : Ctrl+K t + MoveUp + Enter -> buffer A, "AAA" (intacto)
// ---------------------------------------------------------------------------
TEST(e2e_08_filebrowser_open_edit_save_switch) {
    namespace fs = std::filesystem;

    const std::string base =
        "/tmp/edit_fb_" + std::to_string(::getpid()) + "_e2e08";
    testfw::TempDir baseDir(base);
    const std::string dirBeta  = base + "/beta";
    const std::string pathA    = base + "/alpha.txt";
    const std::string pathB    = dirBeta + "/gamma.txt";

    fs::create_directories(dirBeta);
    writeBytes(pathA, "AAA\n");
    writeBytes(pathB, "BBB\n");

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(pathA));                  // loadIntoActiveBuffer A
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");
    CHECK(!ed.active().modified);

    // Ctrl+K o: entrar al explorador, luego sembrar el directorio de arranque
    // (el explorador por defecto arranca en cwd; fijamos base/ para el test).
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('o'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::FileBrowser));
    ed.fileBrowser.path_ = base;                // sembrar dir inicial
    ed.fileBrowser.reload();
    ed.fileBrowser.index_ = 0;
    CHECK_EQ(ed.fileBrowser.displayNames_.size(), 3u);
    CHECK_EQ(ed.fileBrowser.displayNames_[0], "../");      // siempre arriba
    CHECK_EQ(ed.fileBrowser.displayNames_[1], "beta/");   // carpetas primero
    CHECK_EQ(ed.fileBrowser.displayNames_[2], "alpha.txt");

    // navegar directorios: bajar a "beta/" y entrar.
    press(ed, EventType::MoveDown);             // 0 -> 1 "beta/"
    Event enter;
    enter.type = EventType::InsertNewline;
    ed.handleEvent(enter);                      // enter() -> entrar a beta/
    CHECK(ed.fileBrowser.path_ == dirBeta);
    CHECK_EQ(ed.fileBrowser.displayNames_.size(), 2u);
    CHECK_EQ(ed.fileBrowser.displayNames_[0], "../");
    CHECK_EQ(ed.fileBrowser.displayNames_[1], "gamma.txt");

    // openFileInBuffer B: bajar a "gamma.txt" y Enter.
    press(ed, EventType::MoveDown);             // 0 -> 1 "gamma.txt"
    ed.handleEvent(enter);                      // abrir archivo -> buffer B
    CHECK_EQ(ed.buffers.count(), 2);            // A + B
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().document.lineAt(0) == "BBB");
    CHECK(ed.active().filename == pathB);

    // editar B: anexar 'X'.
    press(ed, EventType::MoveEnd);              // (0,3)
    type(ed, "X");                              // B = "BBBX"
    CHECK_EQ(ed.active().document.lineAt(0), "BBBX");
    CHECK(ed.active().modified);

    // save: Ctrl+K Ctrl+S (B tiene nombre -> guardado directo).
    saveViaS(ed);
    CHECK(!ed.active().modified);
    CHECK_EQ(readBytes(pathB), "BBBX\n");       // conservo el '\n' final de B

    // volver A: selector Ctrl+K t, subir, Enter.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    press(ed, EventType::MoveUp);               // 1 -> 0 (A)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");
    CHECK(!ed.active().modified);
    CHECK_EQ(readBytes(pathA), "AAA\n");        // A intacto en disco
}

// ===========================================================================
// E2E-09 — Nuevo buffer -> Save As (P0)
//
//   Ctrl+K n -> escribir archivo -> Save As -> elegir path -> save -> quit
//   -> verificar filesystem
//
// Un buffer nuevo (Ctrl+K n) no tiene nombre, asi que Ctrl+K Ctrl+S no
// guarda directo: abre el prompt "Guardar archivo:" (SaveAs). Se escribe la
// ruta destino y Enter guarda y ancla el nombre al buffer. Luego quit y se
// relee el archivo FUERA del editor para verificar los BYTES escritos.
//
// Workflow determinista:
//   Ctrl+K n   : buffer nuevo sin nombre, vacio, activo, Navegacion
//   escribir   : "Hola" + Enter + "mundo"      -> [ "Hola", "mundo" ]
//   Save As    : Ctrl+K Ctrl+S                 -> state = SaveAs
//   path       : escribir la ruta destino      -> Enter (commit)
//                 -> filename = ruta, modified=false
//   quit       : Ctrl+K Ctrl+Q                 -> running_=false
//   filesystem : readBytes(ruta) == "Hola\nmundo" (sin '\n' final: el buffer
//                 empezo vacio y nunca termino en newline)
// ---------------------------------------------------------------------------
TEST(e2e_09_new_buffer_save_as_byte_exact) {
    TempFile f;                                 // ruta destino (no existe aun)

    Editor ed;
    // Ctrl+K n: buffer nuevo sin nombre, activo.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));
    CHECK(ed.active().filename.empty());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "");

    // escribir el contenido: "Hola\nmundo".
    type(ed, "Hola");
    CHECK_EQ(ed.active().document.lineAt(0), "Hola");
    Event nl;
    nl.type = EventType::InsertNewline;
    ed.handleEvent(nl);                         // -> ["Hola", ""]
    type(ed, "mundo");                          // -> ["Hola", "mundo"]
    CHECK(ed.active().document.snapshot() ==
          (std::vector<std::string>{"Hola", "mundo"}));
    CHECK(ed.active().modified);

    // Save As: Ctrl+K Ctrl+S -> prompt (buffer sin nombre).
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));
    ed.saveAsPath_.clear();

    // elegir path: escribir la ruta destino y Enter.
    for (char c : f.path)
        ed.handleEvent(insert(c));
    ed.handleEvent(nl);                         // Enter: commitSaveAs
    CHECK(ed.active().filename == f.path);
    CHECK(!ed.active().modified);
    CHECK(ed.active().document.snapshot() ==
          (std::vector<std::string>{"Hola", "mundo"}));   // el texto se conserva

    // quit.
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // verificar filesystem: el archivo fue creado en disco con los bytes.
    CHECK_EQ(readBytes(f.path), "Hola\nmundo");
}

// ===========================================================================
// E2E-10 — Nuevo buffer -> Save As cancelado (P1)
//
//   Ctrl+K n -> escribir -> Save As -> Esc -> seguir editando -> quit
//
// Cancelar el prompt "Guardar archivo:" con Esc debe volver al modo previo
// SIN tocar nada: el buffer queda sin nombre, con su texto intacto y sigue
// modificado. Se puede seguir editando, y al salir no se escribe nada.
//
// Workflow determinista:
//   Ctrl+K n   : buffer nuevo sin nombre, activo
//   escribir   : "Hola" + Enter + "mundo"      -> [ "Hola", "mundo" ]
//   Save As    : Ctrl+K Ctrl+S                 -> state = SaveAs
//   Esc        : cancela el prompt             -> vuelve a Interaccion
//                 -> filename vacio, texto intacto, modified, sin guardar
//   editar     : MoveEnd + '!'                 -> [ "Hola", "mundo!" ]
//   quit       : Ctrl+K Ctrl+Q                 -> running_=false
//   verificar  : filename aun vacio; el archivo destino NO se creo
// ---------------------------------------------------------------------------
TEST(e2e_10_new_buffer_save_as_cancel_keeps_state) {
    TempFile f;                                 // ruta destino (nunca escrita)

    Editor ed;
    // Ctrl+K n: buffer nuevo sin nombre, activo.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));
    CHECK(ed.active().filename.empty());

    // escribir "Hola\nmundo".
    type(ed, "Hola");
    Event nl;
    nl.type = EventType::InsertNewline;
    ed.handleEvent(nl);
    type(ed, "mundo");
    CHECK(ed.active().document.snapshot() ==
          (std::vector<std::string>{"Hola", "mundo"}));
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().cursor.col, 5);        // fin de "mundo" (1,5)

    // Save As: Ctrl+K Ctrl+S -> prompt.
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));

    // elegir algo de ruta y luego cancelar con Esc (no llega a confirmarse).
    for (char c : f.path)
        ed.handleEvent(insert(c));
    press(ed, EventType::Escape);               // cancela el prompt

    // El estado se CONSERVA: volvio al modo previo (Interaccion), el buffer
    // sigue sin nombre, con su texto intacto y sigue modificado.
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK(ed.active().filename.empty());
    CHECK(ed.active().document.snapshot() ==
          (std::vector<std::string>{"Hola", "mundo"}));
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().cursor.col, 5);        // el cursor no se movio

    // seguir editando: anexar '!' a "mundo".
    press(ed, EventType::MoveEnd);              // (1,5)
    ed.handleEvent(insert('!'));                // -> "mundo!"
    CHECK(ed.active().document.snapshot() ==
          (std::vector<std::string>{"Hola", "mundo!"}));
    CHECK(ed.active().modified);

    // quit.
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);

    // cancelar el Save As NO escribio nada: sin nombre y sin archivo creado.
    CHECK(ed.active().filename.empty());
    CHECK(!std::filesystem::exists(f.path));
}

// ===========================================================================
// E2E-11 — Buffer selector: A/B/C/D (P1)
//
// Con 4 buffers (A B C D), abrir el selector y moverse:
//   Ctrl+K t -> Down -> Down -> Enter  (elige C)
//   -> edit C -> volver selector -> elegir A -> volver C
// Verificar que cambiar de buffer conserva su contenido y el estado
// esencial de cada buffer.
//
// El selector abre en el buffer activo; para que "Down Down -> C" sea
// determinista, el flujo arranca con A activo. B, C, D se crean con
// Ctrl+K n y se les escribe contenido (indices 0=A 1=B 2=C 3=D).
//
//   setup   : A="AAA" (archivo) ; B="BBB" ; C="CCC" ; D="DDD"  (activo=D)
//             -> selector Up Up Up -> A (activo A para arrancar)
//   Ctrl+K t: selector @A -> Down Down -> Enter -> activar C ("CCC")
//   edit C  : MoveEnd + '!'              -> C="CCC!"
//   volver  : Ctrl+K t @C -> Up Up -> Enter -> A ("AAA")
//   volver C: Ctrl+K t @A -> Down Down -> Enter -> C ("CCC!" conservado)
//   verificar: A="AAA", B="BBB", C="CCC!", D="DDD", y al final se activa C.
// ---------------------------------------------------------------------------
TEST(e2e_11_buffer_selector_abc_verify_states) {
    TempFile fileA;
    writeBytes(fileA.path, "AAA\n");

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(fileA.path));             // buffer 0 = A
    Event enter;
    enter.type = EventType::InsertNewline;

    // B, C, D: buffer nuevo + escribir contenido.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));
    type(ed, "BBB");                            // buffer 1 = B "BBB"
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));
    type(ed, "CCC");                            // buffer 2 = C "CCC"
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));
    type(ed, "DDD");                            // buffer 3 = D "DDD" (activo)
    CHECK_EQ(ed.buffers.count(), 4);

    // Activar A para arrancar el flujo desde el tope del selector.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // selector @ D(3)
    press(ed, EventType::MoveUp);               // -> ...
    press(ed, EventType::MoveUp);
    press(ed, EventType::MoveUp);               // -> 0 (A)
    ed.handleEvent(enter);                      // activar A
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");

    // ---- Ctrl+K t -> Down -> Down -> Enter: activar C (indice 2).
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::BufferSelector));
    press(ed, EventType::MoveDown);             // 0 -> 1 (B)
    press(ed, EventType::MoveDown);             // 1 -> 2 (C)
    ed.handleEvent(enter);                      // activar C
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().document.lineAt(0), "CCC");

    // ---- editar C: anexar '!' -> "CCC!".
    press(ed, EventType::MoveEnd);              // (0,3)
    type(ed, "!");                              // C = "CCC!"
    CHECK_EQ(ed.active().document.lineAt(0), "CCC!");
    CHECK(ed.active().modified);

    // ---- volver selector y elegir A.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // selector @ C(2)
    press(ed, EventType::MoveUp);               // -> 1 (B)
    press(ed, EventType::MoveUp);               // -> 0 (A)
    ed.handleEvent(enter);                      // activar A
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");
    CHECK(!ed.active().modified);               // A intacto/no modificado

    // ---- volver a C: el buffer C conserva su edicion "CCC!".
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // selector @ A(0)
    press(ed, EventType::MoveDown);             // -> 1 (B)
    press(ed, EventType::MoveDown);             // -> 2 (C)
    ed.handleEvent(enter);                      // activar C
    CHECK_EQ(ed.active().document.lineAt(0), "CCC!");
    CHECK_EQ(ed.active().cursor.col, 4);        // cursor conservado (fin "CCC!")

    // ---- verificar todos los estados: B y D quedaron como estaban.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // selector @ C(2)
    press(ed, EventType::MoveDown);             // -> 3 (D)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "DDD");
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // selector @ D(3)
    press(ed, EventType::MoveUp);               // -> 2 (C)
    press(ed, EventType::MoveUp);               // -> 1 (B)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "BBB");

    // de vuelta a C (el estado final pedido), aun con su edicion.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('t'));                // selector @ B(1)
    press(ed, EventType::MoveDown);             // -> 2 (C)
    ed.handleEvent(enter);
    CHECK_EQ(ed.active().document.lineAt(0), "CCC!");
    CHECK_EQ(ed.buffers.count(), 4);            // nadie se perdio
}

// ===========================================================================
// E2E-12 — Binario / byte-safe (P0)
//
// Un archivo que contiene TODOS los bytes 0x00..0xFF en orden. El \n (0x0A)
// actua de separador de linea, por lo que el archivo queda partido en varias
// lineas; el resto son bytes de contenido (incluidos 0x00 NUL y 0x80..0xFF).
//
// El objetivo es DETECTAR una conversion accidental de char/UTF-8 al cargar,
// mover el cursor, editar, deshacer o guardar. Si el editor convirtiera bytes
// en algo (validacion/codificacion), el conteo de bytes romperia.
//
//   setup  : content = concatenacion de 0x00..0xFF; writeBytes(path, content)
//   open   : readBytes + serialize(documento) deben reproducir `content` EXACTO
//            (prueba de que abre sin conversion, incluida la cola sin \n).
//   move   : MoveEnd, MoveDown, MoveHome, MoveRight hasta bytes >=0x80 y MoveEnd (cursor sobre bytes altos)
//   edit   : insertar el byte crudo 0xE9 (secuencia UTF-8 incompleta/byte no ASCII) -> modified=true
//   save1  : Ctrl+K Ctrl+S -> readBytes(path) == contenido modificado (byte a byte)
//   undo   : restaurar el documento exacto -> modified=false
//   save2  : Ctrl+K Ctrl+S -> readBytes(path) == content original (byte a byte)
// ---------------------------------------------------------------------------
TEST(e2e_12_binary_bytes_00_to_ff_roundtrip) {
    std::string content;
    for (int i = 0; i <= 255; ++i)
        content.push_back(static_cast<char>(i));
    CHECK_EQ(content.size(), 256u);

    TempFile f;
    writeBytes(f.path, content);

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    Buffer& b = ed.active();

    // Serializa el documento como se guardaria en disco (lineas unidas por \n,
    // mas la cola si endsWithNewline). Comparar contra `content` verifica sin
    // conversion al abrir.
    auto serialize = [&b] {
        std::string s;
        int n = b.document.lineCount();
        for (int i = 0; i < n; ++i) {
            s += b.document.lineAt(i);
            if (i + 1 < n)
                s += "\n";
        }
        if (b.document.endsWithNewline())
            s += "\n";
        return s;
    };
    CHECK_EQ(serialize(), content);
    CHECK(!b.modified);

    // move: ejercitar el cursor sobre bytes arbitrarios (incluidos 0x80..0xFF).
    // La linea 0 es 0x00..0x09 y la linea 1 es 0x0B..0xFF; hay que llegar a >=0x80.
    press(ed, EventType::MoveEnd);   // (0,10) fin linea 0
    press(ed, EventType::MoveDown);  // (1,10) ~0x15 en linea 1
    press(ed, EventType::MoveHome);  // (1,0) -> 0x0B
    press(ed, EventType::MoveRight); // (1,1) -> 0x0C
    CHECK_EQ(b.cursor.line, 1);
    CHECK_EQ(b.cursor.col, 1);
    for (int i = 0; i < 116; ++i) press(ed, EventType::MoveRight); // (1,117) -> 0x80
    CHECK_EQ(b.cursor.col, 117);
    CHECK_EQ(static_cast<unsigned char>(b.document.lineAt(1)[117]), static_cast<unsigned char>(0x80));
    for (int i = 0; i < 10; ++i) press(ed, EventType::MoveRight); // recorre zona alta
    CHECK_EQ(b.cursor.col, 127);
    press(ed, EventType::MoveEnd); // (1,245) fin linea 1
    CHECK_EQ(b.cursor.col, b.document.lineLength(1));
    CHECK_EQ(serialize(), content);

    // edit: insertar el byte crudo 0xE9 (secuencia UTF-8 incompleta), bien en Interaccion.
    ed.handleEvent(insert('i'));                 // entrar a Interaccion
    Event raw;
    raw.type = EventType::InsertChar;
    raw.text = std::string(1, static_cast<char>(0xE9));
    ed.handleEvent(raw);                         // insertar 0xE9
    CHECK(b.modified);
    {
        std::string modified = serialize();
        CHECK_EQ(modified.size(), 257u);
        CHECK(modified != content);
        CHECK_EQ(static_cast<unsigned char>(modified.back()), static_cast<unsigned char>(0xE9));
        // save modificado y verificar byte a byte.
        saveViaS(ed);
        CHECK(!b.modified);
        CHECK_EQ(readBytes(f.path), modified);
        // El save deja el contenido modificado persistido; luego undo debe
        // seguir funcionando y volver al estado original, que ahora queda
        // nuevamente como modified=true respecto del archivo en disco.
        CHECK_EQ(serialize(), modified);
    }

    // undo: restaurar el documento exacto (incluida la cola sin \n).
    press(ed, EventType::Undo);
    CHECK(b.modified);
    CHECK_EQ(serialize(), content);

    // save: guardar y releer en disco byte a byte (vuelta al original).
    saveViaS(ed);
    CHECK(!b.modified);
    CHECK_EQ(readBytes(f.path), content);
}

// ===========================================================================
// E2E-13 — Error al guardar (P0)
//
// buffer nuevo -> editar -> intentar guardar (Save As) en un path invalido
// (directorio inexistente -> ofstream no puede abrir el archivo ->
// saveToFile()==false). La ruta se elige en el prompt de "Guardar archivo:",
// que es donde se escribe un path; en un buffer con nombre Ctrl+K Ctrl+S
// guarda directo (sin prompt), asi que el error de path solo se dispara aqui.
//
// Verificar:
//   - error visible     (statusMessage_ = "Error al guardar: <path>")
//   - contenido del buffer INTACTO (el error no toco el documento)
//   - modified == true  (los cambios no se marcaron como guardados)
//   - editor sigue funcionando (ESC vuelve a Navegacion y un Save As a una
//     ruta valida termina correctamente en disco)
// ---------------------------------------------------------------------------
TEST(e2e_13_save_error_invalid_path) {
    const std::string badParent = "/tmp/maestro_e2e13_missing_dir";
    const std::string badPath = badParent + "/out.txt";
    std::filesystem::remove_all(badParent);      // garantizar que NO existe

    TempFile f;                                 // ruta valida de recuperacion

    Editor ed;
    Event nl;
    nl.type = EventType::InsertNewline;

    // buffer nuevo (sin nombre) y escribir contenido.
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('n'));
    CHECK(ed.active().filename.empty());
    type(ed, "AAA_world");                       // -> Interaccion
    CHECK_EQ(ed.active().document.lineAt(0), "AAA_world");
    CHECK(ed.active().modified);

    // Save As a una ruta invalida: Ctrl+K Ctrl+S -> escribir path -> Enter.
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));
    ed.saveAsPath_.clear();
    for (char c : badPath)
        ed.handleEvent(insert(c));
    ed.handleEvent(nl);                          // Enter: intenta guardar
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));

    // error visible
    CHECK_EQ(ed.statusMessage_, "Error al guardar: " + badPath);
    CHECK(!std::filesystem::exists(badPath));    // nada se escribio en disco

    // contenido del buffer INTACTO y seguimos modificados
    CHECK_EQ(ed.active().document.lineAt(0), "AAA_world");
    CHECK(ed.active().modified);
    CHECK(ed.active().filename.empty());         // no se caso a la ruta mala

    // editor sigue funcionando: ESC sale del prompt (vuelve a priorState_,
    // que aqui es Interaccion, porque la edicion se hizo con type)...
    press(ed, EventType::Escape);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().document.lineAt(0), "AAA_world");   // intacto

    // ...y un Save As a una ruta VALIDA termina en disco ya sin error.
    prefix(ed, EventType::Prefix, EventType::Save);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::SaveAs));
    ed.saveAsPath_.clear();
    for (char c : f.path)
        ed.handleEvent(insert(c));
    ed.handleEvent(nl);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    CHECK_EQ(ed.active().filename, f.path);
    CHECK(!ed.active().modified);
    CHECK_EQ(ed.active().document.lineAt(0), "AAA_world");   // intacto
    CHECK_EQ(readBytes(f.path), "AAA_world");

    // limpieza
    std::filesystem::remove_all(badParent);
}

// ===========================================================================
// E2E-14 — Error al abrir desde el FileBrowser (P1)
//
//   loadIntoActiveBuffer A -> editar (historial) -> seleccionar -> Ctrl+K o -> FileBrowser
//   -> intentar abrir un archivo con error PermissionDenied -> error visible
//
// El editor NO debe: crashear, perder el buffer actual, perder la seleccion,
// perder el historial de undo/redo, ni cambiar accidentalmente de buffer.
//
// openFileInBuffer ante un error real (PermissionDenied) no crea buffer ni
// toca nada: solo pinta el error en la fila de mensajes. La verificacion es
// que TODO el estado previo sobrevive byte/columna a columna.
//
// NOTA: el mecanismo anterior con `chmod 000` no es determinista bajo root
// (root puede leer 000). Opcion B: filesystem inyectable. Document::loadFromFile
// consulta filesystem::callLoadHook; el test fuerza PermissionDenied para la
// ruta concreta sin depender de permisos reales del SO. Ya no es E2E puro
// pero es determinista en cualquier entorno (local, CI, contenedor, root).
//
// Arbol temporal (base/):
//   alpha.txt   "AAA\n"     (archivo A, se abre al inicio)
//   no_perm.txt "SECRET\n"  (inyectado como PermissionDenied via hook)
//
//   loadIntoActiveBuffer A       : loadIntoActiveBuffer(alpha.txt) -> buffer 0 activo, "AAA"
//   editar       : MoveEnd + "XYZ" -> "AAAXYZ" (historial de undo/redo)
//   seleccionar  : ESC (a Navegacion) + 's' (anchor=cursor) + MoveLeft x2
//   Ctrl+K o     : entra al FileBrowser; sembramos base/
//   abrir malo   : MoveDown x2 -> "no_perm.txt"; Enter -> error, sin cambio
// ---------------------------------------------------------------------------
TEST(e2e_14_filebrowser_open_error_preserves_state) {
    namespace fs = std::filesystem;

    const std::string base =
        "/tmp/edit_fb_" + std::to_string(::getpid()) + "_e2e14";
    testfw::TempDir baseDir(base);
    const std::string pathA = base + "/alpha.txt";
    const std::string pathNoPerm = base + "/no_perm.txt";

    fs::create_directories(base);
    writeBytes(pathA, "AAA\n");
    writeBytes(pathNoPerm, "SECRET\n");
    const std::string absNoPerm = fs::absolute(pathNoPerm).lexically_normal().string();
    filesystem::setLoadHook([pathNoPerm, absNoPerm](const std::string& p) -> std::optional<LoadResult> {
        if (p == pathNoPerm || p == absNoPerm) return LoadResult::PermissionDenied;
        return std::nullopt;
    });
    struct HookGuard { ~HookGuard(){ filesystem::clearLoadHook(); } } hookGuard;

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(pathA));                  // buffer 0 = A
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");

    // editar: crear historial de undo/redo (sin guardar) - sin coalescing.
    press(ed, EventType::MoveEnd);              // (0,3)
    type(ed, "XYZ");                            // -> "AAAXYZ"
    CHECK_EQ(ed.active().document.lineAt(0), "AAAXYZ");
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 3u); // verifica contrato: 3 ediciones por caracter

    // seleccionar "XY": ESC a Navegacion, 's' (anchor=cursor), MoveLeft x2.
    press(ed, EventType::Escape);               // Interaccion -> Navegacion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    ed.handleEvent(insert('s'));                // Seleccion, anchor=(0,6)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    press(ed, EventType::MoveLeft);             // position -> (0,5)
    press(ed, EventType::MoveLeft);             // position -> (0,4)
    CHECK(ed.hasSelection());

    // recordar el estado previo, para comparar que nada cambia.
    const std::string beforeDoc = ed.active().document.snapshot().empty()
                                      ? std::string()
                                      : ed.active().document.lineAt(0);
    CHECK(ed.active().selection.has_value());
    const auto anchorBefore = ed.active().selection->anchor;
    const auto posBefore = ed.active().selection->position;

    // Ctrl+K o: abrir el FileBrowser y sembrar base/ (ver E2E-08).
    press(ed, EventType::Prefix);
    ed.handleEvent(insert('o'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::FileBrowser));
    ed.fileBrowser.path_ = base;
    ed.fileBrowser.reload();
    ed.fileBrowser.index_ = 0;
    CHECK_EQ(ed.fileBrowser.displayNames_.size(), 3u);
    CHECK_EQ(ed.fileBrowser.displayNames_[0], "../");
    CHECK_EQ(ed.fileBrowser.displayNames_[1], "alpha.txt");
    CHECK_EQ(ed.fileBrowser.displayNames_[2], "no_perm.txt");

    // intentar abrir el archivo sin permisos.
    Event enter;
    enter.type = EventType::InsertNewline;
    press(ed, EventType::MoveDown);             // 0 -> 1 "alpha.txt"
    press(ed, EventType::MoveDown);             // 1 -> 2 "no_perm.txt"
    ed.handleEvent(enter);                      // Enter: intenta abrir

    // --- error visible, editor vivo, nada cambio ---
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::FileBrowser));
    CHECK(ed.statusMessage_.find("Sin permisos de lectura:") == 0);
    CHECK(ed.statusMessage_.find(pathNoPerm) != std::string::npos);

    CHECK_EQ(ed.buffers.count(), 1);            // NO se creo buffer nuevo
    CHECK(ed.active().filename == pathA);       // sigue el mismo buffer (A)
    CHECK_EQ(ed.active().document.lineAt(0), beforeDoc);   // contenido intacto
    CHECK(ed.active().modified);                // sigue sin guardar
    CHECK_EQ(ed.active().undoStack.size(), 3u); // historial intacto: 3 entradas
    CHECK_EQ(ed.active().redoStack.size(), 0u);

    // historial intacto: undo por caracter -> "AAA", redo -> "AAAXYZ".
    press(ed, EventType::Escape);               // FileBrowser -> Seleccion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    CHECK(ed.hasSelection());                   // seleccion intacta
    CHECK(ed.active().selection->anchor == anchorBefore);
    CHECK(ed.active().selection->position == posBefore);

    press(ed, EventType::Escape);               // Seleccion -> Navegacion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    // historial intacto: undo por caracter -> "AAA", redo -> "AAAXYZ".
    for (int i = 0; i < 3; ++i)
        press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "AAA");
    for (int i = 0; i < 3; ++i)
        press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "AAAXYZ");
    CHECK(ed.active().filename == pathA);       // nunca se cambio de buffer
}
// E2E-15 — Tabulacion de una seleccion y persistencia en disco (P0)
//
//   seleccionar (s + flecha)
//   -> tabular con '}'
//   -> guardar (Ctrl+K Ctrl+S)
//   -> releer el archivo FUERA del editor y comparar bytes.
//
// Confirma que saveToFile/loadFromFile no normalizan ni corrompen el
// whitespace agregado por la tabulacion: los espacios persistidos se
// releen exactos.
// ---------------------------------------------------------------------------
TEST(e2e_15_indent_selection_save_byte_exact) {
    TempFile f;
    writeBytes(f.path, "aaa\nbbb\nccc\n");     // LF, con '\n' final

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    ed.handleEvent(insert('s'));               // entrar a Seleccion (anchor 0,0)
    press(ed, EventType::MoveDown);            // (1,0)
    press(ed, EventType::MoveRight);           // (1,1): seleccion lineas 0..1
    ed.handleEvent(insert('}'));               // tabular las lineas 0..1
    CHECK_EQ(ed.active().document.lineAt(0), "    aaa");
    CHECK_EQ(ed.active().document.lineAt(1), "    bbb");
    CHECK_EQ(ed.active().document.lineAt(2), "ccc");   // fuera del rango

    press(ed, EventType::Escape);              // -> Navegacion
    saveViaS(ed);

    CHECK_EQ(readBytes(f.path), "    aaa\n    bbb\nccc\n");
}

// ===========================================================================
// E2E-16 — Borrar hasta buffer vacío (P0)
//
//   open "abc\n" -> borrar todo -> modified -> save -> quit
//
// Cubre el workflow crítico: editar → borrar todo → dejar buffer vacío.
// Verifica documento vacío, flag modified, resultado de save, bytes en disco
// y comportamiento de quit (no bloqueado por modified tras guardar).
// ---------------------------------------------------------------------------
TEST(e2e_16_delete_to_empty_and_save_byte_exact) {
    TempFile f;
    writeBytes(f.path, "abc\n");

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "abc");
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);

    auto serialize = [&ed] {
        const Buffer& b = ed.active();
        std::string s;
        int n = b.document.lineCount();
        for (int i = 0; i < n; ++i) {
            s += b.document.lineAt(i);
            if (i + 1 < n) s += "\n";
        }
        if (b.document.endsWithNewline()) s += "\n";
        return s;
    };

    // borrar todo: MoveEnd + entrar a Interaccion + 3x Backspace -> ""
    press(ed, EventType::MoveEnd);              // (0,3)
    CHECK_EQ(ed.active().cursor.col, 3);
    ed.handleEvent(insert('i'));                // Navegacion -> Interaccion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "ab");
    CHECK(ed.active().modified);
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "a");
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().document.lineCount(), 1);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 3u);

    std::string afterDelete = serialize();
    CHECK_EQ(afterDelete.size(), ed.active().document.endsWithNewline() ? 1u : 0u);

    // save: Ctrl+K Ctrl+S -> modified=false, disco byte-exacto
    saveViaS(ed);
    CHECK(!ed.active().modified);
    CHECK_EQ(readBytes(f.path), afterDelete);
    CHECK_EQ(serialize(), afterDelete);
    CHECK_EQ(ed.active().document.lineAt(0), "");
    CHECK_EQ(ed.active().document.lineCount(), 1);

    // quit: Ctrl+K Ctrl+Q -> running=false (no bloqueado)
    prefix(ed, EventType::Prefix, EventType::Quit);
    CHECK(!ed.running_);
    CHECK_EQ(readBytes(f.path), afterDelete);
}

// ===========================================================================
// E2E-18 — Backward selection (anchor > position) (P0)
//
// Todos los tests principales usan anchor < position (forward). Falta
// anchor > position (backward), caso clasico que rompe comparaciones de
// rangos: seleccion invertida, delete, replace y undo, tanto single-line
// como multiline invertida.
// ---------------------------------------------------------------------------
TEST(e2e_18_backward_selection_delete_replace_undo) {
    TempFile f;
    writeBytes(f.path, "hello world\nsecond line\nthird line\n");
    const std::vector<std::string> original = {"hello world", "second line", "third line"};

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK(ed.active().document.snapshot() == original);

    // ---- backward single-line delete: anchor 8 > position 3 -> [3,8) "lo wo"
    for (int i = 0; i < 8; ++i) press(ed, EventType::MoveRight); // (0,8) 'r'
    ed.handleEvent(insert('s')); // anchor (0,8)
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Seleccion));
    for (int i = 0; i < 5; ++i) press(ed, EventType::MoveLeft); // (0,3)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 8);
    CHECK_EQ(ed.active().selection->position.col, 3);
    CHECK_EQ(ed.active().selection->anchor.line, 0);
    CHECK_EQ(ed.active().selection->position.line, 0);
    // normalized start 3 end 8
    {
        auto sel = ed.selection();
        CHECK(sel.has_value());
        CHECK_EQ(sel->start.col, 3);
        CHECK_EQ(sel->end.col, 8);
    }
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineAt(0), "helrld");
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"helrld", "second line", "third line"}));
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK_EQ(ed.active().cursor.col, 3);
    // undo restaura texto + seleccion backward exacta
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 8);
    CHECK_EQ(ed.active().selection->position.col, 3);
    CHECK_EQ(ed.active().cursor.col, 3);
    // redo reproduce borrado
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "helrld");
    CHECK(!ed.hasSelection());
    // undo de nuevo para fase replace
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK(ed.hasSelection());
    // Escape limpia para siguiente fase
    press(ed, EventType::Escape);
    CHECK(!ed.hasSelection());
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));

    // ---- backward single-line replace: mismo rango [3,8) reemplazado por "XYZ"
    // reconstruir seleccion backward
    press(ed, EventType::MoveHome); // (0,0)
    for (int i = 0; i < 8; ++i) press(ed, EventType::MoveRight); // (0,8)
    ed.handleEvent(insert('s')); // anchor 8
    for (int i = 0; i < 5; ++i) press(ed, EventType::MoveLeft); // (0,3)
    CHECK_EQ(ed.active().selection->anchor.col, 8);
    CHECK_EQ(ed.active().selection->position.col, 3);
    // primer caracter reemplaza y abre grupo coalescente
    size_t beforeReplace = ed.active().undoStack.size();
    ed.handleEvent(insert('X'));
    CHECK_EQ(ed.active().document.lineAt(0), "helXrld");
    CHECK_EQ(ed.active().undoStack.size(), beforeReplace + 1);
    size_t afterX = ed.active().undoStack.size();
    ed.handleEvent(insert('Y'));
    ed.handleEvent(insert('Z'));
    CHECK_EQ(ed.active().document.lineAt(0), "helXYZrld");
    CHECK_EQ(ed.active().undoStack.size(), afterX); // coalescido
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    // undo del replace restaura seleccion backward
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.col, 8);
    CHECK_EQ(ed.active().selection->position.col, 3);
    press(ed, EventType::Escape);
    CHECK(!ed.hasSelection());

    // ---- backward multiline delete: anchor (2,3) > position (0,3) -> [0,3)-(2,3)
    press(ed, EventType::MoveDown); // (1,3)
    press(ed, EventType::MoveDown); // (2,3)
    CHECK_EQ(ed.active().cursor.line, 2);
    CHECK_EQ(ed.active().cursor.col, 3);
    ed.handleEvent(insert('s')); // anchor (2,3)
    press(ed, EventType::MoveUp); // (1,3)
    press(ed, EventType::MoveUp); // (0,3)
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 2);
    CHECK_EQ(ed.active().selection->anchor.col, 3);
    CHECK_EQ(ed.active().selection->position.line, 0);
    CHECK_EQ(ed.active().selection->position.col, 3);
    {
        auto sel = ed.selection();
        CHECK_EQ(sel->start.line, 0);
        CHECK_EQ(sel->start.col, 3);
        CHECK_EQ(sel->end.line, 2);
        CHECK_EQ(sel->end.col, 3);
    }
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineAt(0), "helrd line");
    CHECK_EQ(ed.active().document.lineCount(), 1);
    // borrar [0,3)-(2,3) sobre ["hello world","second line","third line"] deja una sola linea "hel" + "rd line"
    // undo multiline restaura
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK(ed.hasSelection());
    CHECK_EQ(ed.active().selection->anchor.line, 2);
    CHECK_EQ(ed.active().selection->position.line, 0);
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "helrd line");
    CHECK(!ed.hasSelection());
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK(ed.hasSelection());
    press(ed, EventType::Escape);
    CHECK(!ed.hasSelection());
    CHECK(ed.active().document.snapshot() == original);
}

// ===========================================================================
// E2E-19 — Unión de líneas vía Backspace/Delete (P0)
//
// Operación estructural distinta del borrado normal: Backspace en col 0
// une con la línea anterior y Delete al final une con la siguiente.
// Se verifica merge, undo/redo y persistencia byte-exacta.
// ---------------------------------------------------------------------------
TEST(e2e_19_line_merge_backspace_delete) {
    TempFile f;
    writeBytes(f.path, "aaa\nbbb\nccc\n");
    const std::vector<std::string> original = {"aaa", "bbb", "ccc"};

    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK(ed.active().document.snapshot() == original);
    CHECK(!ed.active().modified);

    // ---- Backspace en (1,0) une "aaa" + "bbb" -> ["aaabbb","ccc"]
    press(ed, EventType::MoveDown); // (1,0)
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(insert('i')); // Navegacion -> Interaccion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "aaabbb");
    CHECK_EQ(ed.active().document.lineAt(1), "ccc");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 1u);
    // undo -> vuelve a 3 líneas, cursor (1,0)
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK_EQ(ed.active().cursor.line, 1);
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(!ed.active().modified);
    // redo -> reunion
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "aaabbb");
    CHECK_EQ(ed.active().cursor.col, 3);
    // undo de nuevo para probar Delete
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);

    // ---- Delete al final de línea 0 une "aaa" + "bbb" -> mismo resultado
    // cursor está en (1,0) tras undo; subir y ir al final de línea 0
    press(ed, EventType::MoveUp); // (0,0)
    press(ed, EventType::MoveEnd); // (0,3)
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    ed.handleEvent(insert('i')); // entrar a Interaccion para Delete
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineCount(), 2);
    CHECK_EQ(ed.active().document.lineAt(0), "aaabbb");
    CHECK_EQ(ed.active().document.lineAt(1), "ccc");
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.active().modified);
    // undo/redo del Delete
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK_EQ(ed.active().cursor.col, 3);
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "aaabbb");

    // save byte-exacto: "aaabbb\nccc\n"
    saveViaS(ed);
    CHECK(!ed.active().modified);
    CHECK_EQ(readBytes(f.path), "aaabbb\nccc\n");
    CHECK(ed.active().document.snapshot() == (std::vector<std::string>{"aaabbb", "ccc"}));

    // undo tras save -> vuelve a original pero modified=true
    press(ed, EventType::Undo);
    CHECK(ed.active().document.snapshot() == original);
    CHECK(ed.active().modified);
    // redo -> merge de nuevo
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "aaabbb");
    CHECK(!ed.active().modified);
    CHECK_EQ(readBytes(f.path), "aaabbb\nccc\n");
}

// ===========================================================================
// E2E-20 — UTF-8 Backspace/Delete sobre celda multibyte (P0)
//
// E2E-05 cubre delete con selección de "ñ", pero no Backspace/Delete
// directos sobre la celda: cursor después de "ñ" + Backspace, y cursor
// antes de "ñ" + Delete. Verifica límites de celdas UTF-8.
// ---------------------------------------------------------------------------
TEST(e2e_20_utf8_backspace_delete) {
    TempFile f;
    writeBytes(f.path, "ma" "\xC3\xB1" "ana\n");
    Editor ed;
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "ma" "\xC3\xB1" "ana");
    CHECK_EQ(ed.active().document.lineAt(0).size(), 7u);
    CHECK(!ed.active().modified);

    // ---- cursor después de "ñ" (col 4) + Backspace -> borra "ñ" (2 bytes)
    press(ed, EventType::MoveRight); // (0,1)
    press(ed, EventType::MoveRight); // (0,2) inicio "ñ"
    press(ed, EventType::MoveRight); // (0,4) después "ñ" (salta 2 bytes)
    CHECK_EQ(ed.active().cursor.col, 4);
    ed.handleEvent(insert('i')); // -> Interaccion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
    press(ed, EventType::Backspace);
    CHECK_EQ(ed.active().document.lineAt(0), "maana");
    CHECK_EQ(ed.active().document.lineAt(0).size(), 5u);
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
    CHECK_EQ(ed.active().undoStack.size(), 1u);
    // undo restaura "mañana" byte-exacto
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "ma" "\xC3\xB1" "ana");
    CHECK_EQ(ed.active().document.lineAt(0).size(), 7u);
    CHECK_EQ(ed.active().cursor.col, 4);
    CHECK(!ed.active().modified);
    // redo
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "maana");
    CHECK_EQ(ed.active().cursor.col, 2);
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "ma" "\xC3\xB1" "ana");

    // ---- cursor antes de "ñ" (col 2) + Delete -> borra "ñ"
    press(ed, EventType::Escape); // Interaccion -> Navegacion
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    press(ed, EventType::MoveHome); // (0,0)
    press(ed, EventType::MoveRight); // (0,1)
    press(ed, EventType::MoveRight); // (0,2) inicio "ñ"
    CHECK_EQ(ed.active().cursor.col, 2);
    ed.handleEvent(insert('i')); // -> Interaccion
    press(ed, EventType::Delete);
    CHECK_EQ(ed.active().document.lineAt(0), "maana");
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.active().modified);
    // undo restaura
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "ma" "\xC3\xB1" "ana");
    CHECK_EQ(ed.active().cursor.col, 2);
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "maana");
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "ma" "\xC3\xB1" "ana");
    CHECK(!ed.active().modified);

    // ---- persistencia byte-exacta: borrar con Backspace y guardar
    // mover a después de "ñ" y borrar de nuevo
    press(ed, EventType::Escape);
    press(ed, EventType::MoveHome);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight);
    press(ed, EventType::MoveRight); // (0,4)
    ed.handleEvent(insert('i'));
    press(ed, EventType::Backspace); // -> "maana"
    CHECK_EQ(ed.active().document.lineAt(0), "maana");
    saveViaS(ed);
    CHECK(!ed.active().modified);
    CHECK_EQ(readBytes(f.path), "maana\n");
    // undo tras save -> vuelve a "mañana" y modified=true
    press(ed, EventType::Undo);
    CHECK_EQ(ed.active().document.lineAt(0), "ma" "\xC3\xB1" "ana");
    CHECK(ed.active().modified);
    CHECK_EQ(readBytes(f.path), "maana\n");
    press(ed, EventType::Redo);
    CHECK_EQ(ed.active().document.lineAt(0), "maana");
    CHECK(!ed.active().modified);
}
