#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test_framework.h"
#include "core/Document.h"

using testfw::TempFile;

// ---------------------------------------------------------------------------
// Round-trip de archivos: load -> save -> load
// ---------------------------------------------------------------------------
// La propiedad mas fuerte que puede pedirse de carga/guardado: tras abrir,
// guardar y volver a abrir, el contenido en DISCO debe ser EXACTAMENTE el
// original (comparacion byte a byte). Esto comprueba de una sola vez
// loadFromFile + saveToFile + deteccion de terminador + el flag del '\n'
// final: cualquier desincronizacion entre la representacion en lineas y el
// byte stream (p.ej. el problema del '\n' final, o un CRLF mal traducido)
// se delata sola.
//
// ALCANCE de la propiedad: el round-trip byte-exacto se exige SOLO para
// archivos con terminador HOMOGENEO (todo LF o todo CRLF) y que no pongan
// casos que el modelo de lineas no conserva por decision de diseño. En
// concreto NO aplica (dejado anotado, no testeado como propiedad):
//   - archivos con terminadores MIXTOS (p.ej. "a\nb\r\nc"): loadFromFile
//     detecta CRLF si cualquier linea lo usa (Document.cpp:50) y convierte
//     el archivo entero a CRLF al guardar. Decisión documentada en
//     Document.h (LineEnding).
//   - archivos que terminan en CR legacy (p.ej. "abc\ndef\r"): CR no se
//     auto-detecta en load (solo se conserva con setLineEnding), asi que el
//     '\r' final se pierde al guardar. Documentado en Document.h.
// Ambos son comportamientos intencionales, no bugs; este test los excluye
// para no exigir una propiedad que la especificacion no promete.

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
    const char* content;
};

// La tabla cubre todos los casos de borde que pueden desincronizar el
// modelo de lineas con el byte stream. '"..."' con ESC para cr/lf.
static void assertRoundTripTable(const std::vector<RtCase>& cases) {
    for (const RtCase& c : cases) {
        TempFile f;
        // Paso 1: escribir el archivo tal cual.
        writeBytes(f.path, c.content);
        // Paso 2: load del original.
        Document d1;
        CHECK_EQ(d1.loadFromFile(f.path), LoadResult::Success);
        // Paso 3: save (mismo path; un editor normal guarda sobre el mismo).
        CHECK(d1.saveToFile(f.path));
        // Paso 4: reload del archivo ya guardado.
        Document d2;
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

        // Reflexividad de la carga: ambos documentos cargados del mismo
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

TEST(roundtrip_empty_file) {
    assertRoundTripTable({
        {"empty", ""},
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

TEST(roundtrip_trailing_newline_flag) {
    // El caso del '\n' final: el modelo de lineas no representa la nueva
    // linea final, asi que loadFromFile + saveToFile deben llevar el flag
    // sincronizado. Si se desincroniza, el archivo reaparece igual o con
    // un '\n' de mas/menos.
    assertRoundTripTable({
        {"trailing", "abc\ndef\n"},
        {"no_trailing", "abc\ndef"},
        {"trailing_single_empty_line", "\n"},
    });
}

TEST(roundtrip_utf8) {
    // Bytes UTF-8 multibyte: deben sobrevivir intactos (editor
    // binariamente seguro), tanto ASCII dentro de lineas como secuencias
    // multibyte alrededor.
    assertRoundTripTable({
        {"utf8_accents", "caf\xc3\xa9 \xe2\x80\x94 \xf0\x9f\x98\x80\n"},
        {"utf8_multiline", "h\xc3\xa9llo\nm\xc3\xa9t\nb\xc3\xbat\n"},
        // OJO: tras el byte multibyte va un caracter ASCII que es digito
        // hex ('b','f'); hay que separar los literales o el escape \xa1b se
        // tragaria el 'b' como parte del byte (0x1b), no 'á' + 'b'.
        {"utf8_crlf", "\xc3\xa1" "b\r\n" "\xc3\xa9" "f\r\n"},
    });
}

TEST(roundtrip_empty_final_line) {
    // Una ultima linea vacia ("a\nb\n") NO es un '\n' final colgando: son
    // dos lineas "a" y "b" separadas. Debe representarse igual.
    assertRoundTripTable({
        {"empty_final_line", "a\nb\n"},
        {"two_empty_final_lines", "a\n\n"},
        {"only_newlines", "\n\n\n"},
        {"trailing_empty_lines_crlf", "a\r\n\r\n"},
    });
}

TEST(roundtrip_byte_safety) {
    // Bytes arbitrarios no-UTF-8 y con bytes de control no deben destruirse:
    // editor binariamente seguro.
    assertRoundTripTable({
        {"latin1", "caf\xe9\na\xff\xfe\n"},
        {"control_bytes", "\x00\x01\x02\n\x1b\x07\n"},
    });
}

TEST(roundtrip_huge_file) {
    // Archivo grande: el round-trip debe conservarse sin truncar nada,
    // incluyendo si termina (o no) en '\n'.
    std::string big;
    for (int i = 0; i < 20000; ++i)
        big += "linea " + std::to_string(i) + " de contenido\n";
    big += "ultima sin fin\n";

    TempFile f;
    writeBytes(f.path, big);
    Document d1;
    CHECK_EQ(d1.loadFromFile(f.path), LoadResult::Success);
    CHECK(d1.saveToFile(f.path));
    Document d2;
    CHECK_EQ(d2.loadFromFile(f.path), LoadResult::Success);
    CHECK_EQ(readBytes(f.path), big);
    CHECK(d1.snapshot() == d2.snapshot());
    CHECK(d1.endsWithNewline() == d2.endsWithNewline());
    CHECK_EQ(d1.lineEnding(), d2.lineEnding());
    CHECK_EQ(d1.lineCount(), d2.lineCount());
}
