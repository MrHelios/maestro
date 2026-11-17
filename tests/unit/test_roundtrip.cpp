#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test_framework.h"
#include "document/Document.h"

namespace {

using testfw::TempFile;

// Lee un archivo como cadena de bytes cruda.
static std::string readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Escribe los bytes crudos tal cual (sin interpretar finales de linea).
static void writeBytes(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

// Un caso de la tabla: nombre + bytes crudos iniciales.
struct RtCase {
    const char* name;
    std::string content;
};

static void assertRoundTripTable(const std::vector<RtCase>& cases) {
    for (const RtCase& c : cases) {
        TempFile f;
        Document d1, d2;
        
        // escribir el archivo tal cual.
        writeBytes(f.path, c.content);
        
        // d1
        // load del original.
        // save (mismo path; un editor normal guarda sobre el mismo).
        CHECK_EQ(d1.loadFromFile(f.path), LoadResult::Success);
        CHECK(d1.saveToFile(f.path));
        
        // d2
        // reload del archivo ya guardado.
        CHECK_EQ(d2.loadFromFile(f.path), LoadResult::Success);

        // Propiedad: el archivo en disco quedo byte a byte como el original.
        const std::string original = c.content;
        const std::string persisted = readBytes(f.path);
        CHECK_EQ(persisted, original);
        if (persisted != original) {
            std::cout << "    [" << c.name << "] persist != original\n";
            std::cout << "    original (" << original.size() << "): "
                      << original.size() << " bytes\n";
            std::cout << "    persist  (" << persisted.size() << "): "
                      << persisted.size() << " bytes\n";
        }

        // ambos documentos cargados del mismo
        // archivo ven exactamente las mismas lineas y el mismo '\n' final.
        CHECK(d1.snapshot() == d2.snapshot());
        if (d1.snapshot() != d2.snapshot())
            std::cout << "    [" << c.name << "] snapshot difiere tras reload\n";
        CHECK(d1.endsWithNewline() == d2.endsWithNewline());
        if (d1.endsWithNewline() != d2.endsWithNewline())
            std::cout << "    [" << c.name << "] endsWithNewline difiere tras reload\n";
        CHECK_EQ(d1.lineEnding(), d2.lineEnding());
        if (d1.lineEnding() != d2.lineEnding())
            std::cout << "    [" << c.name << "] lineEnding difiere tras reload\n";
    }
}

} // namespace

TEST(roundtrip_empty_file) {
    assertRoundTripTable({
        {"empty",""},
    });
}

TEST(roundtrip_one_line) {
    assertRoundTripTable({
        {"one_line", "hola"},
        {"one_line_with_lf", "hola\n"},
        {"one_line_crlf", "hola\r\n"},
    });
}

TEST(roundtrip_multiple_lines) {
    assertRoundTripTable({
        {"multi_lf", "a\nb\nc\n"},
        {"multi_lf_no_trailing", "a\nb\nc"},
        {"multi_crlf", "a\r\nb\r\nc\r\n"},
        {"multi_crlf_no_trailing", "a\r\nb\r\nc"},
    });
}

// El caso del '\n' final: 
// el modelo de lineas no representa la nueva linea final, asi que loadFromFile + saveToFile deben llevar el flag
// sincronizado. Si se desincroniza, el archivo reaparece igual o con un '\n' de mas/menos.
TEST(roundtrip_trailing_newline_flag) { 
    assertRoundTripTable({
        {"trailing", "abc\ndef\n"},
        {"no_trailing", "abc\ndef"},
        {"trailing_single_empty_line", "\n"},
    });
}

// Bytes UTF-8 multibyte: 
// deben sobrevivir intactos (editor binariamente seguro), 
// tanto ASCII dentro de lineas como secuencias multibyte alrededor.
TEST(roundtrip_utf8) {
    assertRoundTripTable({
        {"utf8_accents", "caf\xc3\xa9 \xe2\x80\x94 \xf0\x9f\x98\x80\n"},
        {"utf8_multiline", "h\xc3\xa9llo\nm\xc3\xa9t\nb\xc3\xbat\n"},
        // OJO: 
        // tras el byte multibyte va un caracter ASCII que es digito hex ('b','f'); 
        // hay que separar los literales o el escape \xa1b
        {"utf8_crlf", "\xc3\xa1" "b\r\n" "\xc3\xa9" "f\r\n"},
    });
}

// Bytes arbitrarios no-UTF-8 y con bytes de control no deben destruirse:
TEST(roundtrip_byte_safety) {
    assertRoundTripTable({
        {"latin1", "caf\xe9\na\xff\xfe\n"},
        // std::string(const char*) se corta en el primer '\0'
        // hay que pasar el largo para que el NUL interno sobreviva.
        {"control_bytes", std::string("\x00\x01\x02\n\x1b\x07\n", 7)},
    });
}

// caso: guardando un emoji
// abc😀def
TEST(roundtrip_emoticon_safety) {
    assertRoundTripTable({
        {"emoji_con_n", "abc😀def"},
        // std::string(const char*) se corta en el primer '\0'
        // hay que pasar el largo para que el NUL interno sobreviva.
        {"emoji_sin_n", "abc😀def\n"},
    });
}

// Con la representacion canonica, una linea vacia final serializa un '\n' final. 
// El roundtrip preserva el contenido,
// pero el byte stream puede crecer en un '\n' final extra.
TEST(roundtrip_empty_final_line) {
    TempFile f1, f2, f3, f4;
    Document d1, d2, d3, d4, d5, d6, d7, d8;

    writeBytes(f1.path, "a\nb\n");
    
    // d1
    CHECK_EQ(d1.loadFromFile(f1.path), LoadResult::Success);
    CHECK(d1.saveToFile(f1.path));

    // d2 
    CHECK_EQ(d2.loadFromFile(f1.path), LoadResult::Success);
    CHECK(d1.snapshot() == d2.snapshot());
    CHECK(d1.endsWithNewline() == d2.endsWithNewline());
    
    writeBytes(f2.path, "a\n\n");
    
    // d3
    CHECK_EQ(d3.loadFromFile(f2.path), LoadResult::Success);
    CHECK(d3.saveToFile(f2.path));
    
    // d4
    CHECK_EQ(d4.loadFromFile(f2.path), LoadResult::Success);
    CHECK(d3.snapshot() == d4.snapshot());
    CHECK(d3.endsWithNewline() == d4.endsWithNewline());

    writeBytes(f3.path, "\n\n\n");
    
    // d5
    CHECK_EQ(d5.loadFromFile(f3.path), LoadResult::Success);
    CHECK(d5.saveToFile(f3.path));

    // d6
    CHECK_EQ(d6.loadFromFile(f3.path), LoadResult::Success);
    CHECK(d5.snapshot() == d6.snapshot());
    CHECK(d5.endsWithNewline() == d6.endsWithNewline());
    
    writeBytes(f4.path, "a\r\n\r\n");
    
    // d7
    CHECK_EQ(d7.loadFromFile(f4.path), LoadResult::Success);
    CHECK(d7.saveToFile(f4.path));
    
    // d8
    CHECK_EQ(d8.loadFromFile(f4.path), LoadResult::Success);
    CHECK(d7.snapshot() == d8.snapshot());
    CHECK(d7.endsWithNewline() == d8.endsWithNewline());
}

// Archivo grande: 
// el round-trip debe conservarse sin truncar nada,
// incluyendo si termina (o no) en '\n'.
TEST(roundtrip_huge_file) {
    TempFile f1, f2;
    std::string withNl, withoutNl;

    for (int i = 0; i < 20000; ++i)
        withNl += "linea " + std::to_string(i) + " de contenido\n";
    withoutNl = withNl + "ultima sin fin";

    writeBytes(f1.path, withNl);
    writeBytes(f2.path, withoutNl);

    struct Case { TempFile* f; const std::string* expected; };
    for (Case c : {Case{&f1, &withNl}, Case{&f2, &withoutNl}}) {
        Document d1, d2;

        CHECK_EQ(d1.loadFromFile(c.f->path), LoadResult::Success);
        CHECK(d1.saveToFile(c.f->path));

        CHECK_EQ(d2.loadFromFile(c.f->path), LoadResult::Success);
        CHECK_EQ(readBytes(c.f->path), *c.expected);
        CHECK(d1.snapshot() == d2.snapshot());
        CHECK(d1.endsWithNewline() == d2.endsWithNewline());
        CHECK_EQ(d1.lineEnding(), d2.lineEnding());
        CHECK_EQ(d1.lineCount(), d2.lineCount());
    }
}