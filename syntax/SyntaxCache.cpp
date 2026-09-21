#include "syntax/SyntaxCache.h"
#include <algorithm>
#include "core/Instrument.h"

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
    parsedUpTo_ = 0;
}

void SyntaxCache::markDirty(int fromLine) {
    if (fromLine < 0) fromLine = 0;
    dirtyFrom_ = std::min(dirtyFrom_, fromLine);
}

void SyntaxCache::onInsertLines(int at, int count) {
    if (count <= 0) return;
    if (at < 0) at = 0;
    
    if ((int)before_.size() >= at + 1) {
        before_.insert(before_.begin() + at + 1, count, SyntaxState{});
    }
    if ((int)spans_.size() >= at) {
        spans_.insert(spans_.begin() + at, count, std::vector<SyntaxSpan>{});
    }
    
    // Las líneas insertadas no están parseadas
    parsedUpTo_ = std::min(parsedUpTo_, at);
    markDirty(at);
}

void SyntaxCache::onRemoveLines(int at, int count) {
    if (count <= 0) return;
    if (at < 0) at = 0;
    
    if ((int)before_.size() > at) {
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
    
    // Conservador: las líneas posteriores pueden haber cambiado
    parsedUpTo_ = std::min(parsedUpTo_, at);
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
    
    int oldN = (int)spans_.size();
    
    // ... (todo el código existente de splice heurístico sin cambios) ...
    
    // Ajuste final por si queda desalineado
    if ((int)before_.size() != n + 1) {
        before_.resize(n + 1);
    }
    if ((int)spans_.size() != n) {
        spans_.resize(n);
        // Si el documento creció, las líneas nuevas NO están parseadas.
        // parsedUpTo_ no puede cubrir líneas que nunca vimos.
        if (n > oldN) {
            parsedUpTo_ = std::min(parsedUpTo_, oldN);
            dirtyFrom_ = std::min(dirtyFrom_, oldN);
        }
    }
    if (before_.empty()) {
        before_.push_back(SyntaxState{});
    }
}

void SyntaxCache::ensureValid(const Document& doc, int upTo) {
    bool doInstr = instrument::enabled;
    uint64_t t0 = doInstr ? instrument::nowNanos() : 0;
    auto doRecord = [&](int requestedClamped, int parsedLines, int hlCalls){
        if (!doInstr) return;
        uint64_t ns = instrument::nowNanos() - t0;
        instrument::recordSyntaxEnsureValid(requestedClamped, parsedLines, ns);
        for (int i=0;i<hlCalls;++i) instrument::recordHighlighterCall();
    };
    if (language_ == SyntaxLanguage::None) {
        ensureSize(doc);
        dirtyFrom_ = INT_MAX;
        parsedUpTo_ = doc.lineCount();
        int req = std::clamp(upTo, 0, doc.lineCount());
        doRecord(req, 0, 0);
        return;
    }
    
    syncDocument(doc);
    int n = doc.lineCount();
    
    if (n == 0) {
        before_.assign(1, SyntaxState{});
        spans_.clear();
        dirtyFrom_ = INT_MAX;
        parsedUpTo_ = 0;
        int req = 0;
        doRecord(req, 0, 0);
        return;
    }
    
    ensureSize(doc);
    
    if (upTo < 0) upTo = 0;
    if (upTo > n) upTo = n;
    int requestedClamped = upTo;
    
    // Fast path: ya está válido hasta upTo
    if (dirtyFrom_ == INT_MAX && parsedUpTo_ >= upTo) {
        if (doInstr && upTo < 100) {
            // debug
            // std::cerr << "ensureValid fast upTo="<<upTo<<" parsedUpTo="<<parsedUpTo_<<" dirty="<<dirtyFrom_<<"\n";
        }
        doRecord(requestedClamped, 0, 0);
        return;
    }
    
    // Determinar dónde empezar a parsear
    int start = dirtyFrom_;
    if (start > upTo) start = upTo;
    
    // Si no hay dirty pero faltan líneas parseadas, empezar donde termina lo parseado
    if (start >= upTo || start > parsedUpTo_) {
        start = parsedUpTo_;
    }
    
    if (start >= upTo) {
        if (dirtyFrom_ < upTo) dirtyFrom_ = upTo;
        if (dirtyFrom_ >= n) dirtyFrom_ = INT_MAX;
        doRecord(requestedClamped, 0, 0);
        return;
    }
    
        // Parsear desde start hasta upTo (o hasta convergencia en cache caliente)
    std::vector<SyntaxSpan> buf;
    SyntaxState state = before_[start];
    int parsedCount = 0;
    int hlCount = 0;
    
    for (int l = start; l < upTo; ++l) {
        SyntaxState oldOut = before_[l + 1];
        SyntaxState out;
        
        highlighter_.highlight(doc.lineAt(l), state, out, buf);
        hlCount++;
        parsedCount++;
        spans_[l] = buf;
        before_[l + 1] = out;
        
        // Actualizar parsedUpTo_
        if (l + 1 > parsedUpTo_) {
            parsedUpTo_ = l + 1;
        }

        // Convergencia real: estado sin cambios Y sufijo ya parseado
        if (out == oldOut && parsedUpTo_ > l) {
            if (upTo <= parsedUpTo_) {
                // Todo el rango pedido es válido
                break;
            }
            // Saltar líneas ya parseadas con estado invariante
            l = parsedUpTo_ - 1;
            state = before_[parsedUpTo_];
            continue;
        }
        
        state = out;
    }
    // Si hemos parseado hasta upTo y no hay dirty pendiente, marcar como limpio
    if (parsedUpTo_ >= upTo && dirtyFrom_ < upTo) {
        dirtyFrom_ = (parsedUpTo_ >= (int)spans_.size() ? INT_MAX : parsedUpTo_);
        if (dirtyFrom_ >= (int)spans_.size()) dirtyFrom_ = INT_MAX;
    }
    doRecord(requestedClamped, parsedCount, hlCount);
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
