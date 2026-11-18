#include <string>
#include <vector>

#include "test_framework.h"
#include "document/Document.h"
#include "diagnostics/Instrument.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxToken.h"

namespace {

std::vector<std::string> makeCodeLines(int n) {
    std::vector<std::string> lines;
    lines.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        lines.push_back("int var" + std::to_string(i) + " = " + std::to_string(i) + ";");
    }
    return lines;
}

// SyntaxSpan no define operator==: comparación elemento a elemento.
bool spansEqual(const std::vector<SyntaxSpan>& a, const std::vector<SyntaxSpan>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].begin != b[i].begin || a[i].end != b[i].end || a[i].token != b[i].token) return false;
    }
    return true;
}

} // namespace


TEST(syntax_cache_two_dirty_regions_partial_ensure) {
    Document doc;
    doc.restore(makeCodeLines(200));

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    c.ensureValid(doc, 200);

    // Dos regiones dirty separadas.
    doc.insertText(5, 0, "/* ");
    c.markDirty(5);

    doc.insertText(100, 0, "*/");
    c.markDirty(100);

    c.ensureValid(doc, 200);

    SyntaxCache fresh;
    fresh.setLanguage(SyntaxLanguage::Cpp);
    fresh.ensureValid(doc, 200);

    for (int l = 0; l < 200; ++l) {
        CHECK(c.stateBefore(l) == fresh.stateBefore(l));
        CHECK(spansEqual(c.spansFor(l), fresh.spansFor(l)));
    }
}

TEST(syntax_cache_dirty_regions_partial_ensure) {
    Document doc;
    doc.restore(makeCodeLines(200));

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    // cache caliente
    c.ensureValid(doc, 200);

    // neutra: no cambia el estado propagado
    doc.insertText(5, 0, "x");      
    c.markDirty(5);
    
    // abre comentario: cambia before_[101+]
    doc.insertText(100, 0, "/* ");  
    c.markDirty(100);

    // no llega a la segunda edición
    c.ensureValid(doc, 30);
    CHECK(!c.stateBefore(101).inBlockComment);
    
    c.ensureValid(doc, 200);
    CHECK(c.stateBefore(101).inBlockComment);
}


TEST(syntax_cache_partial_without_convergence_stays_dirty) {
    Document doc;
    doc.restore({
        "int a = 1;",
        "int b = 2;",
        "int c = 3;",
        "int d = 4;",
        "int e = 5;",
        "int f = 6;",
    });

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    c.ensureValid(doc, 6);
    
    // abre bloque sin cerrar
    doc.insertText(1, 0, "/* ");
    c.markDirty(1);
    
    // parcial: solo la fila editada
    c.ensureValid(doc, 2);
    
    // como ha sido parcial el final del archivo
    // no deberia haber detectado
    CHECK(c.dirtyFrom() != INT_MAX);
    CHECK(!c.isValidThrough(6));
    
    // el rango pedido sí quedó válido.
    CHECK(c.isValidThrough(2));
    CHECK(c.stateBefore(2).inBlockComment);

    // hacemos una validacion total
    c.ensureValid(doc, 6);
    
    CHECK(c.dirtyFrom() == INT_MAX);
    CHECK(c.isValidThrough(6));
}

// creamos un documento de 2k lineas, agregamos al inicio un comentario
// y se va a ir actualizando hasta el final del archivo.
TEST(syntax_cache_block_comment_2k_viewport_budget_and_tail) {
    const int n = 2000;
    const int viewport = 40;
    
    Document doc;
    doc.restore(makeCodeLines(n));

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    c.ensureValid(doc, n);

    doc.insertText(0, 0, "/* "); // comentario hasta EOF
    c.markDirty(0);

    instrument::enable(true);
    instrument::resetFrame();
    c.ensureValid(doc, viewport); // lo que necesita una tecla interactiva
    
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);

    // Budget:
    CHECK(m.syntaxHighlighterCalls <= 64);
    CHECK(m.syntaxHighlighterCalls >= 1);
    
    // Viewport correcto
    CHECK(c.stateBefore(1).inBlockComment);
    CHECK(c.stateBefore(viewport).inBlockComment);
    
    bool tailSpanComment = false;
    for (auto& s : c.spansFor(viewport - 1))
        if (s.token == SyntaxToken::Comment) tailSpanComment = true;
        
    CHECK(tailSpanComment);
    
    // todavia no se detecto que el inicio del comentario va hasta el final del archivo
    CHECK(c.dirtyFrom() != INT_MAX);
    CHECK(!c.isValidThrough(n));

    // validamos hasta el final del archivo
    c.ensureValid(doc, n);
    CHECK(c.stateBefore(n - 1).inBlockComment);
    
    bool lastComment = false;
    for (auto& s : c.spansFor(n - 1))
        if (s.token == SyntaxToken::Comment) lastComment = true;
        
    CHECK(lastComment);
    CHECK(c.dirtyFrom() == INT_MAX);
}

TEST(syntax_cache_neutral_edit_converges_fast) {
    const int n = 2000;
    
    Document doc;
    doc.restore(makeCodeLines(n));

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    c.ensureValid(doc, n);

    // neutra: no cambia el estado propagado
    doc.insertText(100, 0, "x"); 
    c.markDirty(100);

    instrument::enable(true);
    instrument::resetFrame();
    
    c.ensureValid(doc, n);
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);

    CHECK(m.syntaxHighlighterCalls <= 4);
    CHECK(c.dirtyFrom() == INT_MAX);
    
    // verificamos que no lo marque como comentario
    CHECK(!c.stateBefore(101).inBlockComment);
}
