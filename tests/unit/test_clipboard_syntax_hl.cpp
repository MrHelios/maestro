#include "test_framework.h"
#include "document/Buffer.h"
#include "document/Document.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"
#include "syntax/SyntaxToken.h"

#include <string>
#include <vector>

namespace {

// Compara spans elemento a elemento (SyntaxSpan no define operator==).
bool spansEqual(const std::vector<SyntaxSpan>& a, const std::vector<SyntaxSpan>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].begin != b[i].begin || a[i].end != b[i].end || a[i].token != b[i].token)
            return false;
    }
    return true;
}

// Oráculo: caché independiente que se re-analiza completa, para comparar.
SyntaxCache makeOracle(Document& doc, SyntaxLanguage lang) {
    SyntaxCache c;
    c.setLanguage(lang);
    c.ensureValid(doc, static_cast<int>(doc.lineCount()));
    return c;
}

} // namespace

// Inserción multilínea. La primera línea modificada debe quedar dirty
// inmediatamente después de insertBlock (sin esperar una edicion extra).
TEST(syntax_hl_insert_block_marks_first_line_dirty) {
    Buffer b;
    b.filename = "archivo.cpp";

    // Documento con varias líneas (C++).
    b.document.restore({
        "int a = 1;",
        "int b = 2;",
        "int c = 3;",
        "int d = 4;",
        "int e = 5;",
    });

    // Lenguaje C++ por el nombre de archivo.
    b.syntaxCache.setLanguage(SyntaxLanguage::Cpp);

    // Calentar el caché: analizar todo el documento.
    b.syntaxCache.ensureValid(b.document, b.document.lineCount());

    // Antes de insertar, la línea 0 está validada.
    CHECK(b.syntaxCache.isValidThrough(1));

    // Insertar un bloque de DOS líneas en la línea 0 (columna 0).
    // Esto simula pegar código copiado.
    std::vector<std::string> block = {
        "/* comentario abierto",
        "   línea pegada",
        "*/",
    };
    b.document.insertBlock(0, 0, block);

    // Después de insertar multilínea, la línea 0 (modificada) debe estar dirty.
    CHECK(!b.syntaxCache.isValidThrough(1));
}

// Después de ensureValid, la primera línea modificada se re-analiza
// con el nuevo contenido y las líneas insertadas reciben el resaltado correcto,
// incluyendo la propagación del estado sintáctico (comentario multilínea).
TEST(syntax_hl_insert_block_rehighlights_and_propagates_state) {
    Buffer b;
    b.filename = "archivo.cpp";

    b.document.restore({
        "int a = 1;",
        "int b = 2;",
        "int c = 3;",
    });

    b.syntaxCache.setLanguage(SyntaxLanguage::Cpp);
    b.syntaxCache.ensureValid(b.document, b.document.lineCount());

    // Bloque que abre un comentario en la primera línea insertada y lo cierra
    // en la última. Esto obliga a que el estado inBlockComment se propague.
    std::vector<std::string> block = {
        "/* comentario",
        "   contenido del comentario",
        "*/",
        "int d = 4;",
    };
    b.document.insertBlock(0, 0, block);

    // Re-analizar hasta convergencia (como haría el render).
    b.syntaxCache.ensureValid(b.document, b.document.lineCount());

    // Línea 0: ahora empieza con "/*", debe generar un span de Comment.
    const auto& spans0 = b.syntaxCache.spansFor(0);
    bool hasCommentOnFirstLine = false;
    for (const auto& s : spans0) {
        if (s.token == SyntaxToken::Comment) { hasCommentOnFirstLine = true; break; }
    }
    CHECK(hasCommentOnFirstLine);

    // Estado antes de la línea 1 (después de la línea 0): dentro de comentario.
    CHECK(b.syntaxCache.stateBefore(1).inBlockComment);

    // Estado antes de la línea 3 (después de cerrar en la línea 2): fuera de comentario.
    CHECK(!b.syntaxCache.stateBefore(3).inBlockComment);

    // Comparar con el oráculo para asegurar paridad total de spans y estados.
    SyntaxCache oracle = makeOracle(b.document, SyntaxLanguage::Cpp);
    int stateMismatch = 0, spanMismatch = 0;
    for (int l = 0; l < b.document.lineCount(); ++l) {
        if (b.syntaxCache.stateBefore(l) != oracle.stateBefore(l)) ++stateMismatch;
        if (!spansEqual(b.syntaxCache.spansFor(l), oracle.spansFor(l))) ++spanMismatch;
    }
    CHECK_EQ(stateMismatch, 0);
    CHECK_EQ(spanMismatch, 0);
    CHECK(b.syntaxCache.dirtyFrom() == INT_MAX);
}

// Eliminación multilínea (revisión de regresión equivalente).
// deleteRange que borra varias líneas y modifica el contenido de la primera.
TEST(syntax_hl_delete_range_marks_first_line_dirty) {
    Buffer b;
    b.filename = "archivo.cpp";

    b.document.restore({
        "int a = 1;",
        "int b = 2;",
        "int c = 3;",
        "int d = 4;",
    });

    b.syntaxCache.setLanguage(SyntaxLanguage::Cpp);
    b.syntaxCache.ensureValid(b.document, b.document.lineCount());

    // Antes de borrar, la línea 0 está validada.
    CHECK(b.syntaxCache.isValidThrough(1));

    // Borrar las líneas 1 y 2 (de "int b" a "int c"), dejando:
    //   "int a = 1;"
    //   "int d = 4;"
    b.document.deleteRange(1,0,3,0);

    // Después de eliminar multilínea, la línea 1 (primera afectada por el
    // cambio estructural) debe quedar dirty.
    CHECK(!b.syntaxCache.isValidThrough(2));

    // Re-analizar y verificar paridad con el oráculo.
    b.syntaxCache.ensureValid(b.document, b.document.lineCount());
    SyntaxCache oracle = makeOracle(b.document, SyntaxLanguage::Cpp);
    int stateMismatch = 0, spanMismatch = 0;
    for (int l = 0; l < b.document.lineCount(); ++l) {
        if (b.syntaxCache.stateBefore(l) != oracle.stateBefore(l)) ++stateMismatch;
        if (!spansEqual(b.syntaxCache.spansFor(l), oracle.spansFor(l))) ++spanMismatch;
    }
    CHECK_EQ(stateMismatch, 0);
    CHECK_EQ(spanMismatch, 0);
    CHECK(b.syntaxCache.dirtyFrom() == INT_MAX);
}
