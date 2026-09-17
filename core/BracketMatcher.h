#pragma once
#include <optional>
#include <vector>
#include "core/Document.h"
#include "core/Position.h"
#include "syntax/SyntaxLanguage.h"
#include "syntax/SyntaxSpan.h"

struct BracketPair {
    Position open;
    Position close;
};
inline bool operator==(const BracketPair& a, const BracketPair& b) {
    return a.open == b.open && a.close == b.close;
}
inline bool operator!=(const BracketPair& a, const BracketPair& b) { return !(a == b); }

/**
 * @brief Busca el par de brackets correspondiente a pos.
 *
 * - pos usa byte columns (Document/Cursor, no visual columns).
 * - Soporta (), [], {} con matching estricto y anidamiento.
 * - Ignora brackets dentro de strings/chars/comments según SyntaxLanguage (None = sin filtro).
 * - Si pos no está sobre bracket (col/col-1), busca el par envolvente más interno que contiene pos (highlight persistente).
 * - Retorna nullopt si no hay match válido o pos está dentro de string/comment.
 */
std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, SyntaxLanguage lang = SyntaxLanguage::Cpp);
/// @brief Sobrecarga que deduce SyntaxLanguage desde filename (None → Cpp por defecto).
std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, const std::string& filename);
/// @brief Versión con spans precalculados (evita reconstruir highlighting, usada por Editor con cache).
std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, SyntaxLanguage lang, const std::vector<std::vector<SyntaxSpan>>& spansPerLine);
