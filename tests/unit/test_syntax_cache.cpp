#include "test_framework.h"
#include "core/Document.h"
#include "core/Instrument.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxToken.h"
#include <string>
#include <vector>

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

// Dos regiones dirty sin ensureValid entre medio + ensure parcial que no
// alcanza la segunda: con dirtyFrom_ escalar la convergencia corta antes de
// la línea 100 y la cola declara limpio en falso (stale desde 101).
TEST(syntax_cache_two_dirty_regions_partial_ensure) {
    Document doc;
    doc.restore(makeCodeLines(200));

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    c.ensureValid(doc, 200); // caliente

    // Ediciones in-place (sin cambio de lineCount), sin ensureValid entre medio.
    // Nota: Document pelado no tiene callback de Buffer; los markDirty
    // manuales simulan el path de producción (Buffer::rebindCallback).
    doc.insertText(5, 0, "x");      // neutra: no cambia el estado propagado
    c.markDirty(5);
    doc.insertText(100, 0, "/* ");  // abre comentario: cambia before_[101+]
    c.markDirty(100);

    c.ensureValid(doc, 30);  // parcial: no llega a la segunda edición
    c.ensureValid(doc, 200);

    SyntaxCache fresh;
    fresh.setLanguage(SyntaxLanguage::Cpp);
    fresh.ensureValid(doc, 200);

    // La edición de la línea 100 debe verse reflejada desde la 101.
    CHECK(fresh.stateBefore(101).inBlockComment);
    CHECK(c.stateBefore(101).inBlockComment);

    int stateMismatch = 0, spanMismatch = 0;
    for (int l = 0; l < 200; ++l) {
        if (c.stateBefore(l) != fresh.stateBefore(l)) ++stateMismatch;
        if (!spansEqual(c.spansFor(l), fresh.spansFor(l))) ++spanMismatch;
    }
    CHECK_EQ(stateMismatch, 0);
    CHECK_EQ(spanMismatch, 0);
}

// Paso 1 (contrato): ensure parcial (upTo < n) sin convergencia en el borde
// deja el cache dirty, aunque upTo > dirtyMax_. Si se declarara limpio, el
// sufijo más allá de upTo quedaría stale permanente (el scroll lo creería
// válido). Edición pequeña y determinística, sin 20k líneas.
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
    c.ensureValid(doc, 6); // caliente

    doc.insertText(1, 0, "/* "); // abre bloque sin cerrar: propaga hasta EOF
    c.markDirty(1);

    c.ensureValid(doc, 2); // parcial: solo la fila editada (como el fast-path viejo)
    // El borde cambió (before_[2] pasó a inBlockComment): no convergió,
    // así que debe seguir dirty aunque 2 > dirtyMax_(1).
    CHECK(c.dirtyFrom() != INT_MAX);
    CHECK(!c.isValidThrough(6));
    // ...pero el rango pedido sí quedó válido.
    CHECK(c.isValidThrough(2));
    CHECK(c.stateBefore(2).inBlockComment);

    // Al completar (como haría un scroll), converge con el oráculo.
    c.ensureValid(doc, 6);
    SyntaxCache fresh;
    fresh.setLanguage(SyntaxLanguage::Cpp);
    fresh.ensureValid(doc, 6);
    int stateMismatch = 0, spanMismatch = 0;
    for (int l = 0; l < 6; ++l) {
        if (c.stateBefore(l) != fresh.stateBefore(l)) ++stateMismatch;
        if (!spansEqual(c.spansFor(l), fresh.spansFor(l))) ++spanMismatch;
    }
    CHECK_EQ(stateMismatch, 0);
    CHECK_EQ(spanMismatch, 0);
    CHECK(c.dirtyFrom() == INT_MAX);
}

// Paso 2/4 (budget + tail): 20k líneas, /* sin cerrar, viewport 40.
// El ensure de viewport debe costar ~= viewport (nunca 20k) y dejar dirty
// el tail; el scroll posterior (ensure completo) debe colorear el final.
TEST(syntax_cache_block_comment_20k_viewport_budget_and_tail) {
    const int n = 20000;
    const int viewport = 40;
    Document doc;
    doc.restore(makeCodeLines(n));

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    c.ensureValid(doc, n); // caliente (costo one-time fuera de medición)

    doc.insertText(0, 0, "/* "); // comentario hasta EOF
    c.markDirty(0);

    instrument::enable(true);
    instrument::resetFrame();
    c.ensureValid(doc, viewport); // lo que necesita una tecla interactiva
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);

    // Budget: ~viewport highlights, nunca n. Slack generoso anti-flaky.
    CHECK(m.syntaxHighlighterCalls <= 64);
    CHECK(m.syntaxHighlighterCalls >= 1);
    // Viewport correcto...
    CHECK(c.stateBefore(1).inBlockComment);
    CHECK(c.stateBefore(viewport).inBlockComment);
    bool tailSpanComment = false;
    for (auto& s : c.spansFor(viewport - 1))
        if (s.token == SyntaxToken::Comment) tailSpanComment = true;
    CHECK(tailSpanComment);
    // ...y tail todavía dirty (contrato Paso 1), no declarado limpio en falso.
    CHECK(c.dirtyFrom() != INT_MAX);
    CHECK(!c.isValidThrough(n));

    // Scroll hasta el final: debe colorearse como comentario.
    c.ensureValid(doc, n);
    CHECK(c.stateBefore(n - 1).inBlockComment);
    bool lastComment = false;
    for (auto& s : c.spansFor(n - 1))
        if (s.token == SyntaxToken::Comment) lastComment = true;
    CHECK(lastComment);
    CHECK(c.dirtyFrom() == INT_MAX);

    SyntaxCache fresh;
    fresh.setLanguage(SyntaxLanguage::Cpp);
    fresh.ensureValid(doc, n);
    CHECK(c.stateBefore(n - 1) == fresh.stateBefore(n - 1));
    CHECK(spansEqual(c.spansFor(n - 1), fresh.spansFor(n - 1)));
}

// Control de no-regresión: edición neutra en cache caliente converge en
// O(1) highlights y declara limpio (el fast-path de una fila sigue barato).
TEST(syntax_cache_neutral_edit_converges_fast) {
    const int n = 20000;
    Document doc;
    doc.restore(makeCodeLines(n));

    SyntaxCache c;
    c.setLanguage(SyntaxLanguage::Cpp);
    c.ensureValid(doc, n); // caliente

    doc.insertText(100, 0, "x"); // neutra: no cambia el estado propagado
    c.markDirty(100);

    instrument::enable(true);
    instrument::resetFrame();
    c.ensureValid(doc, n);
    auto m = instrument::snapshotAndReset();
    instrument::enable(false);

    CHECK(m.syntaxHighlighterCalls <= 4);
    CHECK(c.dirtyFrom() == INT_MAX);
    CHECK(!c.stateBefore(101).inBlockComment);
}
