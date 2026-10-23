#pragma once
#include <string_view>
#include <vector>
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"

/**
 * @brief Resaltador para Markdown.
 *
 * Diseñado para ser stateless por línea salvo por SyntaxState:
 * - inCodeBlock / codeBlockDelimChar / codeBlockDelimLen se propagan
 *   entre líneas para bloques cercados (``` o ~~~).
 * - El resto del análisis es intra-línea: headings, bold, italic,
 *   inline code, links, images, blockquotes, horizontal rules, lists.
 * - Los marcadores dentro de código inline o bloques cercados no
 *   activan reglas externas.
 */
class MarkdownHighlighter {
public:
    /**
     * @brief Resalta una línea reutilizando el buffer de salida.
     * @param line     Vista de la línea (sin '\n').
     * @param state    Estado de entrada (viene de la línea anterior).
     * @param outNext  Estado de salida para la siguiente línea.
     * @param out      Buffer de salida reutilizable; se hace clear() al inicio.
     */
    void highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext, std::vector<SyntaxSpan>& out) const;

    /**
     * @brief Conveniencia que aloca y retorna.
     */
    std::vector<SyntaxSpan> highlight(std::string_view line, const SyntaxState& state, SyntaxState& outNext) const;
};