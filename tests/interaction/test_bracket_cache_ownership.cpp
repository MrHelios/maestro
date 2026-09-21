#include "test_support.h"

// Regresión: el path de brackets forzaba Cpp sobre buf.syntaxCache mientras el
// renderer lo quería en el idioma real (None para .md/.txt). Cada frame hacía
// setLanguage(Cpp) -> invalidateAll + reparse 0..cursor, y el render lo
// devolvía a None con otro invalidateAll: lag progresivo al avanzar AvPag.
// Los brackets usan bracketCache_ propio; el caché del buffer queda en None.
TEST(bracket_md_paging_does_not_thrash_renderer_cache) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 200; ++i) {
        lines.push_back("# titulo " + std::to_string(i) + " {borrador}");
    }
    lines.push_back("{}");
    ed.active().document.restore(lines);
    ed.active().filename = "notes.md";
    ed.active().viewport.height = 24;
    ed.active().viewport.width = 80;

    CHECK(ed.active().syntaxCache.language() == SyntaxLanguage::None);

    // Simular AvPag: bajar viewport/cursor recalculando highlight por página.
    int prevParsed = -1;
    for (int top = 0; top + 24 <= (int)lines.size(); top += 24) {
        ed.active().viewport.top = top;
        ed.active().cursor.line = top + 10;
        ed.active().cursor.col = 0;
        ed.updateBracketHighlight();
        // El caché del renderer no debe ser tocado por el path de brackets.
        CHECK(ed.active().syntaxCache.language() == SyntaxLanguage::None);
        // Progreso monótono: sin resets a 0 (invalidateAll) por frame.
        int cur = ed.bracketCache_.parsedUpTo();
        CHECK(cur >= prevParsed);
        prevParsed = cur;
    }

    // El highlight sigue funcionando: '{}' real de la última línea matchea.
    const int last = (int)lines.size() - 1;
    ed.active().viewport.top = last - 23;
    ed.active().cursor.line = last;
    ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    if (ed.bracketPair_.has_value()) {
        CHECK_EQ(ed.bracketPair_->open.line, last);
        CHECK_EQ(ed.bracketPair_->close.line, last);
        CHECK_EQ(ed.bracketPair_->open.col, 0);
        CHECK_EQ(ed.bracketPair_->close.col, 1);
    }
}
