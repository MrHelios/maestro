#pragma once

#include <string_view>
#include <vector>

#include "syntax/SyntaxSpan.h"

/**
 * @brief Resaltador para Rust.
 *
 * Diseñado para ser stateless por línea salvo por SyntaxState:
 * - rustBlockCommentDepth: comentarios de bloque anidados.
 * - rustInRawString / rustRawHashCount: raw strings multilínea.
 *
 * Limitaciones pragmáticas de esta primera versión:
 * - strings normales, byte strings y C strings no propagan estado entre líneas;
 *   si no cierran en la línea, se marcan hasta EOL.
 * - lifetimes se reconocen para no confundirlos con char literals,
 *   pero inicialmente no generan span propio.
 * - weak keywords como union, safe, raw, macro_rules no se resaltan
 *   incondicionalmente para evitar falsos positivos.
 * - no se intenta parsing semántico, ni detección de tipos de usuario,
 *   ni macros contextuales, ni atributos especiales.
 */
class RustHighlighter {
public:
    /**
     * @brief Resalta una línea reutilizando el buffer de salida.
     *
     * @param line     Vista de la línea (sin '\n').
     * @param state    Estado de entrada (viene de la línea anterior).
     * @param outNext  Estado de salida para la siguiente línea.
     * @param out      Buffer de salida reutilizable; se hace clear() al inicio.
     */
    void highlight(std::string_view line,
                   const SyntaxState& state,
                   SyntaxState& outNext,
                   std::vector<SyntaxSpan>& out) const;

    /**
     * @brief Conveniencia que aloca y retorna.
     *        Preferir la sobrecarga con out en paths calientes.
     */
    std::vector<SyntaxSpan> highlight(std::string_view line,
                                      const SyntaxState& state,
                                      SyntaxState& outNext) const;
};
