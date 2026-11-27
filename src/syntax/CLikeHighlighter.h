#pragma once

#include <string_view>
#include <vector>

#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"

/**
 * @brief Resaltador para lenguajes tipo C/C++.
 */
class CLikeHighlighter {
public:
    /**
     * @brief          Resalta una línea reutilizando el buffer de salida.
     * @param line     Vista de la línea (sin '\n').
     * @param state    Estado de entrada (viene de la línea anterior).
     * @param outNext  Estado de salida para la siguiente línea.
     * @param lang     Lenguaje (C/Cpp), None no genera spans.
     * @param out      Buffer de salida reutilizable
     */
    void highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, SyntaxLanguage lang, 
                    std::vector<SyntaxSpan>& out) const;

    /**
     * @brief Conveniencia que aloca y retorna; preferir la sobrecarga con out
     *        en paths calientes (render) para reutilizar capacidad.
     */
    std::vector<SyntaxSpan> highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, 
                                        SyntaxLanguage lang) const;
};
