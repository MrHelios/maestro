#pragma once
#include <string_view>
#include <vector>
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"

/**
 * @brief Resaltador para lenguajes tipo C/C++.
 *
 * Diseñado para ser *stateless* por línea salvo por SyntaxState:
 * - inBlockComment / inRawString se propagan entre líneas; el resto
 *   del análisis es intra-línea y sin heap (comparaciones string_view).
 * - Limitación pragmática: strings normales se consideran intra-línea y no
 *   se propaga estado de string entre líneas (un '"' sin cierre marca hasta
 *   EOL). Solo raw strings R"delim(...)delim" y comentarios /\* ... *\/ cruzan
 *   y por eso viven en SyntaxState.
 * - El preprocesador se reconoce solo si es el primer token no-espacio
 *   (tras posibles /\*...*\/ líderes) y consume hasta fin de línea.
 */
class CLikeHighlighter {
public:
    /**
     * @brief Resalta una línea reutilizando el buffer de salida.
     * @param line     Vista de la línea (sin '\n').
     * @param state    Estado de entrada (viene de la línea anterior).
     * @param outNext  Estado de salida para la siguiente línea.
     * @param lang     Lenguaje (C/Cpp); None no genera spans.
     * @param out      Buffer de salida reutilizable; se hace clear() al inicio
     *                 para evitar allocations por línea (ver bench highlight).
     */
    void highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang, std::vector<SyntaxSpan>& out) const;

    /**
     * @brief Conveniencia que aloca y retorna; preferir la sobrecarga con out
     *        en paths calientes (render) para reutilizar capacidad.
     */
    std::vector<SyntaxSpan> highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang) const;
};
