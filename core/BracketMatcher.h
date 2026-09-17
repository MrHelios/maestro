#pragma once
#include <optional>
#include "core/Document.h"
#include "core/Position.h"
#include "syntax/SyntaxLanguage.h"

struct BracketPair {
    Position open;
    Position close;
};
inline bool operator==(const BracketPair& a, const BracketPair& b) {
    return a.open == b.open && a.close == b.close;
}
inline bool operator!=(const BracketPair& a, const BracketPair& b) { return !(a == b); }

std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, SyntaxLanguage lang = SyntaxLanguage::Cpp);
std::optional<BracketPair> findMatchingBracket(const Document& doc, Position pos, const std::string& filename);
