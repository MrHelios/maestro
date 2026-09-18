#include "syntax/SyntaxCache.h"
#include <algorithm>

void SyntaxCache::setLanguage(SyntaxLanguage lang) {
    if (lang == language_) return;
    language_ = lang;
    highlighter_.setLanguage(lang);
    invalidateAll();
}

void SyntaxCache::invalidateAll() {
    before_.clear();
    spans_.clear();
    dirtyFrom_ = 0;
    // docPtr_ se mantiene para detectar reuse, pero se marcará como dirty total
}

void SyntaxCache::markDirty(int fromLine) {
    if (fromLine < 0) fromLine = 0;
    dirtyFrom_ = std::min(dirtyFrom_, fromLine);
}

void SyntaxCache::onInsertLines(int at, int count) {
    if (count <= 0) return;
    if (at < 0) at = 0;
    // before_ tiene N+1, spans_ N
    // Inserción de 'count' líneas en at (índice de líneas)
    // before: insertar count estados en at+1? El estado antes de la nueva línea
    // debe ser copiado del estado previo para mantener continuidad hasta reparse.
    // Simplificamos insertando estados default en at+1.
    if ((int)before_.size() >= at + 1) {
        before_.insert(before_.begin() + at + 1, count, SyntaxState{});
    } else {
        // si el cache aún no tiene tamaño, ensureSize lo creará
        // solo marcar dirty
    }
    if ((int)spans_.size() >= at) {
        spans_.insert(spans_.begin() + at, count, std::vector<SyntaxSpan>{});
    }
    markDirty(at);
}

void SyntaxCache::onRemoveLines(int at, int count) {
    if (count <= 0) return;
    if (at < 0) at = 0;
    if ((int)before_.size() > at) {
        // before tiene N+1, remover count entradas a partir de at+1?
        // Para deleción de líneas at .. at+count-1, removemos before at+1 .. at+count y spans at .. at+count-1
        // Ajuste: si at es línea base, el estado antes de at+1 ya no va.
        int beforeAt = at + 1;
        if (beforeAt < (int)before_.size()) {
            int rem = std::min(count, (int)before_.size() - beforeAt);
            before_.erase(before_.begin() + beforeAt, before_.begin() + beforeAt + rem);
        }
    }
    if ((int)spans_.size() > at) {
        int rem = std::min(count, (int)spans_.size() - at);
        spans_.erase(spans_.begin() + at, spans_.begin() + at + rem);
    }
    markDirty(at);
}

void SyntaxCache::syncDocument(const Document& doc) {
    if (docPtr_ != &doc || docInstanceId_ != doc.instanceId()) {
        // cambio de documento/buffer
        docPtr_ = &doc;
        docInstanceId_ = doc.instanceId();
        docVersion_ = doc.version();
        invalidateAll();
        return;
    }
    if (docVersion_ != doc.version()) {
        // Detecta mutación sin callback (standalone tests). Fallback a dirty 0.
        if (dirtyFrom_ == INT_MAX) dirtyFrom_ = 0;
        docVersion_ = doc.version();
    } else {
        // mantener versión actualizada por si primera vez
        docVersion_ = doc.version();
    }
}

void SyntaxCache::ensureSize(const Document& doc) {
    int n = doc.lineCount();
    if ((int)spans_.size() == n && (int)before_.size() == n + 1) return;
    // Si hay mismatch y no tenemos info de splice, intentar preservar prefijo hasta dirtyFrom
    int oldN = (int)spans_.size();
    if (oldN != n) {
        int delta = n - oldN;
        if (dirtyFrom_ != INT_MAX && dirtyFrom_ <= oldN && delta != 0) {
            // Heurística: splice en dirtyFrom+1 (caso split/insert/delete)
            int at = dirtyFrom_ + 1;
            if (at < 0) at = 0;
            if (delta > 0) {
                if (at <= (int)before_.size()) {
                    before_.insert(before_.begin() + std::min(at, (int)before_.size()), delta, SyntaxState{});
                }
                if (at <= (int)spans_.size()) {
                    spans_.insert(spans_.begin() + std::min(at, (int)spans_.size()), delta, std::vector<SyntaxSpan>{});
                }
            } else {
                int rem = -delta;
                int beforeAt = at;
                if (beforeAt < (int)before_.size()) {
                    int r = std::min(rem, (int)before_.size() - beforeAt);
                    before_.erase(before_.begin() + beforeAt, before_.begin() + beforeAt + r);
                }
                if (at < (int)spans_.size()) {
                    int r = std::min(rem, (int)spans_.size() - at);
                    spans_.erase(spans_.begin() + at, spans_.begin() + at + r);
                }
            }
        }
    }
    // Ajuste final por si queda desalineado
    if ((int)before_.size() != n + 1) {
        before_.resize(n + 1);
    }
    if ((int)spans_.size() != n) {
        spans_.resize(n);
    }
    if (before_.empty()) {
        before_.push_back(SyntaxState{});
    }
}

void SyntaxCache::ensureValid(const Document& doc, int upTo) {
    if (language_ == SyntaxLanguage::None) {
        // Sin lenguaje no hay estados; mantener tamaños pero no parsear
        ensureSize(doc);
        dirtyFrom_ = INT_MAX;
        return;
    }
    syncDocument(doc);
    int n = doc.lineCount();
    if (n == 0) {
        before_.assign(1, SyntaxState{});
        spans_.clear();
        dirtyFrom_ = INT_MAX;
        return;
    }
    ensureSize(doc);
    if (upTo < 0) upTo = 0;
    if (upTo > n) upTo = n;
    // Si dirty es INT_MAX, ya está válido hasta n; solo verificar upTo
    if (dirtyFrom_ == INT_MAX) {
        // Pero si antes nunca se parseó, before_[0] es default y resto vacío: necesitamos parsear
        // Detectar si alguna línea hasta upTo no ha sido parseada (before aún default y spans vacío pero debería tener contenido)
        // Simplificamos: si before_.size() == n+1 y spans validos, asumimos válido.
        // Si no, forzar dirty 0
        // Para inicialización, invalidateAll deja dirty 0, así que no llegamos aquí con INT_MAX sin datos.
        return;
    }
    if (dirtyFrom_ >= n && upTo <= n) {
        dirtyFrom_ = INT_MAX;
        return;
    }
    int start = dirtyFrom_;
    if (start > upTo) {
        return;
    }
    std::vector<SyntaxSpan> buf;
    bool earlyConverged = false;
    for (int l = start; l < n; ++l) {
        SyntaxState in = before_[l];
        SyntaxState oldOut = (l + 1 < (int)before_.size()) ? before_[l + 1] : SyntaxState{};
        SyntaxState out;
        highlighter_.highlight(doc.lineAt(l), in, out, buf);
        spans_[l] = buf;
        before_[l + 1] = out;
        if (out == oldOut) {
            // Convergencia: el estado posterior es idéntico al cacheado, el sufijo es válido
            if (l + 1 >= upTo) {
                earlyConverged = true;
                break;
            }
            earlyConverged = true;
            break;
        }
        if (l + 1 == upTo && !earlyConverged) {
            if (upTo < n) {
                dirtyFrom_ = upTo;
                return;
            }
        }
    }
    (void)earlyConverged;
    dirtyFrom_ = INT_MAX;
}

const SyntaxState& SyntaxCache::stateBefore(int line) const {
    static SyntaxState empty;
    if (line < 0 || line >= (int)before_.size()) return empty;
    return before_[line];
}

const std::vector<SyntaxSpan>& SyntaxCache::spansFor(int line) const {
    static std::vector<SyntaxSpan> empty;
    if (line < 0 || line >= (int)spans_.size()) return empty;
    return spans_[line];
}

SyntaxState SyntaxCache::stateAt(int line, const Document& doc) {
    ensureValid(doc, line);
    if (line < 0) return SyntaxState{};
    if (line >= (int)before_.size()) return SyntaxState{};
    return before_[line];
}
