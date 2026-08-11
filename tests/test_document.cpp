#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "Document.h"
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
TEST(doc_load_nonexistent) {
    Document d;
    CHECK(!d.loadFromFile("/no/such/file/xyz_editor_never"));
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
    CHECK(d2.loadFromFile(f.path));
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
    CHECK(d.loadFromFile(f.path));
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
    CHECK(d.loadFromFile(f.path));
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "a\nb");
}

TEST(doc_open_save_single_line_without_newline) {
    TempFile f;
    f.write("hola");
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "hola");
}

TEST(doc_open_save_single_line_with_newline) {
    TempFile f;
    f.write("hola\n");
    Document d;
    CHECK(d.loadFromFile(f.path));
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "hola\n");
}

TEST(doc_roundtrip_trailing_newline_after_edit) {
    // Editar (agregar una frase) no debe quitar el '\n' final original.
    TempFile f;
    f.write("a\n");
    Document d;
    CHECK(d.loadFromFile(f.path));
    d.insertChar(0, d.lineLength(0), 'b');
    CHECK_EQ(d.lineAt(0), "ab");
    CHECK(d.saveToFile(f.path));
    CHECK_EQ(fileContent(f.path), "ab\n");
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
    CHECK(d2.loadFromFile(f.path));
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
    CHECK(d.loadFromFile(f.path));
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