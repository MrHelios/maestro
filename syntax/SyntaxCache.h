#pragma once
#include <climits>
#include <vector>
#include "syntax/SyntaxSpan.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxHighlighter.h"
#include "core/Document.h"

// Cache incremental para highlight sintáctico.
// Mantiene before[N+1] (estado antes de cada línea) y spans[N].
// El rango sucio es [dirtyFrom_, dirtyMax_]: before_[dirtyFrom_] es válido
// y desde ahí todo es sospechoso hasta cubrir dirtyMax_. La convergencia
// solo cuenta pasado dirtyMax_; sin ese extremo derecho, un break temprano
// puede saltear una segunda región dirty y declarar limpio en falso.
// Invalida solo desde dirtyFromLine y reparsea hasta convergencia
// (nuevo stateOut == cacheado stateOut) preservando suffix.
class SyntaxCache {
public:
    void setLanguage(SyntaxLanguage lang);
    SyntaxLanguage language() const { return language_; }

    // Invalida todo (load/restore/cambio de documento o lenguaje)
    void invalidateAll();

    // Marca dirty a partir de línea (inclusive). Usado para ediciones de contenido.
    void markDirty(int fromLine);

    // Cambios estructurales: splice de vectores para preservar alineación del sufijo.
    void onInsertLines(int at, int count);
    void onRemoveLines(int at, int count);

    // Sincroniza con el documento actual: detecta cambio de instancia,
    // ajusta tamaños si lineCount cambió sin notificación y resetea dirty si hace falta.
    void syncDocument(const Document& doc);

    // Garantiza validez hasta upTo (exclusive, clamp a lineCount). No parsea más allá de convergencia.
    void ensureValid(const Document& doc, int upTo);

    // Accesos válidos tras ensureValid
    const SyntaxState& stateBefore(int line) const;
    const std::vector<SyntaxSpan>& spansFor(int line) const;
    const std::vector<std::vector<SyntaxSpan>>& allSpans() const { return spans_; }
    const std::vector<SyntaxState>& allBefore() const { return before_; }
    SyntaxState stateAt(int line, const Document& doc); // garantiza y devuelve

    // Evita llamar ensureValid cuando ya está válido
    bool isValidThrough(int line) const {
        if (language_ == SyntaxLanguage::None) return true;
        if (line < 0) return true;
        if (line > (int)spans_.size()) line = (int)spans_.size();
        return parsedUpTo_ >= line && dirtyFrom_ >= line;
    }

    // Para tests/debug
    int dirtyFrom() const { return dirtyFrom_; }
    int dirtyMax() const { return dirtyMax_; }
    int parsedUpTo() const { return parsedUpTo_; }
    size_t size() const { return spans_.size(); }

private:
    SyntaxLanguage language_ = SyntaxLanguage::None;
    SyntaxHighlighter highlighter_;

    std::vector<SyntaxState> before_; // size N+1
    std::vector<std::vector<SyntaxSpan>> spans_; // size N

    int dirtyFrom_ = INT_MAX;
    int dirtyMax_ = -1; // última línea editada (inclusive); -1 == limpio

    const Document* docPtr_ = nullptr;
    uint64_t docInstanceId_ = 0;
    uint64_t docVersion_ = UINT64_MAX;
    int parsedUpTo_ = 0;  // Líneas [
    void ensureSize(const Document& doc);
};
