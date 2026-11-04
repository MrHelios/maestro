#include <functional>

#include "syntax/SyntaxCache.h"
#include "test_framework.h"
#include "document/Document.h"
#include "layout/BracketMatcher.h"
#include "syntax/SyntaxLanguage.h"

/*
*  simulamos un cache caliente para poder comprobar los find, sin necesidad
*  de traer toda la maquinaria de produccion.
*  esto hace que sea mas de verificar test y mantenerlos
*/
namespace {

BracketSpanSource makeWarmCacheSource(SyntaxCache& cache) {
    BracketSpanSource src;
    src.ensure = [](int) {};
    src.spans = [&cache](int line) -> const std::vector<SyntaxSpan>& {
        static const std::vector<SyntaxSpan> empty;
        if (line < 0 || line >= (int)cache.size()) return empty;
        return cache.spansFor(line);
    };
    return src;
}

}

TEST(bracket_matcher_single) {
    Document doc;
    
    for(auto lang : getSyntaxLanguages()) {
        doc.restore({"{}"});
        auto p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 1);
        
        doc.restore({"[]"});
        p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 1);
        
        doc.restore({"()"});
        p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 1);
    }
}

TEST(bracket_matcher_limits_corrects) {
    Document doc;
    
    for(auto lang : getSyntaxLanguages()) {
        doc.restore({" {} "});
        auto p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(!p.has_value());
        
        p = findMatchingBracket(doc, {0,3}, lang);
        CHECK(!p.has_value());
        
        doc.restore({" () "});
        p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(!p.has_value());
        
        p = findMatchingBracket(doc, {0,3}, lang);
        CHECK(!p.has_value());
        
        doc.restore({" [] "});
        p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(!p.has_value());
        
        p = findMatchingBracket(doc, {0,3}, lang);
        CHECK(!p.has_value());
    }
}

TEST(bracket_matcher_nested) {
    Document doc;
    
    for(auto lang : getSyntaxLanguages()) {
        doc.restore({"([{}])"});
        auto p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 5);
        
        p = findMatchingBracket(doc, {0,1}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 1); CHECK_EQ(p->close.col, 4);
        
        p = findMatchingBracket(doc, {0,2}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 2); CHECK_EQ(p->close.col, 3);
    }
}

TEST(bracket_matcher_cursor_inside) {
    Document doc; 
    
    for(auto lang : getSyntaxLanguages()) {
        doc.restore({"(a)"});
        auto p = findMatchingBracket(doc, {0,1}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 2);
        
        doc.restore({"[x]"});
        p = findMatchingBracket(doc, {0,1}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 2);
        
        doc.restore({"{x}"});
        p = findMatchingBracket(doc, {0,1}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 2);
    }
}

TEST(bracket_matcher_multiline) {
    Document doc; 
    
    doc.restore({"{" ,"  foo();" ,"}"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.line, 0); CHECK_EQ(p->close.line, 2);
    
    p = findMatchingBracket(doc, {1,5}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.line, 1); CHECK_EQ(p->close.line, 1);
    
    p = findMatchingBracket(doc, {2,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.line, 0); CHECK_EQ(p->close.line, 2);
}

TEST(bracket_matcher_multiline_nested) {
    Document doc; 
    
    for(auto lang : getSyntaxLanguages()) {
        doc.restore({"{" ,"  [" ,"    (" ,"    )" ,"  ]" ,"}"});
        auto p = findMatchingBracket(doc, {0,0}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.line, 0); CHECK_EQ(p->close.line, 5);
        
        p = findMatchingBracket(doc, {1,2}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.line, 1); CHECK_EQ(p->close.line, 4);
        
        p = findMatchingBracket(doc, {2,4}, lang);
        CHECK(p.has_value()); CHECK_EQ(p->open.line, 2); CHECK_EQ(p->close.line, 3);
    }
}

TEST(bracket_matcher_mismatch) {
    Document doc; 
    
    for(auto lang : getSyntaxLanguages()) {
        doc.restore({"{[}]"});
        CHECK(!findMatchingBracket(doc, {0,0}, lang).has_value());
        
        doc.restore({"([)]"});
        CHECK(!findMatchingBracket(doc, {0,0}, lang).has_value());
        
        doc.restore({"{[}]"});
        CHECK(!findMatchingBracket(doc, {0,1}, lang).has_value());
    }
}

/*
*  en un documento escribimos un string con corchetes y parentesis adentro
*  deben ser ignorados por el matcher
*/
TEST(bracket_matcher_ignore_string) {
    Document doc;
    // las \ estan para que podamos poner " dentro del string.
    doc.restore({"std::string s = \"{[()]}\";"});
    
    // Bracket dentro de string literal debe ser ignorado: 
    const std::string& line = doc.lineAt(0);
    size_t token = line.find('"');
    CHECK(token != std::string::npos);
    
    token = line.find('{');
    auto p = findMatchingBracket(doc, {0, (int)token}, SyntaxLanguage::Cpp);
    CHECK(!p.has_value());
    
    token = line.find('[');
    p = findMatchingBracket(doc, {0, (int)token}, SyntaxLanguage::Cpp);
    CHECK(!p.has_value());
    
    token = line.find('(');
    p = findMatchingBracket(doc, {0, (int)token}, SyntaxLanguage::Cpp);
    CHECK(!p.has_value());
}

/*
*  en un documento escribimos un char con corchetes y parentesis adentro
*  deben ser ignorados por el matcher
*/
TEST(bracket_matcher_ignore_char) {
    Document doc;
    doc.restore({"char c = '}';"});
    
    const std::string& line = doc.lineAt(0);
    size_t token = line.find('}');
    CHECK(token != std::string::npos);
    
    auto p = findMatchingBracket(doc, {0, (int)token}, SyntaxLanguage::Cpp);
    CHECK(!p.has_value());
}

TEST(bracket_matcher_ignore_line_comment) {
    Document doc; 
    doc.restore({"// ( [ ] )"});
    
    auto p = findMatchingBracket(doc, {0,3}, SyntaxLanguage::Cpp);
    CHECK(!p.has_value());
    
    p = findMatchingBracket(doc, {0,5}, SyntaxLanguage::Cpp);
    CHECK(!p.has_value());
}

TEST(bracket_matcher_ignore_block_comment) {
    Document doc; 
    doc.restore({"/* { */", "int x = (1);"});
    
    CHECK(!findMatchingBracket(doc, {0,3}, SyntaxLanguage::Cpp).has_value());
    CHECK(findMatchingBracket(doc, {1,8}, SyntaxLanguage::Cpp).has_value());
}

TEST(bracket_matcher_unicode_before) {
    Document doc; 
    
    for(auto lang : getSyntaxLanguages()) {
        doc.restore({"é[ ]"});
        
        // 'é' = 2 bytes (0xC3 0xA9), '[' en byte 2, ']' en byte 4: col es byte column
        const std::string& line = doc.lineAt(0);
        CHECK_EQ((int)line.find('['), 2);
        CHECK_EQ((int)line.find(']'), 4);
        
        // cursor sobre ']' (byte 4)
        auto p = findMatchingBracket(doc, {0, (int)line.find(']')}, lang); 
        CHECK(p.has_value()); CHECK_EQ(p->open.col, 2); CHECK_EQ(p->close.col, 4);
    }
}

TEST(bracket_matcher_tab_before) {
    Document doc;     
    doc.restore({"\t{", "\tfoo();", "\t}"});
    
    // '\t' = 1 byte, visual 4 columnas, 
    // pero col es byte column: '{' en byte 1, cursor en byte 1 dentro de "\tfoo();"
    auto p = findMatchingBracket(doc, {1,1}, SyntaxLanguage::Cpp); // byte column 1, dentro del rango {\n...\n}
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->open.col, 1); // byte column, no visual
    CHECK_EQ(p->close.line, 2);
    CHECK_EQ(p->close.col, 1); // byte column, no visual
}

/*
*  la idea de este test es que usando std:optional, verifiquemos que no quedan recursos 
*  de una vieja instancia que puedan ser mal usados en una nueva instancia
*/
TEST(bracket_matcher_cache_document_reuse) {
    std::optional<Document> slot;
    
    // construye un documento dentro de optional
    slot.emplace();
    slot->restore({
        "line0",
        "line1",
        "line2",
        "line3",
        "line4",
        "{}"
    });

    auto p = findMatchingBracket(*slot, {5, 0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value());
    
    // destruye liberando los recursos
    slot.reset();
    // construye otra vez el documento
    slot.emplace();
    slot->restore({
        "line0",
        "line1",
        "line2",
        "line3",
        "line4",
        "\"{}\""
    });

    p = findMatchingBracket(*slot, {5, 1}, SyntaxLanguage::Cpp);
    CHECK(!p.has_value());
}

// ---------------------------------------------------------------------------
// findMatchingBracketBounded
// ---------------------------------------------------------------------------

/*
*  la idea es comprobar que no se detecta el corchete final que queda afuera
*  del cache.
*/
TEST(bracket_matcher_bounded_open_inside_close_outside) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketBounded(doc, {0, 0}, SyntaxLanguage::Cpp, src, 0, 3);
    CHECK(!p.has_value());

    p = findMatchingBracketBounded(doc, {0, 0}, SyntaxLanguage::Cpp, src, 0, doc.lineCount());
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 4);
}

/*
*  la idea es comprobar que no se detecta el corchete inicial que queda afuera
*  del cache.
*/
TEST(bracket_matcher_bounded_close_inside_open_outside) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketBounded(doc, {4, 0}, SyntaxLanguage::Cpp, src, 2, 5);
    CHECK(!p.has_value());

    p = findMatchingBracketBounded(doc, {4, 0}, SyntaxLanguage::Cpp, src, 0, 5);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 4);
}

// ---------------------------------------------------------------------------
// findMatchingBracketFrom
// ---------------------------------------------------------------------------

TEST(bracket_matcher_from_open_forward) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {0, 0}, SyntaxLanguage::Cpp, src);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 4);
}

TEST(bracket_matcher_from_open_forward_no_close) {
    Document doc;
    doc.restore({"{", "x", "x", "x"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {0, 0}, SyntaxLanguage::Cpp, src);
    CHECK(!p.has_value());
}

TEST(bracket_matcher_from_close_backward) {
    Document doc;
    doc.restore({"{", "x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {4, 0}, SyntaxLanguage::Cpp, src);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 4);
}

TEST(bracket_matcher_from_close_backward_no_open) {
    Document doc;
    doc.restore({"x", "x", "x", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {4, 0}, SyntaxLanguage::Cpp, src);
    CHECK(!p.has_value());
}

TEST(bracket_matcher_from_enclosing_backward) {
    Document doc;
    doc.restore({"{", "  x", "  y", "}"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {2, 1}, SyntaxLanguage::Cpp, src);
    CHECK(p.has_value());
    CHECK_EQ(p->open.line, 0);
    CHECK_EQ(p->close.line, 3);
}

TEST(bracket_matcher_from_no_bracket) {
    Document doc;
    doc.restore({"abc", "def"});

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Cpp);
    cache.ensureValid(doc, doc.lineCount());

    auto src = makeWarmCacheSource(cache);

    auto p = findMatchingBracketFrom(doc, {0, 1}, SyntaxLanguage::Cpp, src);
    CHECK(!p.has_value());
}

// ---------------------------------------------------------------------------
// Equivalencia backward/forward en contornos malformados acordados
// ---------------------------------------------------------------------------

TEST(bracket_matcher_from_backward_forward_equivalence_contours) {
    auto checkCase = [](std::vector<std::string> lines, Position pos) {
        Document doc;
        doc.restore(lines);

        SyntaxCache cache;
        cache.setLanguage(SyntaxLanguage::Cpp);
        cache.ensureValid(doc, doc.lineCount());

        auto src = makeWarmCacheSource(cache);

        auto legacy = findMatchingBracket(doc, pos, SyntaxLanguage::Cpp);
        auto incremental = findMatchingBracketFrom(doc, pos, SyntaxLanguage::Cpp, src);

        CHECK(legacy.has_value() == incremental.has_value());
        if (legacy.has_value() && incremental.has_value()) {
            CHECK(*legacy == *incremental);
        }
    };

    // Envolvente bien formado con close posterior.
    checkCase({"} { x }"}, {0, 4});

    // Envolvente anidado bien formado.
    checkCase({"({ { } x })"}, {0, 7});

    // Mismatch directo en bracket abierto/cerrado.
    checkCase({"{[}]"}, {0, 0});
    checkCase({"{[}]"}, {0, 2});
    checkCase({"([)]"}, {0, 0});
}