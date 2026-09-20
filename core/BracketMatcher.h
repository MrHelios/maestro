#pragma once

#include <functional>
#include <optional>
#include <string>
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

inline bool operator!=(const BracketPair& a, const BracketPair& b) { 
    return !(a == b); 
}

/**
 * @brief Fuente lazy de spans para bracket matching.
 */
struct BracketSpanSource {
    std::function<void(int)> ensure;
    std::function<const std::vector<SyntaxSpan>&(int)> spans;
};

// ---------------------------------------------------------------------------
// Legacy overloads (mantenidos para compatibilidad y tests unitarios)
// ---------------------------------------------------------------------------

std::optional<BracketPair> findMatchingBracket(
    const Document& doc, Position pos, SyntaxLanguage lang = SyntaxLanguage::Cpp);

std::optional<BracketPair> findMatchingBracket(
    const Document& doc, Position pos, const std::string& filename);

std::optional<BracketPair> findMatchingBracket(
    const Document& doc, Position pos, SyntaxLanguage lang,
    const std::vector<std::vector<SyntaxSpan>>& spansPerLine);

// ---------------------------------------------------------------------------
// Nuevas APIs para highlight viewport-only y jump incremental
// ---------------------------------------------------------------------------

std::optional<BracketPair> findMatchingBracketBounded(
    const Document& doc, Position pos, SyntaxLanguage lang,
    BracketSpanSource& src, int firstLine, int lastLineExclusive);

std::optional<BracketPair> findMatchingBracketFrom(
    const Document& doc, Position pos, SyntaxLanguage lang,
    BracketSpanSource& src);

std::optional<Position> findMatchingOpenBackward(
    const Document& doc, Position pos, SyntaxLanguage lang,
    BracketSpanSource& src, int firstLine);

std::optional<Position> findMatchingCloseForward(
    const Document& doc, Position pos, SyntaxLanguage lang,
    BracketSpanSource& src, int lastLineExclusive);