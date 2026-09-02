#include "core/Document.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/utf8.h"

Document::Document() {
    // Un documento nunca esta "vacio del todo": siempre tiene al menos
    // una linea (posiblemente vacia). Esto simplifica muchisimo el
    // resto del codigo (cursor, renderer, etc).
    lines_.push_back("");
}

LoadResult Document::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        // Distinguir el archivo "nuevo" (no existe) de un error real. Solo en
        // el primer caso se resetea el documento a uno vacio; ante permisos
        // o E/S falla no se toca (p.ej. no aparentar que un archivo existente
        // sin permisos es un archivo nuevo, que es exactamente lo que llevaria
        // a sobrescribirlo desde cero).
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            lines_.clear();
            lines_.push_back("");
            endsWithNewline_ = false;
            lineEnding_ = LineEnding::LF; // un archivo nuevo empieza en LF
            bumpVersion();
            return LoadResult::NotFound;
        }
        if (errno == EACCES) {
            return LoadResult::PermissionDenied;
        }
        return LoadResult::IoError;
    }

    // Leemos todo el contenido para poder detectar si el archivo
    // terminaba en '\n' (el modelo de lineas, via getline, no refleja
    // esa nueva linea final y sin esto se perderia al volver a guardar).
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    endsWithNewline_ = !content.empty() && content.back() == '\n';

    // Conservar el formato de nueva linea para no alterarlo al guardar:
    // si el archivo usa \r\n (Windows) en cualquier linea, se guardara
    // como CRLF; si no, como LF.
    lineEnding_ = (content.find("\r\n") != std::string::npos)
                      ? LineEnding::CRLF
                      : LineEnding::LF;

    lines_.clear();
    std::string line;
    std::istringstream in(content);
    while (std::getline(in, line)) {
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

    bumpVersion();
    return LoadResult::Success;
}

bool Document::saveToFile(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
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
        if (i + 1 < lines_.size()) {
            file << term;
        }
    }

    // Respetar el salto de linea final del archivo original: sin esto,
    // abrir y guardar un archivo que terminaba en '\n' lo dejaria sin
    // su nueva linea final (perdida silenciosa del terminador).
    if (!lines_.empty() && endsWithNewline_) {
        file << term;
    }

    return true;
}

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
    // Invariante: un '\n' final se representa UNA vez. Si lines_ termina
    // en una linea vacia, esa linea ya serializa el '\n' (separa la ultima
    // linea de la nada), asi que el flag debe ser false. Si quedara true,
    // saveToFile escribira el '\n' del separador MAS el '\n' del flag:
    // el archivo ganaria una linea vacia al guardar.
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

// 1. cellStartBefore: Optimizado y semánticamente idéntico al original.
// Incluye la corrección crítica (conts > expect) para manejar bytes huérfanos
// que aparecen después de una secuencia UTF-8 válida completada.
int cellStartBefore(const std::string& s, int col) {
    if (col <= 0) return 0;
    int start = col - 1;
    unsigned char c = static_cast<unsigned char>(s[start]);
    
    // Si es ASCII o byte líder, ya es el inicio de la celda.
    if (c < 0x80 || (c & 0xC0) != 0x80) {
        return start;
    }
    
    // Es un byte de continuación: buscamos el byte líder hacia atrás una sola vez.
    int j = start - 1;
    while (j >= 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) {
        j--;
    }
    
    if (j < 0) return start; // Sin líder previo: es huérfano, él mismo es inicio.
    
    unsigned char lead = static_cast<unsigned char>(s[j]);
    int expect = 0;
    if ((lead & 0xE0) == 0xC0) expect = 1;
    else if ((lead & 0xF0) == 0xE0) expect = 2;
    else if ((lead & 0xF8) == 0xF0) expect = 3;
    
    int conts = start - j; // Bytes de continuación desde el líder hasta 'start'
    
    // CORRECCIÓN CLAVE: Si hay MÁS continuaciones de las esperadas, la secuencia
    // previa ya se completó. Este byte es "huérfano" y empieza su propia celda.
    if (conts > expect) {
        return start;
    }
    
    // De lo contrario, pertenece a la secuencia válida que comienza en 'j'.
    return j;
}

// 2. cellEndAt: Optimizado asumiendo el invariante del editor (col es inicio de celda).
// Es O(1) para el caso común. Si por alguna anomalía 'col' cae en un byte de 
// continuación, el fallback lo trata como una celda de 1 byte (seguro y byte-safe).
int cellEndAt(const std::string& s, int col) {
    if (col >= static_cast<int>(s.size())) return static_cast<int>(s.size());
    
    unsigned char c = static_cast<unsigned char>(s[col]);
    
    // Si es ASCII o byte de continuación, la celda termina en el siguiente byte.
    // (Bajo el invariante, si es continuación, es porque es huérfano/corrupto).
    if (c < 0x80 || (c & 0xC0) == 0x80) {
        return col + 1;
    }
    
    // Es un byte líder válido. Contamos cuántas continuaciones debe consumir.
    int expect = 0;
    if ((c & 0xE0) == 0xC0) expect = 1;
    else if ((c & 0xF0) == 0xE0) expect = 2;
    else if ((c & 0xF8) == 0xF0) expect = 3;
    
    int end = col + 1;
    while (end < static_cast<int>(s.size()) && expect > 0 &&
           (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
        ++end;
        --expect;
    }
    
    return end;
}

// NUEVO: Normaliza un offset para que apunte al inicio de la celda que lo contiene.
// Si el offset ya es un límite de celda válido (o está en los extremos), se deja intacto.
int alignStart(const std::string& s, int col) {
    if (col <= 0 || col >= static_cast<int>(s.size())) return col;
    // Si ya es el inicio de una celda (o un byte huérfano que actúa como inicio),
    // no hay nada que alinear hacia atrás.
    if (utf8::isCellStart(s, col)) return col;
    
    // Si llegamos aquí, 'col' es un byte de continuación válido.
    // El inicio de esa secuencia está en o antes de col-1.
    return cellStartBefore(s, col);
}

// NUEVO: Normaliza un offset para que apunte al final exclusivo de la celda que lo contiene.
// Dado que los rangos en C++ son [start, end), si 'col' cae en medio de una celda,
// debemos avanzar 'end' hasta el final de esa celda para no cortarla.
int alignEnd(const std::string& s, int col) {
    if (col <= 0 || col >= static_cast<int>(s.size())) return col;
    // Si 'col' ya es el inicio de una celda, significa que la celda anterior
    // ya está completa. No necesitamos avanzar 'col'.
    if (utf8::isCellStart(s, col)) return col;
    
    // 'col' es un byte de continuación. Para encontrar el final de SU celda,
    // primero debemos encontrar dónde empieza esa celda y luego calcular su longitud.
    int start = cellStartBefore(s, col);
    return cellEndAt(s, start);
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

    std::string rest = target.substr(col);
    target.erase(col);
    lines_.insert(lines_.begin() + line + 1, rest);
    normalizeEndsWithNewline();
    notifyTouched(line, line + 1);
    bumpVersion();
}

bool Document::mergeLine(int line) {
    if (line < 0 || line + 1 >= lineCount()) return false;

    std::string next = lines_[line + 1];
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

    if (block.size() == 1) {
        target.insert(col, block[0]);
        normalizeEndsWithNewline();
        notifyTouched(line, line);
        bumpVersion();
        return {line, col + static_cast<int>(block[0].size())};
    }

    // Multilinea: partimos la linea actual en col (igual que splitLine),
    // la primera linea del bloque se pega a la cola izquierda, las
    // intermedias se insertan como lineas nuevas completas, y la ultima se
    // une con la cola derecha de la linea original (lo que quedaba tras col).
    std::string right = target.substr(col);
    std::string left = target.substr(0, col);

    std::vector<std::string> newLines;
    newLines.push_back(left + block.front());
    for (size_t i = 1; i + 1 < block.size(); ++i) {
        newLines.push_back(block[i]);
    }
    newLines.push_back(block.back() + right);

    lines_.erase(lines_.begin() + line);
    lines_.insert(lines_.begin() + line, newLines.begin(), newLines.end());
    normalizeEndsWithNewline();
    notifyTouched(line, line + static_cast<int>(block.size()) - 1);
    bumpVersion();
    return {line + static_cast<int>(block.size()) - 1,
            static_cast<int>(block.back().size())};
}

int Document::indentLine(int line, bool indent, int indentLen) {
    if (line < 0 || line >= lineCount() || indentLen <= 0) return 0;
    std::string& s = lines_[line];

    if (indent) {
        s.insert(0, static_cast<size_t>(indentLen), ' ');
        notifyTouched(line, line);
        bumpVersion();
        return indentLen;
    }

    // Desindentar: quitar UN nivel de indentacion del comienzo. Un
    // tabulador INICIAL equivale a un nivel completo (se quita entero, un
    // solo caracter); si no arranca con tab, se quitan hasta `indentLen`
    // espacios iniciales, la corrida de espacios anterior a cualquier otro
    // caracter (contenido o tabulador).
    //
    // Regla para mezcla espacios+tab (ej. " \tfoo"): el criterio mira al
    // primer caracter. Si es un tab -> se quita el tab. Si es un espacio ->
    // solo se cuentan los espacios INICIALES (el recorrido se detiene al
    // primer no-espacio, que puede ser un tab), asi que en " \tfoo" se
    // quita solo el espacio y queda "\tfoo"; un segundo '{' quita el tab.
    // Nunca se "traduce" un tab a `indentLen` espacios ni se mezclan: cada
    // pulsacion desindenta exactamente un nivel a partir del primer byte.
    int remove = 0;
    if (!s.empty() && s[0] == '\t') {
        remove = 1;
    } else {
        while (remove < indentLen && remove < static_cast<int>(s.size()) &&
               s[static_cast<size_t>(remove)] == ' ') {
            ++remove;
        }
    }

    if (remove == 0) return 0;
    s.erase(0, static_cast<size_t>(remove));
    notifyTouched(line, line);
    bumpVersion();
    return -remove;
}
