#include "test_framework.h"
#include "syntax/SyntaxHighlighter.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"
#include "syntax/SyntaxCache.h"
#include "syntax/RustHighlighter.h"
#include "document/Document.h"

#include <string>
#include <vector>
#include <cstring>

static bool hasSpan(const std::vector<SyntaxSpan>& spans,
                    std::size_t b,
                    std::size_t e,
                    SyntaxToken tok) {
    for (const auto& s : spans) {
        if (s.begin == b && s.end == e && s.token == tok) {
            return true;
        }
    }
    return false;
}

static bool hasToken(const std::vector<SyntaxSpan>& spans, SyntaxToken tok) {
    for (const auto& s : spans) {
        if (s.token == tok) {
            return true;
        }
    }
    return false;
}

static bool sameSpans(const std::vector<SyntaxSpan>& a,
                      const std::vector<SyntaxSpan>& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].begin != b[i].begin ||
            a[i].end != b[i].end ||
            a[i].token != b[i].token) {
            return false;
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Selección de lenguaje
// -----------------------------------------------------------------------------

TEST(RustLanguage_FromFilename) {
    CHECK(languageFromFilename("main.rs") == SyntaxLanguage::Rust);
    CHECK(languageFromFilename("MAIN.RS") == SyntaxLanguage::Rust);
    CHECK(languageFromFilename("dir.with.dot/main.rs") == SyntaxLanguage::Rust);
    CHECK(languageFromFilename("main.rs.bak") == SyntaxLanguage::None);

    // No debe romper selecciones existentes.
    CHECK(languageFromFilename("main.c") == SyntaxLanguage::C);
    CHECK(languageFromFilename("main.cpp") == SyntaxLanguage::Cpp);
    CHECK(languageFromFilename("README.md") == SyntaxLanguage::Markdown);
}

// -----------------------------------------------------------------------------
// Estado Rust en SyntaxState
// -----------------------------------------------------------------------------

TEST(SyntaxState_RustFieldsParticipateInEquality) {
    SyntaxState a{};
    SyntaxState b{};

    CHECK(a == b);

    b.rustBlockCommentDepth = 1;
    CHECK(a != b);

    b.rustBlockCommentDepth = 0;
    b.rustInRawString = true;
    CHECK(a != b);

    b.rustInRawString = false;
    b.rustRawHashCount = 2;
    CHECK(a != b);
}

// -----------------------------------------------------------------------------
// Grupo A: tokens básicos
// -----------------------------------------------------------------------------

TEST(RustHighlighter_EmptyLine) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    CHECK(hl.highlight("").empty());
    CHECK(hl.highlight("   ").empty());
}

TEST(RustHighlighter_BasicKeywordsTypesNumbers) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = "let x: i32 = 42;";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Keyword));   // let
    CHECK(hasSpan(spans, 7, 10, SyntaxToken::Type));     // i32
    CHECK(hasSpan(spans, 13, 15, SyntaxToken::Number));  // 42
}

TEST(RustHighlighter_KeywordBoundaries) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    // Identificadores que contienen keywords no deben resaltarse como keywords.
    CHECK(hl.highlight("return_value").empty());
    CHECK(hl.highlight("function_name").empty());
    CHECK(hl.highlight("let_return").empty());

    std::string line = "let return_value";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Keyword));
    CHECK(spans.size() == 1);
}

TEST(RustHighlighter_StrictKeywords) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    CHECK(hasSpan(hl.highlight("fn"), 0, 2, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("let"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("mut"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("pub"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("use"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("mod"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("struct"), 0, 6, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("enum"), 0, 4, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("impl"), 0, 4, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("trait"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("match"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("if"), 0, 2, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("else"), 0, 4, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("loop"), 0, 4, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("while"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("for"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("in"), 0, 2, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("return"), 0, 6, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("break"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("continue"), 0, 8, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("async"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("await"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("dyn"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("unsafe"), 0, 6, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("const"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("static"), 0, 6, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("Self"), 0, 4, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("self"), 0, 4, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("super"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("crate"), 0, 5, SyntaxToken::Keyword));
}

TEST(RustHighlighter_ReservedKeywords) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    CHECK(hasSpan(hl.highlight("abstract"), 0, 8, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("become"), 0, 6, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("box"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("do"), 0, 2, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("final"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("gen"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("macro"), 0, 5, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("override"), 0, 8, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("priv"), 0, 4, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("try"), 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("typeof"), 0, 6, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("unsized"), 0, 7, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("virtual"), 0, 7, SyntaxToken::Keyword));
    CHECK(hasSpan(hl.highlight("yield"), 0, 5, SyntaxToken::Keyword));
}

TEST(RustHighlighter_WeakKeywordsNotHighlightedInV1) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    // Weak keywords son contextuales. En esta primera versión no se resaltan
    // incondicionalmente para evitar falsos positivos.
    CHECK(hl.highlight("union").empty());
    CHECK(hl.highlight("safe").empty());
    CHECK(hl.highlight("raw").empty());
    CHECK(hl.highlight("macro_rules").empty());

    std::string line = "union safe raw macro_rules";
    CHECK(hl.highlight(line).empty());
}

TEST(RustHighlighter_BooleansAreKeywords) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = "true false";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 4, SyntaxToken::Keyword));  // true
    CHECK(hasSpan(spans, 5, 10, SyntaxToken::Keyword)); // false
}

TEST(RustHighlighter_PrimitiveTypes) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    CHECK(hasSpan(hl.highlight("bool"), 0, 4, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("char"), 0, 4, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("str"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("i8"), 0, 2, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("i16"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("i32"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("i64"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("i128"), 0, 4, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("isize"), 0, 5, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("u8"), 0, 2, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("u16"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("u32"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("u64"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("u128"), 0, 4, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("usize"), 0, 5, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("f32"), 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(hl.highlight("f64"), 0, 3, SyntaxToken::Type));
}

TEST(RustHighlighter_PrimitiveTypesAreNotKeywords) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    auto spans = hl.highlight("i32");
    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Type));
    CHECK(!hasToken(spans, SyntaxToken::Keyword));
}

TEST(RustHighlighter_RawIdentifierNotHighlighted) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    // r#fn es un raw identifier, no la keyword fn.
    CHECK(hl.highlight(R"(r#fn)").empty());
    CHECK(hl.highlight(R"(r#foo)").empty());

    // br sin comilla no es raw byte string.
    CHECK(hl.highlight(R"(br)").empty());
}

// -----------------------------------------------------------------------------
// Grupo B: comentarios
// -----------------------------------------------------------------------------

TEST(RustHighlighter_LineComment) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    CHECK(hasSpan(hl.highlight("// hello"), 0, 8, SyntaxToken::Comment));
    CHECK(hasSpan(hl.highlight("/// doc"), 0, 7, SyntaxToken::Comment));
    CHECK(hasSpan(hl.highlight("//! inner"), 0, 9, SyntaxToken::Comment));

    std::string line = "let x; // y";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 7, line.size(), SyntaxToken::Comment));
}

TEST(RustHighlighter_BlockCommentSingleLine) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = "/* c */ let x;";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 7, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 8, 11, SyntaxToken::Keyword));
}

TEST(RustHighlighter_DocBlockCommentSingleLine) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = "/** doc */";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, line.size(), SyntaxToken::Comment));
    CHECK(spans.size() == 1);
}

TEST(RustHighlighter_NestedBlockCommentSameLine) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = "/* a /* b */ c */ let x;";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 17, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 18, 21, SyntaxToken::Keyword));
}

TEST(RustHighlighter_NestedBlockCommentMultiLineState) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    SyntaxState st{};
    SyntaxState out;

    std::string l1 = "/* a /* b";
    auto s1 = hl.highlight(l1, st, out);
    st = out;

    CHECK(hasSpan(s1, 0, l1.size(), SyntaxToken::Comment));
    CHECK(st.rustBlockCommentDepth == 2);

    std::string l2 = " c */";
    auto s2 = hl.highlight(l2, st, out);
    st = out;

    CHECK(hasSpan(s2, 0, l2.size(), SyntaxToken::Comment));
    CHECK(st.rustBlockCommentDepth == 1);

    std::string l3 = " d */ let x;";
    auto s3 = hl.highlight(l3, st, out);
    st = out;

    CHECK(hasSpan(s3, 0, 5, SyntaxToken::Comment));
    CHECK(hasSpan(s3, 6, 9, SyntaxToken::Keyword));
    CHECK(st.rustBlockCommentDepth == 0);
}

TEST(RustHighlighter_BlockCommentIgnoresQuotesAndSlashes) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(/* " // */)";
    auto spans = hl.highlight(line);

    CHECK(spans.size() == 1);
    CHECK(hasSpan(spans, 0, line.size(), SyntaxToken::Comment));
    CHECK(!hasToken(spans, SyntaxToken::String));
    CHECK(!hasToken(spans, SyntaxToken::Character));
}

TEST(RustHighlighter_CommentHidesStringMarkers) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(/* " */ let x;)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 7, SyntaxToken::Comment));
    CHECK(hasSpan(spans, 8, 11, SyntaxToken::Keyword));
    CHECK(!hasToken(spans, SyntaxToken::String));
}

// -----------------------------------------------------------------------------
// Grupo C: strings, caracteres y lifetimes
// -----------------------------------------------------------------------------

TEST(RustHighlighter_NormalString) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(let s = "hi";)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 8, 12, SyntaxToken::String));
}

TEST(RustHighlighter_StringWithEscapes) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(let s = "a\"b";)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 8, 14, SyntaxToken::String));
}

TEST(RustHighlighter_UnclosedStringDoesNotPropagateState) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(let s = "unclosed)";
    SyntaxState st{};
    SyntaxState out;

    auto spans = hl.highlight(line, st, out);

    CHECK(hasSpan(spans, 8, line.size(), SyntaxToken::String));
    CHECK(!out.rustInRawString);
    CHECK(out.rustRawHashCount == 0);
    CHECK(out.rustBlockCommentDepth == 0);
}

TEST(RustHighlighter_ByteString) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(b"hi";)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 5, SyntaxToken::String));
}

TEST(RustHighlighter_CString) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(c"ho";)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 5, SyntaxToken::String));
}

TEST(RustHighlighter_StringHidesCommentMarkers) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(let s = "/* x */";)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 8, 17, SyntaxToken::String));
    CHECK(!hasToken(spans, SyntaxToken::Comment));
}

TEST(RustHighlighter_CharLiterals) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    CHECK(hasSpan(hl.highlight(R"('a')"), 0, 3, SyntaxToken::Character));
    CHECK(hasSpan(hl.highlight(R"('\n')"), 0, 4, SyntaxToken::Character));
    CHECK(hasSpan(hl.highlight(R"('\'')"), 0, 4, SyntaxToken::Character));
    CHECK(hasSpan(hl.highlight(R"('\x41')"), 0, 6, SyntaxToken::Character));
    CHECK(hasSpan(hl.highlight(R"('\u{1F600}')"), 0, 11, SyntaxToken::Character));
    CHECK(hasSpan(hl.highlight(R"(b'a')"), 0, 4, SyntaxToken::Character));
}

TEST(RustHighlighter_LifetimesAreNotChars) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    // 'a' es char literal; 'a suelto es lifetime y en v1 no genera span.
    std::string line = R"('a' 'a)";
    auto spans = hl.highlight(line);

    CHECK(spans.size() == 1);
    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Character));
    CHECK(!hasSpan(spans, 4, 6, SyntaxToken::Character));

    CHECK(hl.highlight(R"('a)").empty());
    CHECK(hl.highlight(R"('_)").empty());
    CHECK(hl.highlight(R"('static)").empty());
}

TEST(RustHighlighter_StrayApostrophesDoNotSwallowLine) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"('' let x = 1;)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 3, 6, SyntaxToken::Keyword));  // let
    CHECK(hasSpan(spans, 11, 12, SyntaxToken::Number)); // 1
    CHECK(!hasToken(spans, SyntaxToken::Character));
}

// -----------------------------------------------------------------------------
// Grupo D: raw strings
// -----------------------------------------------------------------------------

TEST(RustHighlighter_RawStringNoHash) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(r"hi";)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 5, SyntaxToken::String));
}

TEST(RustHighlighter_RawStringOneHash) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(r#"hi"#;)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 7, SyntaxToken::String));
}

TEST(RustHighlighter_RawStringMultipleHash) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line = R"(r##"hi"##;)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 9, SyntaxToken::String));
}

TEST(RustHighlighter_RawStringExtraHashTerminator) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    // r#"hi"##; cierra con " seguido de UN #. El # extra queda fuera.
    std::string line = R"(r#"hi"##;)";
    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 7, SyntaxToken::String));
    CHECK(!hasSpan(spans, 0, line.size(), SyntaxToken::String));
}

TEST(RustHighlighter_RawStringUnterminatedPropagatesState) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    SyntaxState st{};
    SyntaxState out;

    std::string l1 = R"(r#"hi";)";
    auto s1 = hl.highlight(l1, st, out);
    st = out;

    CHECK(hasSpan(s1, 0, l1.size(), SyntaxToken::String));
    CHECK(st.rustInRawString);
    CHECK(st.rustRawHashCount == 1);

    std::string l2 = R"(end"# let x;)";
    auto s2 = hl.highlight(l2, st, out);
    st = out;

    CHECK(hasSpan(s2, 0, 5, SyntaxToken::String));
    CHECK(hasSpan(s2, 6, 9, SyntaxToken::Keyword));
    CHECK(!st.rustInRawString);
    CHECK(st.rustRawHashCount == 0);
}

TEST(RustHighlighter_RawStringMultilineMultipleHash) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    SyntaxState st{};
    SyntaxState out;

    std::string l1 = R"(r##"start)";
    auto s1 = hl.highlight(l1, st, out);
    st = out;

    CHECK(hasSpan(s1, 0, l1.size(), SyntaxToken::String));
    CHECK(st.rustInRawString);
    CHECK(st.rustRawHashCount == 2);

    std::string l2 = R"(end"## let x;)";
    auto s2 = hl.highlight(l2, st, out);
    st = out;

    CHECK(hasSpan(s2, 0, 6, SyntaxToken::String));
    CHECK(hasSpan(s2, 7, 10, SyntaxToken::Keyword));
    CHECK(!st.rustInRawString);
    CHECK(st.rustRawHashCount == 0);
}

TEST(RustHighlighter_RawByteAndCRawStrings) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    CHECK(hasSpan(hl.highlight(R"(br"hi";)"), 0, 6, SyntaxToken::String));
    CHECK(hasSpan(hl.highlight(R"(br#"hi"#;)"), 0, 8, SyntaxToken::String));
    CHECK(hasSpan(hl.highlight(R"(cr"hi";)"), 0, 6, SyntaxToken::String));
    CHECK(hasSpan(hl.highlight(R"(cr#"hi"#;)"), 0, 8, SyntaxToken::String));
}

TEST(RustHighlighter_RawStringHidesCommentMarkers) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    // r"/*" -> longitud 5. Span debe ser {0, 5}
    std::string line1 = R"(r"/*")";
    auto spans1 = hl.highlight(line1);
    CHECK(hasSpan(spans1, 0, 5, SyntaxToken::String)); // CORREGIDO: 5 en vez de 4
    CHECK(!hasToken(spans1, SyntaxToken::Comment));

    // r"//" -> longitud 5. Span debe ser {0, 5}
    std::string line2 = R"(r"//")";
    auto spans2 = hl.highlight(line2);
    CHECK(hasSpan(spans2, 0, 5, SyntaxToken::String)); // CORREGIDO: 5 en vez de 4
    CHECK(!hasToken(spans2, SyntaxToken::Comment));
}

// -----------------------------------------------------------------------------
// Grupo E: números
// -----------------------------------------------------------------------------

TEST(RustHighlighter_Numbers) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::string line =
        "42 0xFF 0o755 0b1010 3.14 1e10 2.5e-3 42u32 3.0f64 1_000";

    auto spans = hl.highlight(line);

    CHECK(hasSpan(spans, 0, 2, SyntaxToken::Number));    // 42
    CHECK(hasSpan(spans, 3, 7, SyntaxToken::Number));    // 0xFF
    CHECK(hasSpan(spans, 8, 13, SyntaxToken::Number));   // 0o755
    CHECK(hasSpan(spans, 14, 20, SyntaxToken::Number));  // 0b1010
    CHECK(hasSpan(spans, 21, 25, SyntaxToken::Number));  // 3.14
    CHECK(hasSpan(spans, 26, 30, SyntaxToken::Number));  // 1e10
    CHECK(hasSpan(spans, 31, 37, SyntaxToken::Number));  // 2.5e-3
    CHECK(hasSpan(spans, 38, 43, SyntaxToken::Number));  // 42u32
    CHECK(hasSpan(spans, 44, 50, SyntaxToken::Number));  // 3.0f64
    CHECK(hasSpan(spans, 51, 56, SyntaxToken::Number));  // 1_000
}

TEST(RustHighlighter_NumberRangeAndMethod) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    auto s1 = hl.highlight("1..2");
    CHECK(hasSpan(s1, 0, 1, SyntaxToken::Number));
    CHECK(hasSpan(s1, 3, 4, SyntaxToken::Number));
    CHECK(!hasSpan(s1, 0, 4, SyntaxToken::Number));

    auto s2 = hl.highlight("1.max(2)");
    CHECK(hasSpan(s2, 0, 1, SyntaxToken::Number));
    CHECK(hasSpan(s2, 6, 7, SyntaxToken::Number));
    CHECK(!hasSpan(s2, 0, 8, SyntaxToken::Number));
}

TEST(RustHighlighter_InvalidNumericPrefixes) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    auto s1 = hl.highlight("0xG");
    CHECK(hasSpan(s1, 0, 1, SyntaxToken::Number));
    CHECK(!hasSpan(s1, 0, 3, SyntaxToken::Number));
    CHECK(s1.size() == 1);

    auto s2 = hl.highlight("0b2");
    CHECK(hasSpan(s2, 0, 1, SyntaxToken::Number));
    CHECK(!hasSpan(s2, 0, 3, SyntaxToken::Number));

    auto s3 = hl.highlight("0o8");
    CHECK(hasSpan(s3, 0, 1, SyntaxToken::Number));
    CHECK(!hasSpan(s3, 0, 3, SyntaxToken::Number));

    auto s4 = hl.highlight("0x");
    CHECK(hasSpan(s4, 0, 1, SyntaxToken::Number));
}

TEST(RustHighlighter_NumericSuffixBoundary) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    auto s1 = hl.highlight("123abc");
    CHECK(hasSpan(s1, 0, 3, SyntaxToken::Number));
    CHECK(!hasSpan(s1, 0, 6, SyntaxToken::Number));
    CHECK(s1.size() == 1);

    auto s2 = hl.highlight("123u8x");
    CHECK(hasSpan(s2, 0, 3, SyntaxToken::Number));
    CHECK(!hasSpan(s2, 0, 6, SyntaxToken::Number));
    CHECK(s2.size() == 1);

    CHECK(hasSpan(hl.highlight("0xFFu8"), 0, 6, SyntaxToken::Number));
    CHECK(hasSpan(hl.highlight("3.0f64"), 0, 6, SyntaxToken::Number));
    CHECK(hasSpan(hl.highlight("1f64"), 0, 4, SyntaxToken::Number));
}

// -----------------------------------------------------------------------------
// Grupo F: invariantes básicas de spans
// -----------------------------------------------------------------------------

TEST(RustHighlighter_SpanInvariants) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Rust);

    std::vector<std::string> lines = {
        "",
        "   ",
        "let x: i32 = 42;",
        "// comment",
        "/* nested /* comment */ */ let y = 1;",
        R"(let s = "str";)",
        R"(let r = r#"raw"#;)",
        R"(let c = 'x';)",
        R"(let l = 'a;)",
        "1..2",
        "0xFFu8 3.14f64 1_000"
    };

    for (const auto& line : lines) {
        auto spans = hl.highlight(line);

        for (const auto& s : spans) {
            CHECK(s.begin <= s.end);
            CHECK(s.end <= line.size());
        }
    }
}

// -----------------------------------------------------------------------------
// Integración: Rust no reutiliza estado CLike
// -----------------------------------------------------------------------------

TEST(RustHighlighter_IgnoresCLikeBlockCommentState) {
    RustHighlighter rust;

    SyntaxState st{};
    st.inBlockComment = true;
    st.inRawString = true;

    SyntaxState out;
    std::vector<SyntaxSpan> spans;

    rust.highlight("let x = 1;", st, out, spans);

    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Keyword));
    CHECK(hasSpan(spans, 8, 9, SyntaxToken::Number));

    // El estado Rust propio no debe activarse por campos de CLike.
    CHECK(out.rustBlockCommentDepth == 0);
    CHECK(!out.rustInRawString);
    CHECK(out.rustRawHashCount == 0);
}

// -----------------------------------------------------------------------------
// Integración con SyntaxCache
// -----------------------------------------------------------------------------

TEST(RustHighlighter_SyntaxCache_NestedComment) {
    Document doc;
    doc.restore({
        "/* a",
        " b */ let x = 1;",
        "normal"
    });

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Rust);
    cache.ensureValid(doc, doc.lineCount());

    CHECK(hasSpan(cache.spansFor(0), 0, 4, SyntaxToken::Comment));
    CHECK(cache.stateBefore(1).rustBlockCommentDepth == 1);

    CHECK(hasSpan(cache.spansFor(1), 0, 5, SyntaxToken::Comment));
    CHECK(hasSpan(cache.spansFor(1), 6, 9, SyntaxToken::Keyword));
    CHECK(hasSpan(cache.spansFor(1), 14, 15, SyntaxToken::Number));
    CHECK(cache.stateBefore(2).rustBlockCommentDepth == 0);

    CHECK(!hasSpan(cache.spansFor(2), 0, 6, SyntaxToken::Comment));
}

TEST(RustHighlighter_SyntaxCache_RawString) {
    Document doc;
    doc.restore({
        R"(normal)",
        R"(r#"start)",
        R"(end"# let x = 1;)",
        R"(normal)"
    });

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Rust);
    cache.ensureValid(doc, doc.lineCount());

    CHECK(!cache.stateBefore(1).rustInRawString);

    CHECK(hasSpan(cache.spansFor(1), 0, 8, SyntaxToken::String));
    CHECK(cache.stateBefore(2).rustInRawString);
    CHECK(cache.stateBefore(2).rustRawHashCount == 1);

    CHECK(hasSpan(cache.spansFor(2), 0, 5, SyntaxToken::String));
    CHECK(hasSpan(cache.spansFor(2), 6, 9, SyntaxToken::Keyword));
    CHECK(hasSpan(cache.spansFor(2), 14, 15, SyntaxToken::Number));
    CHECK(!cache.stateBefore(3).rustInRawString);
    CHECK(cache.stateBefore(3).rustRawHashCount == 0);

    CHECK(!hasSpan(cache.spansFor(3), 0, 6, SyntaxToken::String));
}

TEST(RustHighlighter_SyntaxCache_ConvergenceAfterEdit) {
    Document doc;
    doc.restore({
        R"(normal)",
        R"(r#"start)",
        R"(end"# let x = 1;)",
        R"(normal)"
    });

    SyntaxCache cache;
    cache.setLanguage(SyntaxLanguage::Rust);
    cache.ensureValid(doc, doc.lineCount());

    CHECK(cache.stateBefore(2).rustInRawString);

    // Editamos el documento: desaparece el raw string multilínea.
    doc.restore({
        R"(normal)",
        R"(normal)",
        R"(normal)",
        R"(normal)"
    });

    cache.markDirty(0);
    cache.ensureValid(doc, doc.lineCount());

    SyntaxCache fresh;
    fresh.setLanguage(SyntaxLanguage::Rust);
    fresh.ensureValid(doc, doc.lineCount());

    for (int l = 0; l <= doc.lineCount(); ++l) {
        CHECK(cache.stateBefore(l) == fresh.stateBefore(l));
    }

    for (int l = 0; l < doc.lineCount(); ++l) {
        CHECK(sameSpans(cache.spansFor(l), fresh.spansFor(l)));
        CHECK(!hasSpan(cache.spansFor(l), 0, 6, SyntaxToken::String));
    }
}

// -----------------------------------------------------------------------------
// Regresión mínima: CLike y Markdown no cambian
// -----------------------------------------------------------------------------

TEST(RustHighlighter_CLikeRegression_Simple) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Cpp);

    auto spans = hl.highlight("int x = 0;");

    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Type));
    CHECK(hasSpan(spans, 8, 9, SyntaxToken::Number));
}

TEST(RustHighlighter_MarkdownRegression_Simple) {
    SyntaxHighlighter hl;
    hl.setLanguage(SyntaxLanguage::Markdown);

    auto spans = hl.highlight("# H");

    CHECK(spans.size() == 1);
    CHECK(hasSpan(spans, 0, 3, SyntaxToken::Heading));
}
