#include "test_support.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxToken.h"
#include "syntax/SyntaxCache.h"

// Regresión: al escribir /* ... */ en filas separadas (sin cambiar
// lineCount), el render incremental debe repintar al instante todas las
// filas afectadas, no solo la del cursor. Antes del fix, las filas
// siguientes quedaban con el color viejo hasta un Enter o re-entrar al
// archivo (full rebuild). Era un problema de actualizacion incremental, no
// del highlighter: con reparse completo el color siempre fue correcto.
//
// Este test reproduce el flujo real de edicion + render incremental y
// verifica el comportamiento correcto:
//  1. crea un .cpp con codigo sin comentarios,
//  2. lo edita in-place con /* en una fila y */ filas mas abajo,
//  3. verifica (via el cache incremental usado por el Renderer) que todo
//     lo de adentro quedo como Comment,
//  4. remueve /* y */ y verifica que volvio al color original.

namespace {

bool lineIsFullComment(const std::vector<SyntaxSpan>& spans, size_t len) {
    if (spans.size() != 1) return false;
    return spans[0].token == SyntaxToken::Comment &&
           spans[0].begin == 0 && spans[0].end == len;
}

bool lineHasComment(const std::vector<SyntaxSpan>& spans) {
    for (auto& s : spans)
        if (s.token == SyntaxToken::Comment) return true;
    return false;
}

bool lineHasType(const std::vector<SyntaxSpan>& spans) {
    for (auto& s : spans)
        if (s.token == SyntaxToken::Type) return true;
    return false;
}

} // namespace

TEST(syntax_block_comment_multiline_incremental_update) {
    Editor ed;
    Buffer& b = ed.active();
    b.document.restore({
        "int a = 1;",
        "int b = 2;",
        "int c = 3;",
        "int d = 4;",
        "int e = 5;",
        "int f = 6;",
    });
    b.filename = "test_bug.cpp";
    b.viewport.top = 0;
    b.viewport.left = 0;
    b.viewport.height = 10;
    b.viewport.width = 80;
    // Cursor en la fila editada para forzar el fast-path incremental
    // (sameLineEdit: misma linea, distinta columna, mismo lineCount).
    b.cursor.line = 1;
    b.cursor.col = 0;

    b.syntaxCache.setLanguage(SyntaxLanguage::Cpp);
    ed.renderer_.setExternalSyntaxCache(&b.syntaxCache);

    Message msg;
    // Prime: full rebuild, deja rowCache_ caliente y lastVersion_/lastCursor.
    ed.renderer_.buildDiffFrame(b.document, b.cursor, b.viewport,
                                b.filename, false, msg, State::Navegacion,
                                std::nullopt, std::nullopt, std::nullopt);

    // --- Fase 1: abrir /* en fila 1 y cerrar */ en fila 4 (sin tocar lineCount)
    b.document.insertText(1, 0, "/* ");
    b.document.insertText(4, 0, "*/ ");
    // Misma linea que en el prime, columna distinta -> sameLineEdit.
    b.cursor.line = 1;
    b.cursor.col = 3;

    // Render incremental como hace el editor al teclear (diff de una fila).
    ed.renderer_.buildDiffFrame(b.document, b.cursor, b.viewport,
                                b.filename, false, msg, State::Navegacion,
                                std::nullopt, std::nullopt, std::nullopt);

    // Oraculo: reparse completo (lo que se ve al entrar/salir del archivo).
    SyntaxCache fresh;
    fresh.setLanguage(SyntaxLanguage::Cpp);
    fresh.ensureValid(b.document, b.document.lineCount());

    // Sanity del oraculo: lineas 2 y 3 estan dentro del bloque.
    CHECK(fresh.stateBefore(2).inBlockComment);
    CHECK(fresh.stateBefore(3).inBlockComment);
    CHECK(lineIsFullComment(fresh.spansFor(2), b.document.lineAt(2).size()));
    CHECK(lineIsFullComment(fresh.spansFor(3), b.document.lineAt(3).size()));

    // Lo que realmente muestra el editor (cache incremental): debe coincidir
    // con el oraculo en todas las filas del bloque.
    CHECK(lineIsFullComment(b.syntaxCache.spansFor(2), b.document.lineAt(2).size()));
    CHECK(lineIsFullComment(b.syntaxCache.spansFor(3), b.document.lineAt(3).size()));
    CHECK(b.syntaxCache.stateBefore(2).inBlockComment);
    CHECK(b.syntaxCache.stateBefore(5).inBlockComment == false);

    // Simula "salir y volver a entrar al archivo": reparse completo que si
    // pinta bien. Deja el cache incremental sano para probar la fase 2.
    b.syntaxCache.ensureValid(b.document, b.document.lineCount());
    CHECK(lineIsFullComment(b.syntaxCache.spansFor(2), b.document.lineAt(2).size()));
    CHECK(lineIsFullComment(b.syntaxCache.spansFor(3), b.document.lineAt(3).size()));

    // --- Fase 2: remover /* y */ -> debe volver al color original
    // deleteRange(sl, sc, el, ec) con extremo exclusivo.
    b.document.deleteRange(1, 0, 1, 3); // quita "/* "
    b.document.deleteRange(4, 0, 4, 3); // quita "*/ "
    b.cursor.line = 1;
    b.cursor.col = 0;

    ed.renderer_.buildDiffFrame(b.document, b.cursor, b.viewport,
                                b.filename, false, msg, State::Navegacion,
                                std::nullopt, std::nullopt, std::nullopt);

    SyntaxCache fresh2;
    fresh2.setLanguage(SyntaxLanguage::Cpp);
    fresh2.ensureValid(b.document, b.document.lineCount());

    // Oraculo tras remover: ya no hay comentarios, vuelve Type (int).
    CHECK(!lineHasComment(fresh2.spansFor(2)));
    CHECK(lineHasType(fresh2.spansFor(2)));
    CHECK(!lineHasComment(fresh2.spansFor(3)));
    CHECK(!fresh2.stateBefore(2).inBlockComment);

    // Incremental debe coincidir con el oraculo: sin rastros de Comment.
    CHECK(!lineHasComment(b.syntaxCache.spansFor(2)));
    CHECK(lineHasType(b.syntaxCache.spansFor(2)));
    CHECK(!lineHasComment(b.syntaxCache.spansFor(3)));
}
