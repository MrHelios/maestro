#include "test_support.h"

// Regression: makeViewportSpanSource resaltaba cada linea con SyntaxState{}
// default, perdiendo inBlockComment / inRawString entre lineas. Un bracket
// dentro de un comentario multilinea o raw string multilinea no debe
// producir highlight/match.

TEST(bracket_viewport_ignores_multiline_block_comment) {
    Editor ed;
    ed.active().document.restore({
        "/*",
        "  {",
        "  }",
        "*/",
        "{}"
    });
    ed.active().filename = "test.cpp";
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 40;
    ed.active().viewport.top = 0;
    ed.active().viewport.left = 0;

    // '{' dentro de /* ... */ : debe ignorarse.
    ed.active().cursor.line = 1;
    ed.active().cursor.col = 2;
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());

    // '}' dentro de /* ... */ : debe ignorarse.
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 2;
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());

    // Control: '{}' real fuera del comentario si matchea.
    ed.active().cursor.line = 4;
    ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    if (ed.bracketPair_.has_value()) {
        CHECK_EQ(ed.bracketPair_->open.line, 4);
        CHECK_EQ(ed.bracketPair_->close.line, 4);
    }
}

TEST(bracket_viewport_ignores_multiline_raw_string) {
    Editor ed;
    ed.active().document.restore({
        "auto s = R\"foo(",
        "  {",
        "  }",
        ")foo\";",
        "{}"
    });
    ed.active().filename = "test.cpp";
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 40;
    ed.active().viewport.top = 0;
    ed.active().viewport.left = 0;

    // '{' dentro del raw string: debe ignorarse.
    ed.active().cursor.line = 1;
    ed.active().cursor.col = 2;
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());

    // '}' dentro del raw string: debe ignorarse.
    ed.active().cursor.line = 2;
    ed.active().cursor.col = 2;
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());

    // Control: '{}' real fuera del raw string si matchea.
    ed.active().cursor.line = 4;
    ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    if (ed.bracketPair_.has_value()) {
        CHECK_EQ(ed.bracketPair_->open.line, 4);
        CHECK_EQ(ed.bracketPair_->close.line, 4);
    }
}

// Nota: no hay test para string normal "..." multilinea porque por diseño
// (CLikeHighlighter) los strings normales son intra-linea y no propagan
// estado entre lineas. Solo /* */ y raw strings cruzan lineas via SyntaxState.
