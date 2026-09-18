#include "core/Document.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/utf8.h"
#include "filesystem/FileSystem.h"
#include <vector>

std::atomic<uint64_t> Document::s_nextInstanceId{1};

Document::Document() : instanceId_(s_nextInstanceId.fetch_add(1, std::memory_order_relaxed)) {
    // Un documento nunca esta "vacio del todo": siempre tiene al menos
    // una linea (posiblemente vacia). Esto simplifica muchisimo el
    // resto del codigo (cursor, renderer, etc).
    lines_.push_back("");
}

Document::Document(const Document& other)
    : instanceId_(s_nextInstanceId.fetch_add(1, std::memory_order_relaxed)),
      lines_(other.lines_),
      touchedCallbacks_(other.touchedCallbacks_),
      version_(other.version_),
      lineEnding_(other.lineEnding_),
      endsWithNewline_(other.endsWithNewline_) {}

Document& Document::operator=(const Document& other) {
    if (this != &other) {
        lines_ = other.lines_;
        touchedCallbacks_ = other.touchedCallbacks_;
        version_ = other.version_;
        lineEnding_ = other.lineEnding_;
        endsWithNewline_ = other.endsWithNewline_;
        // nueva identidad para evitar colisión de cache por reutilización de dirección
        instanceId_ = s_nextInstanceId.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
}

Document::Document(Document&& other) noexcept
    : instanceId_(other.instanceId_),
      lines_(std::move(other.lines_)),
      touchedCallbacks_(std::move(other.touchedCallbacks_)),
      version_(other.version_),
      lineEnding_(other.lineEnding_),
      endsWithNewline_(other.endsWithNewline_) {
    other.version_ = 0;
    other.instanceId_ = s_nextInstanceId.fetch_add(1, std::memory_order_relaxed);
}

Document& Document::operator=(Document&& other) noexcept {
    if (this != &other) {
        lines_ = std::move(other.lines_);
        touchedCallbacks_ = std::move(other.touchedCallbacks_);
        version_ = other.version_;
        lineEnding_ = other.lineEnding_;
        endsWithNewline_ = other.endsWithNewline_;
        instanceId_ = other.instanceId_;
        other.version_ = 0;
        other.instanceId_ = s_nextInstanceId.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
}

LoadResult Document::loadFromFile(const std::string& path) {
    if (auto hook = filesystem::callLoadHook(path); hook.has_value())
        return *hook;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::error_code ec;
        bool exists = std::filesystem::exists(path, ec);
        if (ec) return LoadResult::IoError;
        if (!exists) {
            lines_.clear();
            lines_.push_back("");
            endsWithNewline_ = false;
            lineEnding_ = LineEnding::LF;
            bumpVersion();
            notifyTouched(0, 0);
            return LoadResult::NotFound;
        }
        if (errno == EACCES) return LoadResult::PermissionDenied;
        return LoadResult::IoError;
    }
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) return LoadResult::IoError;
        if (ec) return LoadResult::IoError;
    }
    bool seekable = false;
    {
        file.seekg(0, std::ios::end);
        if (file.good()) {
            auto endPos = file.tellg();
            if (endPos != static_cast<std::streampos>(-1)) {
                seekable = true;
            }
        }
        file.clear();
        file.seekg(0, std::ios::beg);
        if (!file.good()) return LoadResult::IoError;
    }
    bool hasCRLF = false;
    bool hasLF = false;
    bool hasCR = false;
    char scanLastByte = 0;
    bool hasScanByte = false;
    if (seekable) {
        constexpr size_t SCAN_CHUNK = 256 * 1024;
        std::vector<char> scanBuf(SCAN_CHUNK);
        bool prevWasCR = false;
        while (true) {
            file.read(scanBuf.data(), static_cast<std::streamsize>(SCAN_CHUNK));
            std::streamsize n = file.gcount();
            if (n <= 0) break;
            for (std::streamsize i = 0; i < n; ++i) {
                char c = scanBuf[static_cast<size_t>(i)];
                if (prevWasCR && c == '\n') hasCRLF = true;
                if (c == '\n') hasLF = true;
                if (c == '\r') hasCR = true;
                prevWasCR = (c == '\r');
            }
            scanLastByte = scanBuf[static_cast<size_t>(n - 1)];
            hasScanByte = true;
            if (!file.good() && !file.eof()) return LoadResult::IoError;
            if (file.eof()) break;
        }
        file.clear();
        file.seekg(0, std::ios::beg);
        if (!file.good()) return LoadResult::IoError;
    } else {
        hasLF = true;
    }
    // Deteccion de terminador: CRLF si existe algun "\r\n", CR solo si hay
    // '\r' sin ningun '\n' (legacy Mac), LF en cualquier otro caso. En
    // archivos mixtos el parser en modo LF solo elimina '\r' cuando precede
    // inmediatamente a '\n' (CRLF -> salto); un '\r' aislado (ej. "a\r\nb\nc\r")
    // queda como byte literal dentro de la linea, no como delimitador.
    // Asi se conserva round-trip binariamente seguro sin normalizar mixtos.
    LineEnding newLineEnding;
    if (hasCRLF) newLineEnding = LineEnding::CRLF;
    else if (hasCR && !hasLF) newLineEnding = LineEnding::CR;
    else newLineEnding = LineEnding::LF;
    bool isCRMode = (newLineEnding == LineEnding::CR);
    constexpr size_t CHUNK_SIZE = 256 * 1024;
    std::vector<char> chunk(CHUNK_SIZE);
    std::string currentLine;
    std::vector<std::string> newLines;
    bool lastStoreWasDelimiter = false;
    bool pendingCR = false;
    auto storeLine = [&](bool isDelimiter) {
        newLines.push_back(std::move(currentLine));
        currentLine.clear();
        lastStoreWasDelimiter = isDelimiter;
    };
    if (isCRMode) {
        while (true) {
            file.read(chunk.data(), static_cast<std::streamsize>(CHUNK_SIZE));
            std::streamsize bytesRead = file.gcount();
            if (bytesRead <= 0) break;
            const char* data = chunk.data();
            size_t segmentStart = 0;
            for (size_t i = 0; i < static_cast<size_t>(bytesRead); ++i) {
                if (data[i] == '\r') {
                    currentLine.append(data + segmentStart, i - segmentStart);
                    storeLine(true);
                    segmentStart = i + 1;
                }
            }
            if (segmentStart < static_cast<size_t>(bytesRead)) {
                currentLine.append(data + segmentStart, static_cast<size_t>(bytesRead) - segmentStart);
            }
            if (!file.good() && !file.eof()) return LoadResult::IoError;
        }
        if (!currentLine.empty() || newLines.empty()) {
            if (!currentLine.empty()) storeLine(false);
            else if (newLines.empty()) storeLine(false);
        }
    } else {
        while (true) {
            file.read(chunk.data(), static_cast<std::streamsize>(CHUNK_SIZE));
            std::streamsize bytesRead = file.gcount();
            size_t start = 0;
            // Contrato pendingCR: el chunk anterior termino en '\r' y ese '\r'
            // ya esta apendeado en currentLine. Aqui determinamos si era
            // CRLF partido entre chunks ("...\r" | "\n..."): si el chunk
            // actual empieza en '\n', ese '\r' era parte del delimitador y
            // se hace pop_back()+storeLine(); si no, el '\r' queda como
            // byte literal dentro de la linea (archivo mixto).
            if (pendingCR) {
                pendingCR = false;
                if (bytesRead > 0 && chunk[0] == '\n') {
                    if (!currentLine.empty() && currentLine.back() == '\r') currentLine.pop_back();
                    storeLine(true);
                    start = 1;
                }
            }
            if (bytesRead <= 0) break;
            const char* data = chunk.data();
            size_t segmentStart = start;
            bool chunkEndsWithCR = false;
            for (size_t i = start; i < static_cast<size_t>(bytesRead); ++i) {
                if (data[i] == '\n') {
                    currentLine.append(data + segmentStart, i - segmentStart);
                    if (!currentLine.empty() && currentLine.back() == '\r') currentLine.pop_back();
                    storeLine(true);
                    segmentStart = i + 1;
                }
            }
            if (segmentStart < static_cast<size_t>(bytesRead)) {
                currentLine.append(data + segmentStart, static_cast<size_t>(bytesRead) - segmentStart);
                // Solo difiere CRLF partido si el chunk se lleno y no es EOF:
                // el '\r' final queda en currentLine y pendingCR resolvera
                // en la proxima iteracion si sigue '\n'.
                if (!currentLine.empty() && currentLine.back() == '\r' && static_cast<size_t>(bytesRead) == CHUNK_SIZE && !file.eof()) {
                    chunkEndsWithCR = true;
                }
            }
            if (chunkEndsWithCR) pendingCR = true;
            if (!file.good() && !file.eof()) return LoadResult::IoError;
        }
        if (!currentLine.empty() || newLines.empty()) {
            if (!currentLine.empty()) {
                if (!currentLine.empty() && currentLine.back() == '\r') currentLine.pop_back();
                storeLine(false);
            } else if (newLines.empty()) {
                storeLine(false);
            }
        }
    }
    bool newEndsWithNewline = false;
    if (seekable && hasScanByte) {
        newEndsWithNewline = isCRMode ? (scanLastByte == '\r') : (scanLastByte == '\n');
    } else if (!seekable) {
        newEndsWithNewline = lastStoreWasDelimiter;
    }
    if (newLines.empty()) newLines.push_back("");
    lines_.swap(newLines);
    lineEnding_ = newLineEnding;
    endsWithNewline_ = newEndsWithNewline;
    normalizeEndsWithNewline();
    bumpVersion();
    notifyTouched(0, lineCount() - 1);
    return LoadResult::Success;
}

bool Document::saveToFile(const std::string& path) const {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    // Terminador segun el formato detectado (y conservado) al cargar.
    const char* term;
    switch (lineEnding_) {
        case LineEnding::CRLF: term = "\r\n"; break;
        case LineEnding::CR:   term = "\r";   break;
        default:               term = "\n";   break;
    }

    for (size_t i = 0; i < lines_.size(); ++i) {
        file << lines_[i];
        if (i + 1 < lines_.size()) file << term;
    }
    // Terminador final:
    // - endsWithNewline_ (p.ej. "abc\n" o un archivo que es solo "\n"), o
    // - linea vacia final con mas de una linea (p.ej. "abc\n\n" -> {"abc",""}).
    // Un documento de una sola linea vacia y flag en false es el archivo
    // vacio: no se escribe terminador.
    if (endsWithNewline_ || (lines_.size() > 1 && lines_.back().empty()))
        file << term;

    file.flush();
    if (!file.good()) return false;
    file.close();
    return !file.fail();
}

// Nota futura: idealmente guardar a temporal + rename atómico para evitar
// truncamiento en disco lleno; por ahora se verifica flush/close.

Document::LineEnding Document::lineEnding() const {
    return lineEnding_;
}

void Document::setLineEnding(LineEnding e) {
    lineEnding_ = e;
}

bool Document::endsWithNewline() const {
    return endsWithNewline_;
}

void Document::setEndsWithNewline(bool ends) {
    if (endsWithNewline_ == ends) return;
    endsWithNewline_ = ends;
    normalizeEndsWithNewline();
    bumpVersion();
}

void Document::normalizeEndsWithNewline() {
    // Canonical representation:
    // - a trailing empty line in lines_ represents the final newline separator;
    // - endsWithNewline_ is true only when the final newline is represented
    //   independently of a trailing empty line.
    //
    // Examples:
    //   "abc\n"    -> lines_={"abc"}, endsWithNewline_=true
    //   "abc\n\n"  -> lines_={"abc", ""}, endsWithNewline_=false
    //   "abc"      -> lines_={"abc"}, endsWithNewline_=false
    if (lines_.size() > 1 && lines_.back().empty()) {
        endsWithNewline_ = false;
    }
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
    normalizeEndsWithNewline();
    bumpVersion();
    notifyTouched(0, lineCount() - 1);
}

void Document::insertChar(int line, int col, char c) {
    if (line < 0 || line >= lineCount()) return;
    std::string& target = lines_[line];
    if (col < 0) col = 0;
    if (col > static_cast<int>(target.size())) col = static_cast<int>(target.size());
    target.insert(target.begin() + col, c);
    normalizeEndsWithNewline();
    notifyTouched(line, line);
    bumpVersion();
}

namespace {

int cellStartBefore(const std::string& s, int col) {
    return utf8::cellStartBefore(s, col);
}

int cellEndAt(const std::string& s, int col) {
    if (col >= static_cast<int>(s.size())) return static_cast<int>(s.size());
    return col + utf8::cellLen(s, col, static_cast<int>(s.size()));
}

int alignStart(const std::string& s, int col) {
    return utf8::alignStart(s, col);
}

int alignEnd(const std::string& s, int col) {
    return utf8::alignEnd(s, col);
}

} // namespace

std::string Document::cellTextBefore(int line, int col) const {
    if (line < 0 || line >= lineCount()) return "";
    const int len = lineLength(line);
    if (col <= 0 || col > len) return "";
    const std::string& target = lines_[line];
    int start = cellStartBefore(target, col);
    return target.substr(static_cast<size_t>(start),
                         static_cast<size_t>(col - start));
}

std::string Document::cellTextAt(int line, int col) const {
    if (line < 0 || line >= lineCount()) return "";
    const std::string& target = lines_[line];
    if (col < 0 || col >= static_cast<int>(target.size())) return "";
    int end = cellEndAt(target, col);
    return target.substr(static_cast<size_t>(col),
                         static_cast<size_t>(end - col));
}

Position Document::insertText(int line, int col, const std::string& text) {
    if (line < 0 || line >= lineCount() || text.empty()) return {line, col};

    // Texto multilinea: mismo modelo que insertBlock ('\n' separa lineas;
    // un '\n' final equivale a una ultima linea vacia). La insercion y la
    // posicion final resultante son responsabilidad de Document, no del
    // llamador.
    if (text.find('\n') != std::string::npos) {
        std::vector<std::string> block;
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                block.emplace_back(text.substr(start, i - start));
                start = i + 1;
            }
        }
        return insertBlock(line, col, block);
    }

    std::string& target = lines_[line];
    if (col < 0) col = 0;
    if (col > static_cast<int>(target.size())) col = static_cast<int>(target.size());
    col = alignStart(target, col);
    target.insert(col, text);
    normalizeEndsWithNewline();
    notifyTouched(line, line);
    bumpVersion();
    return {line, col + static_cast<int>(text.size())};
}

void Document::splitLine(int line, int col) {
    if (line < 0 || line >= lineCount()) return;
    std::string& target = lines_[line];
    if (col < 0) col = 0;
    if (col > static_cast<int>(target.size())) col = static_cast<int>(target.size());
    col = alignStart(target, col);

    std::string rest = target.substr(col);
    target.erase(col);
    lines_.insert(lines_.begin() + line + 1, rest);
    normalizeEndsWithNewline();
    notifyTouched(line, line + 1);
    bumpVersion();
}

bool Document::mergeLine(int line) {
    if (line < 0 || line + 1 >= lineCount()) return false;

    std::string next = std::move(lines_[line + 1]);
    lines_.erase(lines_.begin() + line + 1);
    lines_[line] += next;
    normalizeEndsWithNewline();
    notifyTouched(line, line);
    bumpVersion();
    return true;
}

int Document::deleteCharBefore(int line, int col) {
    if (line < 0 || line >= lineCount()) return 0;

    // Clamp de la columna para que erase() nunca salga de rango.
    const int len = lineLength(line);
    if (col < 0) col = 0;
    if (col > len) col = len;

    if (col > 0) {
        std::string& target = lines_[line];
        int start = cellStartBefore(target, col);
        int bytes = col - start;
        target.erase(start, static_cast<size_t>(bytes));
        notifyTouched(line, line);
        bumpVersion();
        return bytes;
    }

    // col == 0: fundir con la linea anterior, si existe.
    if (line == 0) return 0;

    std::string current = lines_[line];
    lines_.erase(lines_.begin() + line);
    lines_[line - 1] += current;
    normalizeEndsWithNewline();
    notifyTouched(line - 1, line - 1);
    bumpVersion();
    return 0;
}

int Document::deleteCharAt(int line, int col) {
    if (line < 0 || line >= lineCount()) return 0;

    // Clamp de la columna para que erase() nunca salga de rango.
    if (col < 0) col = 0;

    int len = lineLength(line);
    if(col>len) col = len;

    if (col < len) {
        std::string& target = lines_[line];
        int end = cellEndAt(target, col);
        int bytes = end - col;
        target.erase(col, static_cast<size_t>(bytes));
        notifyTouched(line, line);
        bumpVersion();
        return bytes;
    }

    // col == len: fundir con la siguiente linea, si existe.
    if (line + 1 >= lineCount()) return 0;

    mergeLine(line);
    return 0;
}

bool Document::deleteRange(int sl, int sc, int el, int ec) {
    if (sl < 0 || sl >= lineCount()) return false;
    if (el < sl || el >= lineCount()) return false;
    if (sc < 0 || sc > lineLength(sl)) return false;
    if (ec < 0 || ec > lineLength(el)) return false;

    if (sl == el && sc > ec) return false;
    if (sl == el && sc == ec) return false;

    // NUEVO: Alineación defensiva UTF-8 usando los nuevos helpers
    int start = alignStart(lines_[sl], sc);
    int end = alignEnd(lines_[el], ec);

    if (sl == el) {
        if (start > end) start = end; // Protección por si la alineación invirtiera el rango
        lines_[sl].erase(start, end - start);
        normalizeEndsWithNewline();
        notifyTouched(sl, sl);
        bumpVersion();
        return true;
    }

    std::string tail = lines_[el].substr(end);
    lines_[sl].erase(start);
    lines_[sl] += tail;
    lines_.erase(lines_.begin() + sl + 1, lines_.begin() + el + 1);
    
    normalizeEndsWithNewline();
    notifyTouched(sl, el);
    bumpVersion();
    return true;
}

/**
 * Extrae el texto en el rango [sl, sc) a [el, ec).
 * 
 * GARANTÍA DE INTEGRIDAD UTF-8:
 * Si sc o ec caen a mitad de una secuencia multibyte (ej. coordenadas crudas 
 * del mouse), los límites se expanden silenciosamente al borde de celda válido 
 * más cercano. Esto asegura que el texto devuelto sea siempre UTF-8 válido, 
 * evitando copiar bytes huérfanos al portapapeles.
 */
std::vector<std::string> Document::extractRange(int sl, int sc, int el, int ec) const {
    if (sl < 0 || sl >= lineCount()) return {};
    if (el < sl || el >= lineCount()) return {};
    if (sc < 0 || sc > lineLength(sl)) return {};
    if (ec < 0 || ec > lineLength(el)) return {};
    if (sl == el && sc >= ec) return {};

    // Normalización defensiva intencional (mismo contrato que deleteRange)
    int start = alignStart(lines_[sl], sc);
    int end = alignEnd(lines_[el], ec);

    std::vector<std::string> out;
    if (sl == el) {
        if (start < end) {
            out.push_back(lines_[sl].substr(start, end - start));
        }
    } else {
        out.push_back(lines_[sl].substr(start));
        for (int i = sl + 1; i < el; ++i) {
            out.push_back(lines_[i]);
        }
        out.push_back(lines_[el].substr(0, end));
    }
    return out;
}

Position Document::insertBlock(int line, int col, const std::vector<std::string>& block) {
    if (line < 0 || line >= lineCount() || block.empty()) return {line, col};

    std::string& target = lines_[line];
    if (col < 0) col = 0;
    if (col > static_cast<int>(target.size())) col = static_cast<int>(target.size());
    col = alignStart(target, col);

    if (block.size() == 1) {
        target.insert(col, block[0]);
        normalizeEndsWithNewline();
        notifyTouched(line, line);
        bumpVersion();
        return {line, col + static_cast<int>(block[0].size())};
    }

    // A partir de aqui block.size() >= 2; el caso de una sola linea
    // se resuelve mediante el early-return anterior.
    std::string right = target.substr(col);
    std::string left = target.substr(0, col);

    std::vector<std::string> newLines;
    newLines.reserve(block.size());
    newLines.push_back(left + block.front());
    for (size_t i = 1; i + 1 < block.size(); ++i) {
        newLines.push_back(block[i]);
    }
    newLines.push_back(block.back() + right);

    // Reserva capacidad para evitar una reallocacion durante el insert.
    // Primero insertamos las lineas adicionales y solo despues reemplazamos
    // la linea original con la primera linea resultante.
    lines_.reserve(lines_.size() + newLines.size() - 1);
    lines_.insert(lines_.begin() + line + 1, newLines.begin() + 1, newLines.end());
    lines_[line] = std::move(newLines[0]);
    normalizeEndsWithNewline();
    notifyTouched(line, line + static_cast<int>(block.size()) - 1);
    bumpVersion();
    return {line + static_cast<int>(block.size()) - 1,
            static_cast<int>(block.back().size())};
}

int Document::previewIndentDelta(int line, bool indent, int indentLen) const {
    if (line < 0 || line >= lineCount() || indentLen <= 0) return 0;
    const std::string& s = lines_[line];
    if (indent) return indentLen;
    if (!s.empty() && s[0] == '\t') return -1;
    int remove = 0;
    while (remove < indentLen && remove < static_cast<int>(s.size()) &&
           s[static_cast<size_t>(remove)] == ' ') ++remove;
    return remove == 0 ? 0 : -remove;
}

int Document::indentLine(int line, bool indent, int indentLen) {
    int delta = previewIndentDelta(line, indent, indentLen);
    if (delta == 0) return 0;
    std::string& s = lines_[line];
    if (delta > 0) {
        s.insert(0, static_cast<size_t>(delta), ' ');
    } else {
        s.erase(0, static_cast<size_t>(-delta));
    }
    notifyTouched(line, line);
    bumpVersion();
    return delta;
}
