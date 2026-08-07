#include <fstream>
#include <string>
#include <vector>

#include "Document.h"
#include "test_framework.h"

using Lines = std::vector<std::string>;
using testfw::TempFile;

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
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "");
}

TEST(doc_load_one_line) {
    TempFile f;
    f.write("hola");
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "hola");
}

TEST(doc_load_multiple_lines) {
    TempFile f;
    f.write("uno\ndos\ntres");
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 3);
    CHECK_EQ(d.lineAt(0), "uno");
    CHECK_EQ(d.lineAt(1), "dos");
    CHECK_EQ(d.lineAt(2), "tres");
}

TEST(doc_load_trailing_newline) {
    TempFile f;
    f.write("a\nb\n");
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "a");
    CHECK_EQ(d.lineAt(1), "b");
}

TEST(doc_load_no_trailing_newline) {
    TempFile f;
    f.write("x\ny");
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(1), "y");
}

TEST(doc_load_crlf) {
    TempFile f;
    f.write("a\r\nb\r\n");
    Document d;
    CHECK(d.loadFromFile(f.path));
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
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 100000);
    CHECK_EQ(d.lineAt(0), "linea");
    CHECK_EQ(d.lineAt(99999), "linea");
}

TEST(doc_load_long_lines) {
    TempFile f;
    std::string line(100000, 'x');
    f.write(line + "\nfin");
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineLength(0), 100000);
    CHECK_EQ(d.lineAt(1), "fin");
}

// ---------------------------------------------------------------------------
// 1. Archivo inexistente / errores
// ---------------------------------------------------------------------------
TEST(doc_load_nonexistent_creates_empty) {
    TempFile f;
    Document d;
    CHECK(!d.loadFromFile(f.path));
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

TEST(doc_load_nonexistent_returns_false) {
    Document d;
    const std::string bad = "/no/such/file/xyz_editor_never";
    CHECK(!d.loadFromFile(bad));
    CHECK_EQ(d.lineCount(), 1);
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
    Document d;
    d.restore(Lines{"bc"});
    d.insertChar(0, 0, 'a');
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_insert_middle) {
    Document d;
    d.restore(Lines{"ac"});
    d.insertChar(0, 1, 'b');
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_insert_at_end) {
    Document d;
    d.restore(Lines{"ab"});
    d.insertChar(0, 2, 'c');
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_insert_col_beyond_clamps) {
    Document d;
    d.restore(Lines{"ab"});
    d.insertChar(0, 100, 'Z');
    CHECK_EQ(d.lineAt(0), "abZ");
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
    Document d;
    d.restore(Lines{"abc"});
    d.insertNewline(0, 0);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "");
    CHECK_EQ(d.lineAt(1), "abc");
}

TEST(doc_newline_middle) {
    Document d;
    d.restore(Lines{"abcd"});
    d.insertNewline(0, 2);
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "ab");
    CHECK_EQ(d.lineAt(1), "cd");
}

TEST(doc_newline_end) {
    Document d;
    d.restore(Lines{"abc"});
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
    Document d;
    d.restore(Lines{"texto"});
    for (int i = 0; i < 100; ++i)
        d.insertNewline(0, 0);
    CHECK_EQ(d.lineCount(), 101);
    CHECK_EQ(d.lineAt(100), "texto");
}

TEST(doc_newline_oob_line_ignored) {
    Document d;
    d.restore(Lines{"abc"});
    d.insertNewline(5, 0); // indice de linea invalido: no hace nada
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abc");
}

// ---------------------------------------------------------------------------
// 8. Backspace (deleteCharBefore)
// ---------------------------------------------------------------------------
TEST(doc_backspace_mid_line) {
    Document d;
    d.restore(Lines{"abc"});
    CHECK(d.deleteCharBefore(0, 2));
    CHECK_EQ(d.lineAt(0), "ac");
}

TEST(doc_backspace_several) {
    Document d;
    d.restore(Lines{"abcdef"});
    while (d.lineAt(0).size() > 3)
        CHECK(d.deleteCharBefore(0, static_cast<int>(d.lineAt(0).size())));
    CHECK_EQ(d.lineAt(0), "abc");
}

TEST(doc_backspace_erase_whole_line) {
    Document d;
    d.restore(Lines{"zzz"});
    while (!d.lineAt(0).empty())
        CHECK(d.deleteCharBefore(0, static_cast<int>(d.lineAt(0).size())));
    CHECK_EQ(d.lineAt(0), "");
    CHECK_EQ(d.lineCount(), 1);
}

TEST(doc_backspace_start_joins_previous) {
    Document d;
    d.restore(Lines{"aa", "bb"});
    CHECK(d.deleteCharBefore(1, 0));
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "aabb");
}

TEST(doc_backspace_first_line_noop) {
    Document d;
    d.restore(Lines{"ab"});
    CHECK(!d.deleteCharBefore(0, 0));
    CHECK_EQ(d.lineAt(0), "ab");
}

TEST(doc_backspace_empty_doc_noop) {
    Document d;
    CHECK(!d.deleteCharBefore(0, 0));
    CHECK_EQ(d.lineCount(), 1);
}

TEST(doc_backspace_empty_line_merges) {
    Document d;
    d.restore(Lines{"a", "", "b"});
    CHECK(d.deleteCharBefore(1, 0));
    CHECK_EQ(d.lineCount(), 2);
    CHECK_EQ(d.lineAt(0), "a");
    CHECK_EQ(d.lineAt(1), "b");
}

// ---------------------------------------------------------------------------
// 9. Delete (deleteCharAt)
// ---------------------------------------------------------------------------
TEST(doc_delete_mid_line) {
    Document d;
    d.restore(Lines{"abc"});
    CHECK(d.deleteCharAt(0, 1));
    CHECK_EQ(d.lineAt(0), "ac");
}

TEST(doc_delete_end_joins_next) {
    Document d;
    d.restore(Lines{"ab", "cd"});
    CHECK(d.deleteCharAt(0, 2));
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.lineAt(0), "abcd");
}

TEST(doc_delete_last_line_noop) {
    Document d;
    d.restore(Lines{"ab"});
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
    Document d;
    d.restore(Lines{"abc"});
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
    Document d;
    d.restore(Lines{"uno", "dos", "tres"});
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK(d2.loadFromFile(f.path));
    CHECK_EQ(d2.lineCount(), 3);
    CHECK_EQ(d2.lineAt(0), "uno");
    CHECK_EQ(d2.lineAt(1), "dos");
    CHECK_EQ(d2.lineAt(2), "tres");
}

TEST(doc_save_trailing_empty_line_collapses) {
    // Una linea vacia final equivale a "terminar con \n": al recargar
    // ambas representaciones de la misma forma (sin linea vacia extra).
    TempFile f;
    Document d;
    d.restore(Lines{"a", "b", ""});
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK(d2.loadFromFile(f.path));
    CHECK_EQ(d2.lineCount(), 2);
    CHECK_EQ(d2.lineAt(1), "b");
}

TEST(doc_save_empty) {
    TempFile f;
    Document d;
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK(d2.loadFromFile(f.path));
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
    CHECK(d2.loadFromFile(f.path));
    CHECK_EQ(d2.lineCount(), 50000);
    CHECK_EQ(d2.lineAt(0), "linea grande de prueba");
    CHECK_EQ(d2.lineAt(49999), "linea grande de prueba");
}

TEST(doc_save_to_directory_fails) {
    Document d;
    d.restore(Lines{"x"});
    CHECK(!d.saveToFile("/tmp"));
}

// ---------------------------------------------------------------------------
// 15. Invariantes / contenido correcto
// ---------------------------------------------------------------------------
TEST(doc_invariants_after_ops) {
    Document d;
    d.restore(Lines{"ab", "def"});

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
    Document d;
    d.restore(Lines{"abc"});
    d.insertChar(0, 1, 'X');
    CHECK(d.deleteCharBefore(0, 3));
    CHECK_EQ(d.lineAt(0), "aXc");
}

// ---------------------------------------------------------------------------
// 16. Archivo grande (1 MB y 10 MB)
// ---------------------------------------------------------------------------
TEST(doc_large_1MB_operations) {
    const int target = 1 << 20;
    Document d;
    d.restore(Lines{std::string(target, 'z')});

    CHECK(d.deleteCharAt(0, target / 2));
    CHECK_EQ(d.lineLength(0), target - 1);

    d.insertChar(0, 0, 'A');
    CHECK_EQ(d.lineLength(0), target);
    CHECK_EQ(d.lineAt(0)[0], 'A');

    TempFile f;
    CHECK(d.saveToFile(f.path));
    Document d2;
    CHECK(d2.loadFromFile(f.path));
    CHECK_EQ(d2.lineCount(), 1);
    CHECK_EQ(d2.lineLength(0), target);
}

TEST(doc_large_10mb_roundtrip) {
    const int target = 10 * (1 << 20);
    TempFile f;
    {
        std::ofstream out(f.path, std::ios::trunc);
        const std::string filler(65536, 'y');
        int written = 0;
        while (written < target) {
            out << filler;
            written += static_cast<int>(filler.size());
        }
    }
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK_EQ(d.lineCount(), 1);
    CHECK(d.lineLength(0) >= target - 65536);
    d.insertChar(0, 0, 'Q');
    CHECK(d.saveToFile(f.path));
}

TEST(doc_large_newline_split) {
    Document d;
    d.restore(Lines{std::string(2000000, 'm')});
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
        d.insertChar(0, static_cast<int>(d.lineAt(0).size()), 'z');
    const int n = d.lineLength(0);
    for (int i = 0; i < 500; ++i)
        d.deleteCharBefore(0, static_cast<int>(d.lineAt(0).size()));
    CHECK_EQ(d.lineLength(0), n - 500);
    CHECK_EQ(d.lineAt(0).size(), std::size_t(n - 500));
}