#include "test_framework.h"
#include "core/Document.h"
#include "syntax/SyntaxCache.h"
#include "syntax/SyntaxLanguage.h"
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
