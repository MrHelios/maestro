#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "core/Document.h"
#include "test_framework.h"

using Lines = std::vector<std::string>;
using testfw::TempFile;

static Document makeDoc(std::initializer_list<std::string> lines) {
    Document d;
    d.restore(Lines(lines));
    return d;
}

// Devuelve el contenido crudo del archivo (para verificar bytes exactos).
static std::string fileContent(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// 1. Inicio: documento vacio
// ---------------------------------------------------------------------------
TEST(doc_new_empty) {
    Document d;
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineLength(0), 0);
    CHECK_EQ(d.lineAt(0), "");
}

// ---------------------------------------------------------------------------
// 1. Abrir archivo existente
// ---------------------------------------------------------------------------
TEST(doc_load_empty_file) {
    TempFile f;
    f.write("");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "");
}

TEST(doc_load_one_line) {
    TempFile f;
    f.write("hola");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "hola");
}

TEST(doc_load_multiple_lines) {
    TempFile f;
    f.write("uno\ndos\ntres");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 3);
    CHECK_EQ(d.lineAt(0), "uno");
    CHECK_EQ(d.lineAt(1), "dos");
    CHECK_EQ(d.lineAt(2), "tres");
}

TEST(doc_load_trailing_newline) {
    TempFile f;
    f.write("a\nb\n");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "a");
    CHECK_EQ(d.lineAt(1), "b");
}

TEST(doc_load_no_trailing_newline) {
    TempFile f;
    f.write("x\ny");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(1), "y");
}

TEST(doc_load_crlf) {
    TempFile f;
    f.write("a\r\nb\r\n");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "a");
    CHECK_EQ(d.lineAt(1), "b");
}

TEST(doc_load_large_file) {
    TempFile f;
    std::string big;
    big.reserve(1024 * 1024);
    for (int i = 0; i < 100000; ++i)
        big += "linea\n";
    f.write(big);
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 100000);
    CHECK_EQ(d.lineAt(0), "linea");
    CHECK_EQ(d.lineAt(99999), "linea");
}

TEST(doc_load_long_lines) {
    TempFile f;
    std::string line(100000, 'x');
    f.write(line + "\nfin");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineLength(0), 100000);
    CHECK_EQ(d.lineAt(1), "fin");
}

// ---------------------------------------------------------------------------
// 1. Archivo inexistente / errores
// ---------------------------------------------------------------------------
TEST(doc_load_nonexistent) {
    Document d;
    CHECK_EQ(d.loadFromFile("/no/such/file/xyz_editor_never"), LoadResult::NotFound);
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "");
}

// Abrir un directorio no debe crashear: el fallo de lectura se traduce a un
// documento de una sola linea vacia (sin importar el valor de retorno).
TEST(doc_load_directory_no_crash) {
    Document d;
    d.loadFromFile("/tmp");
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "");
}

// Un archivo EXISTENTE sin permisos de lectura NO se trata como archivo nuevo:
// loadFromFile devuelve PermissionDenied y NO toca el contenido del documento.
// (Bajo root no se puede forzar el fallo de permisos, asi que esa parte se omite).
TEST(doc_load_permission_denied_does_not_touch_document) {
    if (::geteuid() == 0) return;

    TempFile f;
    f.write("contenido original\n");
    std::filesystem::permissions(f.path, std::filesystem::perms::none);

    Document d = makeDoc({"texto previo", "no debe borrarse"});
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::PermissionDenied);
    // El documento queda intacto, listo para seguir editando/guardando.
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "texto previo");
    CHECK_EQ(d.lineAt(1), "no debe borrarse");
}

// ---------------------------------------------------------------------------
// 2. Insertar un caracter
// ---------------------------------------------------------------------------
TEST(doc_insert_into_empty) {
    Document d;
    d.insertChar(0, 0, 'a');
    CHECK_EQ(d.lineAt(0), "a");
}

TEST(doc_insert_at_start) {
    Document d = makeDoc({"bc"});
    d.insertChar(0, 0, 'a');
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_insert_middle) {
    Document d = makeDoc({"ac"});
    d.insertChar(0, 1, 'b');
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_insert_at_end) {
    Document d = makeDoc({"ab"});
    d.insertChar(0, 2, 'c');
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_insert_col_beyond_clamps) {
    Document d = makeDoc({"ab"});
    d.insertChar(0, 100, 'Z');
    CHECK_EQ(d.lineAt(0), "abZ");
}

TEST(doc_insert_col_negative_clamps_to_start) {
    Document d = makeDoc({"bc"});
    d.insertChar(0, -1, 'Z');
    CHECK_EQ(d.lineAt(0), "Zbc");
}

TEST(doc_insert_invalid_line_ignored) {
    Document d = makeDoc({"abc"});
    d.insertChar(50, 0, 'a');
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_restore_empty_vector) {
    Document d;
    d.restore(Lines{});
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "");
}

TEST(doc_insert_many_characters) {
    Document d;
    const std::string word = "hola";
    for (size_t i = 0; i < word.size(); ++i)
        d.insertChar(0, static_cast<int>(i), word[i]);
    CHECK_EQ(d.lineAt(0), "hola");
}

TEST(doc_insert_thousands) {
    Document d;
    const int n = 5000;
    for (int i = 0; i < n; ++i)
        d.insertChar(0, i, 'x');
    CHECK_EQ(d.lineLength(0), n);
    CHECK_EQ(d.lineAt(0), std::string(n, 'x'));
}

TEST(doc_insert_special_chars) {
    Document d;
    d.insertChar(0, 0, ' ');
    d.insertChar(0, 1, '\t');
    d.insertChar(0, 2, ',');
    d.insertChar(0, 3, '.');
    d.insertChar(0, 4, '!');
    CHECK_EQ(d.lineAt(0), " \t,.!");
}

// ---------------------------------------------------------------------------
// 3. InsertNewline
// ---------------------------------------------------------------------------
TEST(doc_newline_empty) {
    Document d;
    d.insertNewline(0, 0);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "");
    CHECK_EQ(d.lineAt(1), "");
}

TEST(doc_newline_start) {
    Document d = makeDoc({"abc"});
    d.insertNewline(0, 0);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "");
    CHECK_EQ(d.lineAt(1), "abc");
}

TEST(doc_newline_middle) {
    Document d = makeDoc({"abcd"});
    d.insertNewline(0, 2);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "ab");
    CHECK_EQ(d.lineAt(1), "cd");
}

TEST(doc_newline_end) {
    Document d = makeDoc({"abc"});
    d.insertNewline(0, 3);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "abc");
    CHECK_EQ(d.lineAt(1), "");
}

TEST(doc_newline_repeated) {
    Document d;
    for (int i = 0; i < 5; ++i)
        d.insertNewline(0, 0);
    CHECK_EQ(d.lineCount(), 6);
}

TEST(doc_newline_many_lines) {
    Document d = makeDoc({"texto"});
    for (int i = 0; i < 100; ++i)
        d.insertNewline(0, 0);
    CHECK_EQ(d.lineCount(), 101);
    CHECK_EQ(d.lineAt(100), "texto");
}

TEST(doc_newline_oob_line_ignored) {
    Document d = makeDoc({"abc"});
    d.insertNewline(5, 0); // indice de linea invalido: no hace nada
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_newline_col_beyond_clamps_to_end) {
    // col fuera de rango se recorta al final de la linea: crea nueva linea vacia.
    Document d = makeDoc({"abc"});
    d.insertNewline(0, 1000);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "abc");
    CHECK_EQ(d.lineAt(1), "");
}

TEST(doc_newline_col_negative_clamps_to_start) {
    Document d = makeDoc({"abc"});
    d.insertNewline(0, -1);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "");
    CHECK_EQ(d.lineAt(1), "abc");
}

// ---------------------------------------------------------------------------
// 8. Backspace (deleteCharBefore)
// ---------------------------------------------------------------------------
TEST(doc_backspace_mid_line) {
    Document d = makeDoc({"abc"});
    CHECK(d.deleteCharBefore(0, 2));
    CHECK_EQ(d.lineAt(0), "ac");
}

TEST(doc_backspace_col_beyond_clamps_to_last) {
    // col fuera de rango se recorta al final: borra el ultimo caracter.
    Document d = makeDoc({"abc"});
    CHECK(d.deleteCharBefore(0, 500));
    CHECK_EQ(d.lineAt(0), "ab");
}

TEST(doc_backspace_col_negative_clamps_to_start) {
    // col negativa se recorta a 0: en la primera linea no hay nada que borrar.
    Document d = makeDoc({"abc"});
    CHECK(!d.deleteCharBefore(0, -1));
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_backspace_several) {
    Document d = makeDoc({"abcdef"});
    while (d.lineAt(0).size() > 3)
        CHECK(d.deleteCharBefore(0, static_cast<int>(d.lineAt(0).size())));
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_backspace_erase_whole_line) {
    Document d = makeDoc({"zzz"});
    while (!d.lineAt(0).empty())
        CHECK(d.deleteCharBefore(0, static_cast<int>(d.lineAt(0).size())));
    CHECK_EQ(d.lineAt(0), "");
    CHECK_EQ(d.lineCount(), 1);
}

TEST(doc_backspace_start_joins_previous) {
    Document d = makeDoc({"aa", "bb"});
    // Fundir lineas no borra bytes dentro de una linea: devuelve 0, pero
    // el efecto (unir ambas lineas) si ocurre.
    CHECK_EQ(d.deleteCharBefore(1, 0), 0);
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "aabb");
}

TEST(doc_backspace_first_line_noop) {
    Document d = makeDoc({"ab"});
    CHECK(!d.deleteCharBefore(0, 0));
    CHECK_EQ(d.lineAt(0), "ab");
}

TEST(doc_backspace_empty_doc_noop) {
    Document d;
    CHECK(!d.deleteCharBefore(0, 0));
    CHECK_EQ(d.lineCount(), 1);
}

TEST(doc_backspace_empty_line_merges) {
    Document d = makeDoc({"a", "", "b"});
    CHECK_EQ(d.deleteCharBefore(1, 0), 0); // funde: 0 bytes dentro de la linea
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "a");
    CHECK_EQ(d.lineAt(1), "b");
}

// ---------------------------------------------------------------------------
// 9. Delete (deleteCharAt)
// ---------------------------------------------------------------------------
TEST(doc_delete_mid_line) {
    Document d = makeDoc({"abc"});
    CHECK(d.deleteCharAt(0, 1));
    CHECK_EQ(d.lineAt(0), "ac");
}

TEST(doc_delete_col_beyond_range_noop) {
    Document d = makeDoc({"ab"});
    CHECK(!d.deleteCharAt(0, 100));
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "ab");
}

TEST(doc_delete_col_negative_clamps_to_start) {
    Document d = makeDoc({"abc"});
    CHECK(d.deleteCharAt(0, -1));
    CHECK_EQ(d.lineAt(0), "bc");
}

TEST(doc_delete_end_joins_next) {
    Document d = makeDoc({"ab", "cd"});
    CHECK_EQ(d.deleteCharAt(0, 2), 0); // funde: 0 bytes dentro de la linea
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abcd");
}

TEST(doc_delete_last_line_noop) {
    Document d = makeDoc({"ab"});
    d.insertNewline(0, 2);
    CHECK(!d.deleteCharAt(1, 2));
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(1), "");
}

TEST(doc_delete_empty_doc_noop) {
    Document d;
    CHECK(!d.deleteCharAt(0, 0));
    CHECK_EQ(d.lineCount(), 1);
}

TEST(doc_delete_till_whole_line) {
    Document d = makeDoc({"abc"});
    CHECK(d.deleteCharAt(0, 0));
    CHECK(d.deleteCharAt(0, 0));
    CHECK(d.deleteCharAt(0, 0));
    CHECK_EQ(d.lineAt(0), "");
}

// ---------------------------------------------------------------------------
// 12. Guardado (nivel Document)
// ---------------------------------------------------------------------------
TEST(doc_save_roundtrip) {
    TempFile f;
    Document d = makeDoc({"uno", "dos", "tres"});
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK_EQ(d2.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d2.lineCount(), 3);
    CHECK_EQ(d2.lineAt(0), "uno");
    CHECK_EQ(d2.lineAt(1), "dos");
    CHECK_EQ(d2.lineAt(2), "tres");
}

TEST(doc_save_trailing_empty_line_collapses) {
    // DECISION DE DISENO (especificacion, no accidente):
    // Una ultima linea vacia no tiene representacion en disco. saveToFile
    // escribe separadores ENTRE lineas (nunca uno tras la ultima), asi que
    // {"a","b",""} se guarda como "a\nb"; y loadFromFile de "a\nb" devuelve
    // 2 lineas. Por lo tanto {"a","b",""} y {"a","b"} son equivalentes al
    // persistir: la linea vacia final es un estado de memoria, no del archivo.
    // Si algun dia se quiere preservar la linea vacia final, hay que cambiar
    // saveToFile Y loadFromFile a la vez.
    TempFile f;
    Document d = makeDoc({"a", "b", ""});
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK_EQ(d2.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d2.lineCount(), 2);
    CHECK_EQ(d2.lineAt(1), "b");
}

TEST(doc_save_empty) {
    TempFile f;
    Document d;
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK_EQ(d2.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d2.lineCount(), 1);
    CHECK_EQ(d2.lineAt(0), "");
}

TEST(doc_save_large) {
    TempFile f;
    Document d;
    Lines big(50000, "linea grande de prueba");
    d.restore(big);
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK_EQ(d2.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d2.lineCount(), 50000);
    CHECK_EQ(d2.lineAt(0), "linea grande de prueba");
    CHECK_EQ(d2.lineAt(49999), "linea grande de prueba");
}

TEST(doc_save_to_directory_fails) {
    Document d = makeDoc({"x"});
    CHECK(!d.saveToFile("/tmp"));
}

// ---------------------------------------------------------------------------
// 12b. Newline final: abrir+guardar debe respetar (no perder) el '\n' final
// ---------------------------------------------------------------------------
TEST(doc_roundtrip_preserves_trailing_newline) {
    // Un archivo "bien formado" que termina en '\n' debe conservar ese
    // '\n' tras abrirlo y guardarlo (antes se perdia silenciosamente).
    TempFile f;
    f.write("a\nb\n");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "a");
    CHECK_EQ(d.lineAt(1), "b");
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "a\nb\n");
}

TEST(doc_roundtrip_preserves_no_trailing_newline) {
    // Un archivo SIN '\n' final no debe ganarse uno al guardar.
    TempFile f;
    f.write("a\nb");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "a\nb");
}

TEST(doc_open_save_single_line_without_newline) {
    TempFile f;
    f.write("hola");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "hola");
}

TEST(doc_open_save_single_line_with_newline) {
    TempFile f;
    f.write("hola\n");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "hola\n");
}

TEST(doc_roundtrip_trailing_newline_after_edit) {
    // Editar (agregar una frase) no debe quitar el '\n' final original.
    TempFile f;
    f.write("a\n");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    d.insertChar(0, d.lineLength(0), 'b');
    CHECK_EQ(d.lineAt(0), "ab");
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "ab\n");
}

TEST(doc_enter_at_end_of_newline_file_does_not_double) {
    // REGRESION (bug real): abrir "a\n" y apretar Enter al final crea una
    // linea vacia final ["a",""]. Antes el flag de '\n' final seguia en
    // true y saveToFile escribia el separador MAS el '\n' del flag:
    // "a\n\n" (el archivo ganaba una linea). El flag debe quedar en false
    // porque la linea vacia final ya aporta el '\n'.
    TempFile f;
    f.write("a\n");
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK(d.endsWithNewline());
    d.insertNewline(0, 1); // Enter al final de "a"
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(1), "");
    CHECK(!d.endsWithNewline());
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "a\n");
}

TEST(doc_fuse_trailing_empty_line_removes_newline) {
    // La linea vacia final (que serializa el '\n') se puede fundir con
    // Delete/Backspace, y eso QUITA el '\n' final de verdad. El flag se
    // mantiene consistente con el nuevo ultimo byte.
    Document d = makeDoc({"a", ""});
    CHECK(!d.endsWithNewline()); // la linea vacia final ya aporta el '\n'
    d.deleteCharAt(0, 1);        // fundir "a" con la linea vacia (devuelve 0)
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "a");
    CHECK(!d.endsWithNewline());
    TempFile f;
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "a");
}

TEST(doc_insert_block_trailing_empty_keeps_flag_consistent) {
    // insertBlock multilinea con ultima linea vacia: el flag no puede
    // quedar en true junto con la linea vacia final (doble '\n' al guardar).
    Document d = makeDoc({"a"});
    d.insertBlock(0, 1, {"x", ""}); // col 1: queda "ax" + linea vacia
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "ax");
    CHECK_EQ(d.lineAt(1), "");
    CHECK(!d.endsWithNewline());
    TempFile f;
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "ax\n");
}

// ---------------------------------------------------------------------------
// 14b. deleteRange: guarda de invariante de orden en una sola linea
// ---------------------------------------------------------------------------
TEST(doc_delete_range_requires_ordered_columns) {
    // Incluso si se llama con (sc > ec) en una sola linea (rango no
    // normalizado), deleteRange debe rechazarlo y NO borrar nada. Antes
    // el (ec - sc) negativo se convertia a size_t gigante y erase()
    // borraba desde sc hasta el final de la linea, silenciosamente.
    Document d = makeDoc({"abcdef"});
    CHECK(!d.deleteRange(0, 4, 0, 1)); // sc > ec -> rechazado
    CHECK_EQ(d.lineAt(0), "abcdef");   // nada se borro
    CHECK_EQ(d.lineCount(), 1);
}

TEST(doc_delete_range_ordered_same_line_still_works) {
    // La misma operacion con el rango bien ordenado (sc <= ec) si borra.
    Document d = makeDoc({"abcdef"});
    CHECK(d.deleteRange(0, 1, 0, 4));
    CHECK_EQ(d.lineAt(0), "aef");
}

// ---------------------------------------------------------------------------
// 14c. insertBlock: insertar un bloque de lineas en (line, col)
// ---------------------------------------------------------------------------
// Primitiva usada por pegar. Contrato:
//   - bloque de una linea: insercion inline (no parte la linea).
//   - bloque de varias lineas: parte la linea en col; la 1ra del bloque se
//     pega a la izquierda, las intermedias son lineas completas y la ultima
//     se une con la cola derecha.
//   - devuelve la posicion donde queda el cursor: final de la ultima linea
//     insertada del bloque.
//   - bloque vacio o (line,col) invalido: no cambia nada y devuelve la
//     posicion sin modificar.
// ---------------------------------------------------------------------------
TEST(doc_insert_block_single_line_at_start) {
    // Bloque de una linea al comienzo de la linea.
    Document d = makeDoc({"abc"});
    Position p = d.insertBlock(0, 0, {"xyz"});
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "xyzabc");
    CHECK_EQ(p.line, 0);
    CHECK_EQ(p.col, 3);
}

TEST(doc_insert_block_single_line_in_middle) {
    // Bloque de una linea en el medio de la linea.
    Document d = makeDoc({"abcdef"});
    Position p = d.insertBlock(0, 3, {"xyz"});
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abcxyzdef");
    CHECK_EQ(p.line, 0);
    CHECK_EQ(p.col, 6);
}

TEST(doc_insert_block_single_line_at_end) {
    // Bloque de una linea al final de la linea.
    Document d = makeDoc({"abc"});
    Position p = d.insertBlock(0, 3, {"xyz"});
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abcxyz");
    CHECK_EQ(p.line, 0);
    CHECK_EQ(p.col, 6);
}

TEST(doc_insert_block_single_line_onto_empty_line) {
    // Bloque de una linea sobre una linea vacia: queda como unica linea.
    Document d = makeDoc({""});
    Position p = d.insertBlock(0, 0, {"xyz"});
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "xyz");
    CHECK_EQ(p.line, 0);
    CHECK_EQ(p.col, 3);
}

TEST(doc_insert_block_empty_block_is_noop) {
    // Bloque vacio: no cambia nada y devuelve (line,col) sin modificar.
    Document d = makeDoc({"abc", "def"});
    Position p = d.insertBlock(1, 1, {});
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "abc");
    CHECK_EQ(d.lineAt(1), "def");
    CHECK_EQ(p.line, 1);
    CHECK_EQ(p.col, 1);
}

TEST(doc_insert_block_invalid_position_is_noop) {
    // (line,col) fuera de rango: no cambia nada y devuelve la posicion.
    Document d = makeDoc({"abc"});
    Position p = d.insertBlock(5, 0, {"xyz"});   // linea inexistente
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abc");
    CHECK_EQ(p.line, 5);
    CHECK_EQ(p.col, 0);
}

TEST(doc_insert_block_two_lines_at_start) {
    // Bloque de dos lineas al comienzo de la linea.
    Document d = makeDoc({"ghi"});
    Position p = d.insertBlock(0, 0, {"abc", "def"});
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "abc");
    CHECK_EQ(d.lineAt(1), "defghi");
    CHECK_EQ(p.line, 1);
    CHECK_EQ(p.col, 3);
}

TEST(doc_insert_block_two_lines_in_middle) {
    // Bloque de dos lineas en el medio de la linea: la linea se parte en
    // col; la cola derecha se une a la ultima linea del bloque.
    Document d = makeDoc({"abcdef"});
    Position p = d.insertBlock(0, 3, {"abc", "def"});
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "abcabc");
    CHECK_EQ(d.lineAt(1), "defdef");
    CHECK_EQ(p.line, 1);
    CHECK_EQ(p.col, 3);
}

TEST(doc_insert_block_two_lines_at_end) {
    // Bloque de dos lineas al final de la linea.
    Document d = makeDoc({"abc"});
    Position p = d.insertBlock(0, 3, {"abc", "def"});
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "abcabc");
    CHECK_EQ(d.lineAt(1), "def");
    CHECK_EQ(p.line, 1);
    CHECK_EQ(p.col, 3);
}

TEST(doc_insert_block_splits_line_at_cursor) {
    // Caso CENTRAL: partir una linea en el cursor con un bloque de varias
    // lineas. "abcdef" con | = abc|def + bloque {X,Y,Z} debe dar:
    //   abcX
    //   Y
    //   Zdef
    // La parte izquierda queda al inicio, las intermedias son lineas
    // completas y la cola derecha se une a la ultima linea del bloque.
    // El Position retornado apunta EXACTAMENTE despues de "Z".
    Document d = makeDoc({"abcdef"});
    Position p = d.insertBlock(0, 3, {"X", "Y", "Z"});
    CHECK_EQ(d.lineCount(), 3);
    CHECK_EQ(d.lineAt(0), "abcX");   // izquierda + 1ra del bloque
    CHECK_EQ(d.lineAt(1), "Y");      // linea intermedia completa
    CHECK_EQ(d.lineAt(2), "Zdef");   // ultima del bloque + cola derecha
    CHECK_EQ(p.line, 2);             // ultima linea insertada del bloque
    CHECK_EQ(p.col, 1);              // inmediatamente despues de "Z"
}

TEST(doc_insert_block_many_lines) {
    // Bloque de cuatro lineas insertado en la linea 1 del documento.
    Document d = makeDoc({"cabecera", "fin"});
    Position p = d.insertBlock(1, 0, {"uno", "dos", "tres", "cuatro"});
    CHECK_EQ(d.lineCount(), 5);
    CHECK_EQ(d.lineAt(0), "cabecera");
    CHECK_EQ(d.lineAt(1), "uno");
    CHECK_EQ(d.lineAt(2), "dos");
    CHECK_EQ(d.lineAt(3), "tres");
    CHECK_EQ(d.lineAt(4), "cuatrofin");
    CHECK_EQ(p.line, 4);              // ultima linea insertada del bloque
    CHECK_EQ(p.col, 6);               // largo de "cuatro"
}

TEST(doc_insert_block_many_lines_in_middle_preserves_edges) {
    // Bloque de cuatro lineas en el medio de una linea existente: la parte
    // izquierda queda al inicio, la derecha se funde con la ultima linea.
    Document d = makeDoc({"XabY"});
    Position p = d.insertBlock(0, 1, {"uno", "dos", "tres", "cuatro"});
    CHECK_EQ(d.lineCount(), 4);
    CHECK_EQ(d.lineAt(0), "Xuno");
    CHECK_EQ(d.lineAt(1), "dos");
    CHECK_EQ(d.lineAt(2), "tres");
    CHECK_EQ(d.lineAt(3), "cuatroabY");
    CHECK_EQ(p.line, 3);
    CHECK_EQ(p.col, 6);
}

// ---------------------------------------------------------------------------
// 14d. extractRange: extraer texto como bloque de lineas (solo lectura)
// ---------------------------------------------------------------------------
// Primitiva usada por copiar/cortar. Contrato:
//   - una sola linea: vector de un elemento con el substring [sc, ec).
//   - multilinea:     1er el. = cola de sl desde sc; intermedios = lineas
//                      completas; ultimo el. = cabeza de el hasta ec.
//   - rango invalido (fuera de linea, invertido) o vacio (sc == ec):
//     vector vacio.
//   - NO modifica el documento (es la hermana de solo lectura de
//     deleteRange).
// ---------------------------------------------------------------------------
TEST(doc_extract_range_first_char) {
    Document d = makeDoc({"abcdef"});
    auto out = d.extractRange(0, 0, 0, 1);
    CHECK_EQ(out.size(), size_t{1});
    CHECK_EQ(out[0], "a");
}

TEST(doc_extract_range_last_char) {
    Document d = makeDoc({"abcdef"});
    auto out = d.extractRange(0, 5, 0, 6);
    CHECK_EQ(out.size(), size_t{1});
    CHECK_EQ(out[0], "f");
}

TEST(doc_extract_range_middle) {
    Document d = makeDoc({"abcdef"});
    auto out = d.extractRange(0, 1, 0, 4);
    CHECK_EQ(out.size(), size_t{1});
    CHECK_EQ(out[0], "bcd");
}

TEST(doc_extract_range_whole_line) {
    Document d = makeDoc({"abcdef"});
    auto out = d.extractRange(0, 0, 0, 6);
    CHECK_EQ(out.size(), size_t{1});
    CHECK_EQ(out[0], "abcdef");
}

TEST(doc_extract_range_empty_range_rejected) {
    // Rango vacio (sc == ec en una sola linea): vector vacio.
    Document d = makeDoc({"abcdef"});
    auto out = d.extractRange(0, 2, 0, 2);
    CHECK(out.empty());
}

TEST(doc_extract_range_reverse_single_line_rejected) {
    // Rango invertido en una sola linea (sc > ec): vector vacio.
    Document d = makeDoc({"abcdef"});
    auto out = d.extractRange(0, 4, 0, 1);
    CHECK(out.empty());
}

TEST(doc_extract_range_reverse_multiline_rejected) {
    // Rango invertido entre lineas (el < sl): vector vacio.
    Document d = makeDoc({"abc", "def", "ghi"});
    auto out = d.extractRange(2, 0, 0, 0);
    CHECK(out.empty());
}

TEST(doc_extract_range_out_of_bounds_rejected) {
    // Columna fuera de linea: vector vacio.
    Document d = makeDoc({"abc"});
    auto out = d.extractRange(0, 0, 0, 9);
    CHECK(out.empty());
    auto out2 = d.extractRange(5, 0, 5, 1); // linea inexistente
    CHECK(out2.empty());
}

TEST(doc_extract_range_half_to_half) {
    // Multilinea: desde mitad de la primera hasta mitad de la segunda.
    // Cola de sl desde sc + cabeza de el hasta ec.
    Document d = makeDoc({"abc", "def", "ghi"});
    auto out = d.extractRange(0, 1, 1, 2);
    CHECK_EQ(out.size(), size_t{2});
    CHECK_EQ(out[0], "bc");
    CHECK_EQ(out[1], "de");
}

TEST(doc_extract_range_start_to_end) {
    // Multilinea: desde el comienzo del doc hasta el final del doc.
    Document d = makeDoc({"abc", "def", "ghi"});
    auto out = d.extractRange(0, 0, 2, 3);
    CHECK_EQ(out.size(), size_t{3});
    CHECK_EQ(out[0], "abc");
    CHECK_EQ(out[1], "def");
    CHECK_EQ(out[2], "ghi");
}

TEST(doc_extract_range_whole_lines) {
    // Multilinea: lineas completas del medio (col 0 a col len).
    Document d = makeDoc({"abc", "def", "ghi"});
    auto out = d.extractRange(1, 0, 2, 3);
    CHECK_EQ(out.size(), size_t{2});
    CHECK_EQ(out[0], "def");
    CHECK_EQ(out[1], "ghi");
}

TEST(doc_extract_range_first_to_last_line) {
    // Multilinea: desde el inicio de la primera hasta el fin de la ultima.
    Document d = makeDoc({"abc", "def", "ghi"});
    auto out = d.extractRange(0, 0, 1, 3);
    CHECK_EQ(out.size(), size_t{2});
    CHECK_EQ(out[0], "abc");
    CHECK_EQ(out[1], "def");
}

TEST(doc_extract_range_utf8_single_char) {
    // "é" (2 bytes UTF-8): el rango respeta el byte de inicio/fin.
    Document d = makeDoc({std::string("\xC3\xA9")});
    auto out = d.extractRange(0, 0, 0, 2);
    CHECK_EQ(out.size(), size_t{1});
    CHECK_EQ(out[0], std::string("\xC3\xA9"));
}

TEST(doc_extract_range_utf8_em_dash) {
    // "—" (3 bytes UTF-8).
    Document d = makeDoc({std::string("\xE2\x80\x94")});
    auto out = d.extractRange(0, 0, 0, 3);
    CHECK_EQ(out.size(), size_t{1});
    CHECK_EQ(out[0], std::string("\xE2\x80\x94"));
}

TEST(doc_extract_range_utf8_emoji) {
    // "😀" (4 bytes UTF-8).
    Document d = makeDoc({std::string("\xF0\x9F\x98\x80")});
    auto out = d.extractRange(0, 0, 0, 4);
    CHECK_EQ(out.size(), size_t{1});
    CHECK_EQ(out[0], std::string("\xF0\x9F\x98\x80"));
}

TEST(doc_extract_range_utf8_mixed) {
    // Mezcla "é—😀" (2+3+4 bytes) y extraer subconjuntos por byte.
    Document d = makeDoc({std::string("\xC3\xA9\xE2\x80\x94\xF0\x9F\x98\x80")});
    auto e = d.extractRange(0, 0, 0, 2);       // "é"
    CHECK_EQ(e.size(), size_t{1});
    CHECK_EQ(e[0], std::string("\xC3\xA9"));
    auto em = d.extractRange(0, 2, 0, 5);      // "—"
    CHECK_EQ(em.size(), size_t{1});
    CHECK_EQ(em[0], std::string("\xE2\x80\x94"));
    auto emoji = d.extractRange(0, 5, 0, 9);   // "😀"
    CHECK_EQ(emoji.size(), size_t{1});
    CHECK_EQ(emoji[0], std::string("\xF0\x9F\x98\x80"));
    auto two = d.extractRange(0, 0, 0, 5);     // "é—"
    CHECK_EQ(two.size(), size_t{1});
    CHECK_EQ(two[0], std::string("\xC3\xA9\xE2\x80\x94"));
}

TEST(doc_extract_range_does_not_modify_document) {
    // Lo mas importante: extractRange es SOLO lectura. Snapshot antes y
    // despues debe ser identico, para single-line, multilinea y UTF-8.
    Document d = makeDoc({"abcdef", "ghijkl"});
    const auto before = d.snapshot();
    auto out = d.extractRange(0, 1, 1, 4);
    CHECK_EQ(out.size(), size_t{2});
    CHECK_EQ(out[0], "bcdef");
    CHECK_EQ(out[1], "ghij");      // cabeza de el hasta ec (exclusivo)
    CHECK(d.snapshot() == before); // el documento no cambio

    Document du = makeDoc({std::string("a\xC3\xA9\xF0\x9F\x98\x80")});
    const auto beforeU = du.snapshot();
    auto outU = du.extractRange(0, 1, 0, 3);
    CHECK_EQ(outU.size(), size_t{1});
    CHECK_EQ(outU[0], std::string("\xC3\xA9"));
    CHECK(du.snapshot() == beforeU); // ni con UTF-8
}

// ---------------------------------------------------------------------------
// 14e. extractRange() + insertBlock(): round-trip de integracion
// ---------------------------------------------------------------------------
// El flujo completo "extraer un bloque y reinsertarlo en otra posicion"
// debe reproducir el contenido extraido EXACTAMENTE (byte a byte). Aqui
// se comprueba la invariante: lo que sale de extractRange() es lo que
// insertBlock() vuelve a poner en el documento.
// ---------------------------------------------------------------------------
TEST(doc_roundtrip_ascii) {
    // Extraer "mundo" de "hola mundo" y reinsertarlo al comienzo de una
    // copia: el bloque aparece identico y el cursor apunta tras el.
    Document src = makeDoc({"hola mundo"});
    auto block = src.extractRange(0, 5, 0, 10);
    CHECK(block == (Lines{"mundo"}));

    Document dst = makeDoc({"hola mundo"});
    Position p = dst.insertBlock(0, 0, block);
    CHECK_EQ(dst.lineCount(), 1);
    CHECK_EQ(dst.lineAt(0), "mundohola mundo");
    CHECK_EQ(p.line, 0);
    CHECK_EQ(p.col, 5); // final de "mundo"
}

TEST(doc_roundtrip_utf8) {
    // Extraer "é — 😀" (bytes UTF-8 de 2/3/4 bytes) de un string mixto y
    // reinsertarlo sobre una linea vacia: los bytes llegan intactos.
    // "café é — 😀" = cafe(4B) + "é"(2B) + " "(1B) + "é"(2B) + " "(1B)
    // + "—"(3B) + " "(1B) + "😀"(4B) => el bloque mixto empieza en la
    // col 7 (4 + 2 + 1) y ocupa 11 bytes.
    const std::string mixto = std::string("\xC3\xA9") + " " + "\xE2\x80\x94" + " " + "\xF0\x9F\x98\x80";
    const std::string full = std::string("cafe\xC3\xA9") + " " + mixto;
    Document src = makeDoc({full});

    auto block = src.extractRange(0, 7, 0, static_cast<int>(full.size()));
    CHECK(block.size() == size_t{1});
    CHECK_EQ(block[0], mixto);

    Document dst;
    Position p = dst.insertBlock(0, 0, block);
    CHECK_EQ(dst.lineCount(), 1);
    CHECK_EQ(dst.lineAt(0), mixto);   // byte a byte identico
    CHECK_EQ(p.line, 0);
    CHECK_EQ(p.col, static_cast<int>(mixto.size()));
}

TEST(doc_roundtrip_multiline) {
    // Extraer un bloque multilinea (con linea intermedia completa) y
    // reinsertarlo en una linea vacia: las tres lineas salen exactas y el
    // cursor queda al final de la ultima insertada.
    Document src = makeDoc({"aaa", "bbb", "ccc", "ddd"});
    auto block = src.extractRange(1, 0, 3, 3);
    CHECK(block == (Lines{"bbb", "ccc", "ddd"}));

    Document dst;
    Position p = dst.insertBlock(0, 0, block);
    CHECK_EQ(dst.lineCount(), 3);
    CHECK_EQ(dst.lineAt(0), "bbb");
    CHECK_EQ(dst.lineAt(1), "ccc");
    CHECK_EQ(dst.lineAt(2), "ddd");
    CHECK_EQ(p.line, 2);              // ultima linea insertada
    CHECK_EQ(p.col, 3);               // final de "ddd"
}

TEST(doc_roundtrip_multiline_into_document) {
    // Extraer un bloque multilinea y reinsertarlo dentro de un documento
    // existente: cada linea del bloque aparece verbatim (la ultima con la
    // cola derecha del documento, segun contrato de insertBlock).
    // {"aaaa","bbbb","cccc"} desde (0,1) hasta (2,3):
    //   cola de line0 desde 1 = "aaa"; linea 1 completa = "bbbb";
    //   cabeza de line2 hasta 3 = "ccc".
    Document src = makeDoc({"aaaa", "bbbb", "cccc"});
    auto block = src.extractRange(0, 1, 2, 3);
    CHECK(block == (Lines{"aaa", "bbbb", "ccc"}));

    Document dst = makeDoc({"XabY"});
    Position p = dst.insertBlock(0, 1, block);
    CHECK_EQ(dst.lineCount(), 3);
    CHECK_EQ(dst.lineAt(0), "Xaaa");
    CHECK_EQ(dst.lineAt(1), "bbbb");
    CHECK_EQ(dst.lineAt(2), "cccabY");
    CHECK_EQ(p.line, 2);
    CHECK_EQ(p.col, 3);
}

// ---------------------------------------------------------------------------
// 15. Invariantes / contenido correcto
// ---------------------------------------------------------------------------
TEST(doc_invariants_after_ops) {
    Document d = makeDoc({"ab", "def"});

    d.insertChar(0, 1, 'X');   // line0: "aXb"
    d.insertNewline(0, 2);       // line0 "aX", line1 "b", line2 "def"
    d.deleteCharAt(1, 0);        // line1 "b" -> ""
    d.insertChar(1, 0, 'Z');     // line1 "Z"

    CHECK_EQ(d.lineCount(), 3);
    CHECK_EQ(d.lineAt(0), "aX");
    CHECK_EQ(d.lineAt(1), "Z");
    CHECK_EQ(d.lineAt(2), "def");
    CHECK_EQ(d.lineLength(0), 2);
    CHECK_EQ(d.lineLength(1), 1);
    CHECK_EQ(d.lineLength(2), 3);

    // No se pierde ni se duplica texto.
    std::string total;
    for (int i = 0; i < d.lineCount(); ++i)
        total += d.lineAt(i);
    CHECK_EQ(total, "aXZdef");
}

TEST(doc_no_unexpected_chars) {
    Document d = makeDoc({"abc"});
    d.insertChar(0, 1, 'X');
    CHECK(d.deleteCharBefore(0, 3));
    CHECK_EQ(d.lineAt(0), "aXc");
}

// ---------------------------------------------------------------------------
// 16. Archivo grande (1 MB y 10 MB)
// ---------------------------------------------------------------------------
TEST(doc_large_1MB_operations) {
    const int target = 1 << 20;
    Document d = makeDoc({std::string(target, 'z')});

    CHECK(d.deleteCharAt(0, target / 2));
    CHECK_EQ(d.lineLength(0), target - 1);

    d.insertChar(0, 0, 'A');
    CHECK_EQ(d.lineLength(0), target);
    CHECK_EQ(d.lineAt(0)[0], 'A');

    TempFile f;
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK_EQ(d2.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d2.lineCount(), 1);
    CHECK_EQ(d2.lineLength(0), target);
}

TEST(doc_large_10mb_roundtrip) {
    const int target = 10 * (1 << 20);
    TempFile f;
    {
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        CHECK(out.good());
        const std::string filler(65536, 'y');
        int written = 0;
        while (written < target) {
            out << filler;
            written += static_cast<int>(filler.size());
        }
        CHECK(out.good());
    }
    Document d;
    CHECK_EQ(d.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(d.lineCount(), 1);
    CHECK(d.lineLength(0) >= target - 65536);
    d.insertChar(0, 0, 'Q');
    CHECK(d.saveToFile(f.path));
}

TEST(doc_large_newline_split) {
    Document d = makeDoc({std::string(2000000, 'm')});
    d.insertNewline(0, 1000000);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineLength(0), 1000000);
    CHECK_EQ(d.lineLength(1), 1000000);
}

// ---------------------------------------------------------------------------
// 17. Operaciones repetidas
// ---------------------------------------------------------------------------
TEST(doc_repeated_insert_delete) {
    Document d;
    for (int i = 0; i < 20000; ++i)
        d.insertChar(0, 0, 'a');
    for (int i = 0; i < 20000; ++i)
        d.deleteCharAt(0, 0);
    CHECK_EQ(d.lineAt(0), "");
    CHECK_EQ(d.lineCount(), 1);
}

TEST(doc_repeated_newline_merge) {
    Document d;
    for (int i = 0; i < 1000; ++i)
        d.insertNewline(0, 0);
    for (int i = 0; i < 1000; ++i)
        d.deleteCharBefore(1, 0);
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "");
}

TEST(doc_repeated_edit_consistency) {
    Document d;
    for (int i = 0; i < 1000; ++i)
        d.insertChar(0, static_cast<int>(d.lineAt(0).size()), 'a');
    const int n = d.lineLength(0);
    for (int i = 0; i < 500; ++i)
        d.deleteCharBefore(0, static_cast<int>(d.lineAt(0).size()));
    CHECK_EQ(d.lineLength(0), n - 500);
    CHECK_EQ(d.lineAt(0).size(), std::size_t(n - 500));
}

// ---------------------------------------------------------------------------
// 22. Stress: fuzzer deterministico sobre Document
// ---------------------------------------------------------------------------
// Elige operaciones (y lineas/columnas, algunas fuera de rango) al azar y
// comprueba los invariantes tras cada paso: nunca 0 lineas y que la longitud
// que reporta lineLength sea exactamente el tamano del string de la linea.
// El numero de lineas se topa para que la comprobacion siga siendo O(1) por
// linea y el test corra rapido.
TEST(doc_stress_random_operations) {
    Document d;
    std::mt19937 rng(0xC0FFEE);

    const auto rnd = [&](int lo, int hi) {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(rng);
    };

    const int maxLines = 400;
    const int totalOps = 100000;

    for (int step = 0; step < totalOps; ++step) {
        // Linea valida o un poco fuera de rango, para ejercitar los clamps.
        const int line = rnd(0, d.lineCount() + 5);

        // Si la linea es valida, la columna se genera respecto a esa linea
        // (valida o apenas fuera de rango). Si la linea no existe, col arbitraria.
        int col;
        if (line < d.lineCount())
            col = rnd(0, d.lineLength(line) + 5);
        else
            col = rnd(0, 5);

        switch (rnd(0, 3)) {
            case 0:
                d.insertChar(line, col, static_cast<char>('a' + rnd(0, 25)));
                break;
            case 1:
                if (d.lineCount() < maxLines)
                    d.insertNewline(line, col);
                break;
            case 2:
                d.deleteCharAt(line, col);
                break;
            default:
                d.deleteCharBefore(line, col);
                break;
        }

        // Invariantes: nunca 0 lineas y lineLength == string::size().
        CHECK(d.lineCount() >= 1);
        for (int i = 0; i < d.lineCount(); ++i)
            CHECK_EQ(d.lineLength(i), static_cast<int>(d.lineAt(i).size()));
    }
}

// ---------------------------------------------------------------------------
// 23. Terminadores de linea (CRLF) se conservan al abrir+guardar
// ---------------------------------------------------------------------------
TEST(doc_crlf_round_trip_preserved) {
    // Un archivo Windows se detecta como CRLF, carga bien (sin '\r' en las
    // lineas) y al guardar vuelve a escribirse con CRLF: no se traduce a LF.
    TempFile src;
    src.write("a\r\nb\r\n");
    TempFile dst;

    Document d;
    CHECK_EQ(d.loadFromFile(src.path), LoadResult::Success);
    CHECK_EQ(d.lineEnding(), Document::LineEnding::CRLF);
    CHECK_EQ(d.lineAt(0), "a"); // el '\r' se quito de la linea interna
    CHECK_EQ(d.lineAt(1), "b");
    CHECK(d.endsWithNewline());

    CHECK(d.saveToFile(dst.path));
    CHECK_EQ(fileContent(dst.path), "a\r\nb\r\n");
}

TEST(doc_crlf_no_trailing_newline_preserved) {
    // CRLF sin una nueva linea final: el ultimo '\r\n' no es trailing.
    TempFile src;
    src.write("x\r\n"); // solo una linea con salto final
    TempFile dst;

    Document d;
    CHECK_EQ(d.loadFromFile(src.path), LoadResult::Success);
    CHECK_EQ(d.lineEnding(), Document::LineEnding::CRLF);
    CHECK(d.saveToFile(dst.path));
    CHECK_EQ(fileContent(dst.path), "x\r\n");
}

TEST(doc_lf_stays_lf) {
    // Un archivo Unix queda en LF; no se afecta por la logica de CRLF.
    TempFile src;
    src.write("a\nb");
    TempFile dst;

    Document d;
    CHECK_EQ(d.loadFromFile(src.path), LoadResult::Success);
    CHECK_EQ(d.lineEnding(), Document::LineEnding::LF);
    CHECK(!d.endsWithNewline()); // 'x\ny' no termina en salto
    CHECK(d.saveToFile(dst.path));
    CHECK_EQ(fileContent(dst.path), "a\nb");
}

TEST(doc_empty_file_is_lf) {
    TempFile src;
    src.write(""); // crear el archivo, aunque este vacio
    TempFile dst;

    Document d;
    CHECK_EQ(d.loadFromFile(src.path), LoadResult::Success);
    CHECK_EQ(d.lineEnding(), Document::LineEnding::LF);
    CHECK(d.saveToFile(dst.path));
    CHECK_EQ(fileContent(dst.path), "");
}

TEST(doc_new_document_defaults_lf) {
    Document d;
    CHECK_EQ(d.lineEnding(), Document::LineEnding::LF);
    d.setLineEnding(Document::LineEnding::CRLF);
    CHECK_EQ(d.lineEnding(), Document::LineEnding::CRLF);
}

TEST(doc_crlf_edit_then_save_preserves) {
    // Editar un archivo CRLF (agregar texto) y guardar: sigue CRLF.
    TempFile src;
    src.write("a\r\nb\r\n");
    TempFile dst;

    Document d;
    CHECK_EQ(d.loadFromFile(src.path), LoadResult::Success);
    d.insertChar(0, 1, 'Z'); // "aZ"
    CHECK(d.saveToFile(dst.path));
    CHECK_EQ(fileContent(dst.path), "aZ\r\nb\r\n");
}

TEST(doc_indent_line_adds_spaces_at_start) {
    Document d;
    d.restore(Lines({"if (x) {", "    foo();", "}", ""}));
    CHECK(d.indentLine(0, true, 4));
    CHECK_EQ(d.lineAt(0), "    if (x) {");
    CHECK_EQ(d.lineCount(), 4);   // no creo lineas
    CHECK(d.indentLine(2, true, 4));
    CHECK_EQ(d.lineAt(2), "    }");
}

TEST(doc_indent_line_empty_line) {
    Document d;
    d.restore(Lines({"", "x", ""}));
    CHECK(d.indentLine(0, true, 4));
    CHECK_EQ(d.lineAt(0), "    ");
}

TEST(doc_indent_line_bad_args_noop) {
    Document d;
    d.restore(Lines({"x", "y"}));
    CHECK(!d.indentLine(0, true, 0));    // indentLen <= 0
    CHECK(!d.indentLine(-1, true, 4));   // linea invalida
    CHECK(!d.indentLine(99, true, 4));   // fuera de rango
    CHECK_EQ(d.lineAt(0), "x");
}

TEST(doc_dedent_line_removes_up_to_level) {
    Document d;
    d.restore(Lines({"    foo();", "        inner();", "noindent", "\tTab", "  two", ""}));
    // Quita hasta 4 espacios.
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "foo();");
    // Dedenta en dos pasos una indentacion mas profunda.
    CHECK(d.indentLine(1, false, 4));
    CHECK_EQ(d.lineAt(1), "    inner();");
    CHECK(d.indentLine(1, false, 4));
    CHECK_EQ(d.lineAt(1), "inner();");
}

TEST(doc_dedent_line_tab_counts_one_level) {
    Document d;
    d.restore(Lines({"\tfoo();", "  "}));
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "foo();");     // un tab se quita entero
    CHECK(d.indentLine(1, false, 4));
    CHECK_EQ(d.lineAt(1), "");           // 2 espacios se quitan
}

TEST(doc_dedent_line_no_leading_whitespace_returns_false) {
    Document d;
    d.restore(Lines({"foo();", ""}));
    CHECK(!d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "foo();");
    CHECK(!d.indentLine(1, false, 4));   // linea vacia no cambia
}

TEST(doc_dedent_line_fewer_than_level_spaces_removes_only_present) {
    // La linea arranca con MENOS de indentLen espacios seguidos de
    // contenido: se quitan solo los que hay (no "mas alla" del contenido),
    // el contenido queda intacto y el resultado nunca es negativo.
    Document d;
    d.restore(Lines({"  foo", "   bar"}));
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "foo");
    CHECK(d.indentLine(1, false, 4));
    CHECK_EQ(d.lineAt(1), "bar");
}

TEST(doc_dedent_line_more_than_level_spaces_removes_only_level) {
    // La linea arranca con MAS de indentLen espacios: un solo dedent quita
    // exactamente indentLen, el resto (y el contenido) queda intacto.
    Document d;
    d.restore(Lines({"             foo"}));   // 13 espacios + "foo"
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "         foo");    // 9 espacios + "foo"
}

TEST(doc_dedent_line_leading_tab_removed_whole_is_not_spaces) {
    // Un tab inicial se quita ENTERO (un solo caracter), NO como si valiera
    // indentLen espacios ni se traduce a nada mas.
    Document d;
    d.restore(Lines({"\tfoo"}));
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "foo");
}

TEST(doc_dedent_line_no_indentation_returns_false_untouched) {
    // Las lineas SIN indentacion (arrancan con texto o estan vacias)
    // devuelven false y no se modifican.
    Document d;
    d.restore(Lines({"foo", "\t", ""}));
    CHECK(!d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "foo");
    CHECK(!d.indentLine(2, false, 4));
    CHECK_EQ(d.lineAt(2), "");
}

TEST(doc_dedent_line_mixed_space_tab_keys_off_first_char) {
    // " \tfoo": el criterio mira al primer caracter (un espacio), asi que
    // un dedent quita SOLO la corrida de espacios inicial (el tab la corta)
    // y deja "\tfoo"; un segundo dedent quita el tab y deja "foo". El tab
    // nunca se traduce a indentLen espacios.
    Document d;
    d.restore(Lines({" \tfoo"}));
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "\tfoo");
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), "foo");
}

TEST(doc_indent_line_out_of_range_safe) {
    // line negativo o >= lineCount(): devuelve false sin crashear, para
    // indent y dedent por igual.
    Document d;
    d.restore(Lines({"x", "y"}));
    CHECK(!d.indentLine(-1, true, 4));
    CHECK(!d.indentLine(99, true, 4));
    CHECK(!d.indentLine(-1, false, 4));
    CHECK(!d.indentLine(99, false, 4));
    CHECK_EQ(d.lineAt(0), "x");
    CHECK_EQ(d.lineAt(1), "y");
}

TEST(doc_indent_line_invalid_indentlen_noop_both_directions) {
    // indentLen <= 0: false sin modificar, para indent=true Y indent=false.
    Document d;
    d.restore(Lines({"  foo", "bar"}));
    CHECK(!d.indentLine(0, true, 0));
    CHECK(!d.indentLine(0, false, 0));
    CHECK(!d.indentLine(0, true, -2));
    CHECK(!d.indentLine(0, false, -2));
    CHECK_EQ(d.lineAt(0), "  foo");
    CHECK_EQ(d.lineAt(1), "bar");
}

TEST(doc_indent_line_preserves_multibyte_content) {
    // Indentar/desindentar inserta/quita bytes ASCII antes del contenido
    // sin corromper un caracter UTF-8 multibyte que arranque la linea.
    Document d;
    d.restore(Lines({std::string("\xC3\xA9x")}));        // "éx"
    CHECK(d.indentLine(0, true, 4));
    CHECK_EQ(d.lineAt(0), std::string("    \xC3\xA9x")); // "    éx"
    CHECK(d.indentLine(0, false, 4));
    CHECK_EQ(d.lineAt(0), std::string("\xC3\xA9x"));     // vuelve a "éx"
}