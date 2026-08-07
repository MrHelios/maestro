#include "Document.h"

#include <fstream>
#include <sstream>

Document::Document() {
    // Un documento nunca esta "vacio del todo": siempre tiene al menos
    // una linea (posiblemente vacia). Esto simplifica muchisimo el
    // resto del codigo (cursor, renderer, etc).
    lines_.push_back("");
}

bool Document::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // Archivo nuevo: dejamos el documento con una linea vacia.
        lines_.clear();
        lines_.push_back("");
        return false;
    }

    lines_.clear();
    std::string line;
    while (std::getline(file, line)) {
        // getline ya nos da la linea sin el '\n'.
        // Si el archivo usa \r\n, sacamos el \r final.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines_.push_back(line);
    }

    if (lines_.empty()) {
        lines_.push_back("");
    }

    return true;
}

bool Document::saveToFile(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    for (size_t i = 0; i < lines_.size(); ++i) {
        file << lines_[i];
        if (i + 1 < lines_.size()) {
            file << '\n';
        }
    }

    return true;
}

int Document::lineCount() const {
    return static_cast<int>(lines_.size());
}

int Document::lineLength(int line) const {
    if (line < 0 || line >= lineCount()) return 0;
    return static_cast<int>(lines_[line].size());
}

const std::string& Document::lineAt(int line) const {
    static const std::string empty;
    if (line < 0 || line >= lineCount()) return empty;
    return lines_[line];
}

std::vector<std::string> Document::snapshot() const {
    return lines_;
}

void Document::restore(const std::vector<std::string>& lines) {
    lines_ = lines;
    if (lines_.empty()) {
        lines_.push_back("");
    }
}

void Document::insertChar(int line, int col, char c) {
    if (line < 0 || line >= lineCount()) return;
    std::string& target = lines_[line];
    if (col < 0) col = 0;
    if (col > static_cast<int>(target.size())) col = static_cast<int>(target.size());
    target.insert(target.begin() + col, c);
}

void Document::insertNewline(int line, int col) {
    if (line < 0 || line >= lineCount()) return;
    std::string& target = lines_[line];
    if (col < 0) col = 0;
    if (col > static_cast<int>(target.size())) col = static_cast<int>(target.size());

    std::string rest = target.substr(col);
    target.erase(col);
    lines_.insert(lines_.begin() + line + 1, rest);
}

bool Document::deleteCharBefore(int line, int col) {
    if (line < 0 || line >= lineCount()) return false;

    // Clamp de la columna para que erase() nunca salga de rango.
    const int len = lineLength(line);
    if (col < 0) col = 0;
    if (col > len) col = len;

    if (col > 0) {
        std::string& target = lines_[line];
        target.erase(target.begin() + (col - 1));
        return true;
    }

    // col == 0: fundir con la linea anterior, si existe.
    if (line == 0) return false;

    std::string current = lines_[line];
    lines_.erase(lines_.begin() + line);
    lines_[line - 1] += current;
    return true;
}

bool Document::deleteCharAt(int line, int col) {
    if (line < 0 || line >= lineCount()) return false;

    // Clamp de la columna para que erase() nunca salga de rango.
    if (col < 0) col = 0;

    int len = lineLength(line);
    if (col < len) {
        std::string& target = lines_[line];
        target.erase(target.begin() + col);
        return true;
    }

    // col == len: fundir con la linea siguiente, si existe.
    if (line + 1 >= lineCount()) return false;

    std::string next = lines_[line + 1];
    lines_.erase(lines_.begin() + line + 1);
    lines_[line] += next;
    return true;
}
