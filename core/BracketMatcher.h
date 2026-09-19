#pragma once
#include <optional>
#include <vector>
#include <functional>
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
 * @brief Fuente lazy de spans para bracket matching.
 *
 * `ensure(line)` debe garantizar que los spans de `line` estén parseados.
 * `spans(line)` debe devolver los spans de esa línea (llamar solo después de ensure).
 */
struct BracketSpanSource {
    std::function<void(int)> ensure;
    std::function<const std::vector<SyntaxSpan>&(int)> spans;
};

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

/**
 * @brief Versión acotada a un rango de líneas [firstLine, lastLineExclusive).
 *
 * Regla: no resuelve nada fuera del rango. Si el match o el envolvente
 * requiere líneas fuera del rango, devuelve nullopt.
 */
std::optional<BracketPair> findMatchingBracketBounded(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    BracketSpanSource& src,
    int firstLine,
    int lastLineExclusive
);

/**
 * @brief Busca solo la posición del bracket de apertura correspondiente.
 *
 * Si `pos` está sobre un bracket de cierre, busca su apertura hacia atrás.
 * Si `pos` no está sobre un bracket, busca la apertura del bracket envolvente hacia atrás.
 * Devuelve nullopt si no se encuentra en el rango [firstLine, pos.line].
 *
 * Costo de scanning: O(distancia al match).
 * Costo de sintaxis: depende de SyntaxCache (materializa desde dirtyFrom hasta pos).
 */
std::optional<Position> findMatchingOpenBackward(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    BracketSpanSource& src,
    int firstLine
);

/**
 * @brief Busca solo la posición del bracket de cierre correspondiente.
 *
 * Si `pos` está sobre un bracket de apertura, busca su cierre hacia adelante.
 * Si `pos` no está sobre un bracket, busca el cierre del bracket envolvente.
 * (Requiere encontrar primero el open hacia atrás para saber qué se está cerrando).
 *
 * Costo de scanning: O(distancia al open + distancia al close).
 * Costo de sintaxis: depende de SyntaxCache (materializa desde dirtyFrom hasta el match).
 */
std::optional<Position> findMatchingCloseForward(
    const Document& doc,
    Position pos,
    SyntaxLanguage lang,
    BracketSpanSource& src,
    int lastLineExclusive
);