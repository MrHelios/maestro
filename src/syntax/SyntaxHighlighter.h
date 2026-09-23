#pragma once
#include <string_view>
#include <vector>
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"
#include "syntax/CLikeHighlighter.h"

/**
 * @file SyntaxHighlighter.h
 * @brief Wrapper que elige el resaltador según SyntaxLanguage.
 *
 * - Si lang==None no genera spans (early return, 0 alloc).
 * - Para C/Cpp delega en CLikeHighlighter con el mismo SyntaxState.
 * - Ofrece dos familias: retorno por valor (conveniencia) y `out` reutilizable
 *   (path caliente en Renderer para evitar allocations por línea).
 */
class SyntaxHighlighter {
public:
    void setLanguage(SyntaxLanguage lang) { lang_ = lang; }
    SyntaxLanguage language() const { return lang_; }

    /** @brief Conveniencia sin estado (línea aislada). */
    std::vector<SyntaxSpan> highlight(std::string_view line) const;
    /** @brief Conveniencia con estado, retorna por valor. */
    std::vector<SyntaxSpan> highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext) const;
    /**
     * @brief Path caliente: reutiliza `out` (clear + fill) para evitar
     *        allocation por línea; usado por Renderer::syntaxStateAt y
     *        renderEditorContent.
     */
    void highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, std::vector<SyntaxSpan>& out) const;
    /** @brief Overload sin estado con buffer reutilizable. */
    void highlight(std::string_view line, std::vector<SyntaxSpan>& out) const;
private:
    SyntaxLanguage lang_ = SyntaxLanguage::None;
    CLikeHighlighter cLike_;
};
