#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include "document/Document.h"
#include "document/Position.h"
#include "base/utf8.h"

namespace softwrap {

// ---------------------------------------------------------------------------
// Proyección visual: una línea lógica puede ocupar varias filas visuales.
// ---------------------------------------------------------------------------
struct VisualRow {
    int docLine = 0;            // línea del documento (0-based)
    int byteBegin = 0;          // byte inicial en la línea (inclusive)
    int byteEnd = 0;            // byte final en la línea (exclusive)
    int columnBegin = 0;        // columna visual absoluta en la línea (inclusive)
    int columnEnd = 0;          // columna visual absoluta en la línea (exclusive)
    bool firstOfLine = true;    // primera fila visual de la línea lógica
    bool lastOfLine = true;     // última fila visual de la línea lógica
};

// Afinidad visual en los límites del wrap. Una misma Position lógica puede
// representar el final de una fila visual y el comienzo de la siguiente.
//   Start: en un límite pertenece a la fila siguiente (cursor que avanza).
//   End:   en un límite pertenece a la fila anterior (cursor al final).
enum class VisualAffinity { Start, End };

inline bool isMarkdownFilename(const std::string& filename) {
    if (filename.empty()) return false;
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = filename.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".md";
}

// Soft-wrap habilitado para un filename: Markdown siempre, y cualquier
// buffer SIN extension (o sin nombre: p.ej. "SinNombre1", el buffer nuevo,
// o un archivo sin sufijo). Se extiende el wrap a esos buffers para que el
// usuario vea las filas completas sin scroll horizontal, igual que en .md.
// Un filename con extension conocida y NO markdown (ej. .txt, .cpp) no usa
// soft-wrap (modo identidad).
inline bool isWrapEnabledForFilename(const std::string& filename) {
    if (isMarkdownFilename(filename)) return true;
    // Sin "." => sin extension (o filename vacio: buffer sin nombre).
    if (filename.find_last_of('.') == std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Índice de wrap ACOTADO A UNA VENTANA de líneas lógicas.
//
// En vez de proyectar el documento completo, el índice mantiene las filas
// visuales de [firstLine(), endLine()) — la ventana visible más un margen de
// líneas. Las posiciones visuales que expone (rowAt/visualRowOf/firstRowOfLine)
// son RELATIVAS A LA VENTANA, no filas globales del documento: un índice
// acotado no puede conocer el total de filas visuales sin recorrer todo el
// documento, y no debe aparentar que lo hace.
//
// `viewport.top` en modo wrap es la primera LÍNEA LÓGICA visible. La fila
// visual 0 de la ventana es la primera fila visual de firstLine(); para
// renderizar desde `top` se usa firstRowOfLine(top) como base y se suman las
// filas de la ventana.
// ---------------------------------------------------------------------------
class Index {
public:
    // Líneas lógicas extra que se conservan a cada lado de la ventana visible.
    static constexpr int kMarginLines = 5;

    Index() = default;

    // Asegura que la ventana cubra la vista que empieza en `topLine` (línea
    // lógica) y ocupa `height` filas visuales, más el margen.
    //
    // Distingue tres casos:
    //   (a) sin cambios              -> no hace nada, devuelve false.
    //   (b) edición dentro de ventana -> recalcula solo las líneas sucias.
    //   (c) cambio estructural, de ancho/instancia o ventana distinta ->
    //       reconstruye SOLO la ventana (nunca el documento completo).
    bool ensure(const Document& doc, int textWidth, bool enabled, int topLine,
                int height) {
        // El ancho efectivo se normaliza UNA sola vez: así ensure() compara
        // exactamente el mismo valor que almacena (evita reconstrucciones
        // repetidas con textWidth == 0).
        const int width = std::max(1, textWidth);
        const bool wantWrap = enabled && textWidth > 0;
        const int count = std::max(0, doc.lineCount());
        const int clampedTop =
            count > 0 ? std::clamp(topLine, 0, count - 1) : 0;
        const int h = std::max(1, height);

        const bool sameParams =
            instance_ == doc.instanceId() && lineCount_ == count &&
            width_ == width && enabled_ == wantWrap &&
            topLine_ == clampedTop && height_ == h;

        if (!forceRebuild_ && built_ && sameParams &&
            version_ == doc.version()) {
            if (enabled_ && !dirtyLines_.empty()) {
                flushDirty(doc);
                return true;
            }
            return false; // sin cambios
        }

        const bool wasEnabled = enabled_;

        if (!wantWrap) {
            // Modo identidad: no construir índice, usar mapeo directo.
            enabled_ = false;
            width_ = width;
            lineCount_ = count;
            instance_ = doc.instanceId();
            version_ = doc.version();
            topLine_ = clampedTop;
            height_ = h;
            firstLine_ = clampedTop;
            endLine_ = std::min(count, clampedTop + 1);
            visualRows_.clear(); // liberar memoria
            lineMeta_.clear();
            dirtyLines_.clear();
            built_ = true;
            forceRebuild_ = false;
            return wasEnabled;
        }

        // Ventana deseada: la vista completa (height filas => a lo sumo
        // height líneas, porque cada línea aporta >= 1 fila) más margen.
        // Si el documento cabe entero en la altura (count <= h), la ventana
        // debe cubrir todo el documento desde la línea 0, no una ventana
        // parcial desplazada por top. Así maxTop() puede decidir si hay
        // scroll posible.
        const int wantFirst =
            (count <= h) ? 0 : std::max(0, clampedTop - kMarginLines);
        const int wantEnd = std::min(
            count, clampedTop + h + kMarginLines + 1);

        // Sin marcas dirty no se puede actualizar de forma incremental con
        // seguridad: cualquier cambio de versión no reportado fuerza rebuild.
        const bool fullRebuild =
            forceRebuild_ || !built_ || !enabled_ || version_ == 0 ||
            instance_ != doc.instanceId() || width_ != width ||
            lineCount_ != count || firstLine_ != wantFirst ||
            endLine_ != wantEnd ||
            (version_ != doc.version() && dirtyLines_.empty());

        enabled_ = true;
        width_ = width;
        lineCount_ = count;
        instance_ = doc.instanceId();
        version_ = doc.version();
        topLine_ = clampedTop;
        height_ = h;

        forceRebuild_ = false;
        built_ = true;
        if (fullRebuild) {
            rebuildWindow(doc, wantFirst, wantEnd);
        } else {
            flushDirty(doc);
        }
        return true;
    }

    // Marca una línea como modificada para actualización incremental. Solo
    // tiene efecto si la línea pertenece a la ventana actual: fuera de ella
    // el próximo ensure() reconstruirá la ventana cuando haga falta.
    void markLineDirty(int line) {
        if (enabled_ && coversLine(line)) {
            dirtyLines_.push_back(line);
        }
    }

    // Limpia la marca de líneas sucias (útil tras reconstrucción completa).
    void clearDirtyLines() { dirtyLines_.clear(); }

    // Invalida el índice (fuerza reconstrucción de la ventana en próximo ensure).
    void invalidate() {
        version_ = 0;
        forceRebuild_ = true;
    }

    // Estado
    bool enabled() const { return enabled_; }
    int textWidth() const { return width_; }
    int lineCount() const { return lineCount_; }
    uint64_t instanceId() const { return instance_; }

    // Ventana de líneas lógicas cubierta por el índice: [firstLine, endLine).
    int firstLine() const { return firstLine_; }
    int endLine() const { return endLine_; }
    bool coversLine(int line) const {
        return line >= firstLine_ && line < endLine_;
    }

    // Total de filas visuales EN LA VENTANA (no del documento).
    int rowCount() const {
        return static_cast<int>(visualRows_.size());
    }

    // Número de líneas lógicas que caben en `height` filas visuales a partir
    // de `topLine` y `topOffset`. Para la primera línea solo se cuentan las
    // filas restantes desde el offset (rowsInLine - topOffset); las demás
    // líneas cuentan todas sus filas. Acotado: recorre como máximo `height`
    // líneas (cada una aporta >= 1 fila). Se usa para el scrollbar y los
    // clamps de scroll, donde la unidad es la línea lógica.
    int visibleLines(int topLine, int height, int topOffset = 0) const {
        if (height <= 0) return 1;
        if (!enabled_) return height;
        const int count = lineCount_;
        if (count <= 0) return 1;
        const int top = std::clamp(topLine, 0, count - 1);
        int rows = 0;
        int lines = 0;
        for (int l = top; l < count && rows < height; ++l) {
            int lineRows = (l == top)
                ? std::max(0, rowsInLine(l) - topOffset)
                : rowsInLineOrCompute(l);
            rows += lineRows;
            ++lines;
        }
        return std::max(1, lines);
    }

    // Máximo `top` lógico para que el final del documento siga alcanzable.
    // Solo podemos saber si el documento cabe completo cuando la ventana
    // cubre todas las líneas (firstLine_ == 0 && endLine_ == lineCount_):
    // entonces visibleLines(0, height) es exacta. Fuera de la ventana no
    // conocemos las filas reales de cada línea, así que asumimos
    // conservadoramente que hay scroll posible (top hasta lineCount - 1).
    int maxTop(int topLine, int height) const {
        (void)topLine;  // se conserva el parámetro por firma.
        if (lineCount_ <= 0) return 0;
        if (firstLine_ == 0 && endLine_ == lineCount_ &&
            visibleLines(0, height) >= lineCount_)
            return 0;
        return std::max(0, lineCount_ - 1);
    }

    // Acceso a filas visuales de la ventana (O(1)).
    // Para índices >= rowCount() devuelve una fila "marcador" (~).
    VisualRow rowAt(int windowRow) const {
        if (!enabled_) {
            // Identidad: una fila visual por línea
            return {windowRow, 0, 0, 0, 0, true, true};
        }
        if (windowRow >= 0 && windowRow < rowCount())
            return visualRows_[static_cast<size_t>(windowRow)];
        if (windowRow >= rowCount()) {
            // Marcador más allá del EOF (solo válido si la ventana llega al
            // final del documento; ensure() garantiza filas suficientes).
            const int extra = windowRow - rowCount();
            return {lineCount_ + extra, 0, 0, 0, 0, true, true};
        }
        // Fila anterior a la ventana: no debería consultarse.
        return {firstLine_ + windowRow, 0, 0, 0, 0, true, true};
    }

    // Mapeo: posición lógica -> fila visual RELATIVA A LA VENTANA.
    // Devuelve -1 si la línea no está cubierta por la ventana.
    // La afinidad define a qué fila pertenece una posición exactamente en el
    // límite entre dos filas visuales de la misma línea lógica.
    int visualRowOf(const Position& pos,
                    VisualAffinity affinity = VisualAffinity::Start) const {
        if (!enabled_ || pos.line < 0) return pos.line;
        if (!coversLine(pos.line) || lineMeta_.empty()) return -1;

        const LineMeta& lm = lineMeta_[static_cast<size_t>(pos.line - firstLine_)];
        const int firstRow = lm.firstVisualRow;
        const int rowCount = lm.rowCount;
        const int byte = std::max(0, pos.col);
        for (int i = 0; i < rowCount; ++i) {
            const VisualRow& vr = visualRows_[static_cast<size_t>(firstRow + i)];
            if (byte < vr.byteEnd) return firstRow + i;
            if (byte == vr.byteEnd) {
                if (i + 1 < rowCount && affinity == VisualAffinity::Start)
                    return firstRow + i + 1;
                return firstRow + i;
            }
        }
        return firstRow + rowCount - 1;
    }

    // Columna visual relativa dentro de la fila visual (0..columns-1).
    // Funciona para cualquier línea: si no está en la ventana, calcula sus
    // filas al vuelo (uso de navegación, no de render).
    int visualColumnOf(const Document& doc, const Position& pos,
                       VisualAffinity affinity = VisualAffinity::Start) const {
        if (!enabled_) return utf8::columnOf(doc.lineAt(pos.line), pos.col);
        const VisualRow* row = nullptr;
        std::vector<VisualRow> tmp;
        if (coversLine(pos.line)) {
            int vr = visualRowOf(pos, affinity);
            if (vr >= 0) row = &visualRows_[static_cast<size_t>(vr)];
        }
        if (!row) {
            tmp = makeRowsForLine(pos.line, doc.lineAt(pos.line));
            if (tmp.empty()) return 0;
            int idx = rowIndexForByte(tmp, std::max(0, pos.col), affinity);
            row = &tmp[static_cast<size_t>(idx)];
        }
        int absCol = utf8::columnOf(doc.lineAt(pos.line), pos.col);
        int rel = absCol - row->columnBegin;
        if (rel < 0) rel = 0;
        int width = row->columnEnd - row->columnBegin;
        if (rel > width) rel = width;
        return rel;
    }

    // Posición lógica desde fila visual (relativa a la ventana) + columna
    // relativa en esa fila.
    Position positionAt(const Document& doc, int windowRow, int colInRow) const {
        if (!enabled_) {
            const int line = std::clamp(windowRow, 0,
                                        std::max(0, lineCount_ - 1));
            const std::string& s = doc.lineAt(line);
            int byte = utf8::byteForColumn(s, std::max(0, colInRow));
            byte = utf8::alignStart(s, byte);
            byte = std::clamp(byte, 0, static_cast<int>(s.size()));
            return {line, byte};
        }
        VisualRow vr = rowAt(windowRow);
        if (vr.docLine >= lineCount_) {
            // Marker row beyond EOF
            return {std::max(0, lineCount_ - 1), 0};
        }
        return positionInVisualRow(doc, vr, colInRow);
    }

    // Posición en la PRIMERA fila visual de `line`, en la columna relativa
    // pedida. No requiere que `line` esté en la ventana: si no lo está,
    // calcula sus filas al vuelo. Es el mapeo que usan las navegaciones
    // verticales (Arriba/Abajo/Repág/AvPág), que siempre aterrizan en el
    // comienzo de una línea lógica.
    Position positionAtFirstRow(const Document& doc, int line, int colInRow) const {
        const int count = doc.lineCount();
        if (count <= 0) return {0, 0};
        line = std::clamp(line, 0, count - 1);
        const VisualRow* row = nullptr;
        std::vector<VisualRow> tmp;
        if (enabled_ && coversLine(line)) {
            int first = firstRowOfLine(line);
            if (first >= 0 && first < rowCount())
                row = &visualRows_[static_cast<size_t>(first)];
        }
        if (!row) {
            tmp = makeRowsForLine(line, doc.lineAt(line));
            if (tmp.empty()) return {line, 0};
            row = &tmp.front();
        }
        return positionInVisualRow(doc, *row, colInRow);
    }

    // Primera fila visual de `docLine` RELATIVA A LA VENTANA.
    // Devuelve -1 si la línea no está cubierta.
    int firstRowOfLine(int docLine) const {
        if (!enabled_) return docLine;
        if (!coversLine(docLine)) return -1;
        return lineMeta_[static_cast<size_t>(docLine - firstLine_)].firstVisualRow;
    }

    // Filas visuales de `docLine` (0 si no está cubierta por la ventana).
    int rowsInLine(int docLine) const {
        if (!enabled_) return 0;
        if (!coversLine(docLine)) return 0;
        return lineMeta_[static_cast<size_t>(docLine - firstLine_)].rowCount;
    }

    // Primera fila visual del viewport: firstRowOfLine(topLine) + topOffset.
    // Devuelve -1 si la línea no está cubierta.
    int firstRowOfViewport(int topLine, int topOffset) const {
        if (!enabled_) return topLine;
        if (!coversLine(topLine)) return -1;
        int first = firstRowOfLine(topLine);
        if (first < 0) return -1;
        return first + topOffset;
    }

    // Clampea topOffset al rango válido [0, rowsInLine(topLine) - 1].
    int clampTopOffset(int topLine, int topOffset) const {
        if (!enabled_) return 0;
        if (!coversLine(topLine)) return 0;
        int rows = rowsInLine(topLine);
        if (rows <= 0) return 0;
        return std::clamp(topOffset, 0, rows - 1);
    }

private:
    struct LineMeta {
        int firstVisualRow = 0; // relativa a la ventana
        int rowCount = 0;
    };

    // Construye las filas visuales de UNA línea lógica. No toca el estado del
    // índice: lo usan rebuildWindow(), updateLine() y los cálculos al vuelo.
    std::vector<VisualRow> makeRowsForLine(int lineIdx,
                                           const std::string& line) const {
        std::vector<VisualRow> rows;
        const int n = static_cast<int>(line.size());
        if (n == 0) {
            rows.push_back(VisualRow{lineIdx, 0, 0, 0, 0, true, true});
            return rows;
        }

        int start = 0;
        int startCol = 0;
        bool first = true;

        while (start < n) {
            const int limitCol = startCol + width_;
            int i = start;
            int col = startCol;
            int lastBreakByte = -1;
            int lastBreakCol = -1;

            while (i < n) {
                int w = utf8::cellWidth(line, i, n);
                if (line[i] == '\t') {
                    w = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - col;
                }
                if (col + w > limitCol) break;

                col += w;
                int next = i + utf8::cellLen(line, i, n);

                if (line[i] == ' ' || line[i] == '\t') {
                    lastBreakByte = next;
                    lastBreakCol = col;
                }
                i = next;
            }

            int endByte, endCol;
            if (i >= n) {
                endByte = n;
                endCol = col;
            } else if (lastBreakByte > start) {
                endByte = lastBreakByte;
                endCol = lastBreakCol;
            } else {
                if (i == start) {
                    int w = utf8::cellWidth(line, i, n);
                    if (line[i] == '\t') {
                        w = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - col;
                    }
                    endByte = i + utf8::cellLen(line, i, n);
                    endCol = col + w;
                } else {
                    endByte = i;
                    endCol = col;
                }
            }

            const bool last = (endByte >= n);
            rows.push_back(VisualRow{lineIdx, start, endByte, startCol, endCol,
                                     first, last});
            start = endByte;
            startCol = endCol;
            first = false;
        }
        return rows;
    }

    static int rowIndexForByte(const std::vector<VisualRow>& rows, int byte,
                               VisualAffinity affinity) {
        if (rows.empty()) return 0;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const VisualRow& vr = rows[static_cast<size_t>(i)];
            if (byte < vr.byteEnd) return i;
            if (byte == vr.byteEnd) {
                if (i + 1 < static_cast<int>(rows.size()) &&
                    affinity == VisualAffinity::Start)
                    return i + 1;
                return i;
            }
        }
        return static_cast<int>(rows.size()) - 1;
    }

    static Position positionInVisualRow(const Document& doc,
                                        const VisualRow& vr, int colInRow) {
        const std::string& line = doc.lineAt(vr.docLine);
        int targetAbs = vr.columnBegin + colInRow;
        if (targetAbs <= vr.columnBegin) return {vr.docLine, vr.byteBegin};
        if (targetAbs >= vr.columnEnd) {
            // Fin de la línea lógica: el cursor puede quedar en la posición
            // exclusiva (lineLength), igual que sin soft-wrap.
            if (vr.lastOfLine) return {vr.docLine, vr.byteEnd};
            // Fin de una fila visual intermedia: no cruzar el límite, quedar
            // en la última posición válida de esta fila.
            int lastAbs = vr.columnEnd - 1;
            int byte = utf8::byteForColumn(line, lastAbs);
            byte = utf8::alignStart(line, byte);
            if (byte > vr.byteEnd) byte = vr.byteEnd;
            return {vr.docLine, byte};
        }
        int byte = utf8::byteForColumn(line, targetAbs);
        byte = utf8::alignStart(line, byte);
        if (byte < vr.byteBegin) byte = vr.byteBegin;
        if (byte > vr.byteEnd) byte = vr.byteEnd;
        return {vr.docLine, byte};
    }

    int rowsInLineOrCompute(int line) const {
        if (line < 0 || line >= lineCount_) return 0;
        if (coversLine(line))
            return lineMeta_[static_cast<size_t>(line - firstLine_)].rowCount;
        // Fuera de la ventana: mínimo seguro (toda línea aporta >= 1 fila).
        // visibleLines() solo recorre líneas dentro de la ventana, así que
        // este camino es defensivo.
        return 1;
    }

    // Reconstruye SOLO la ventana [first, end) de líneas lógicas.
    void rebuildWindow(const Document& doc, int first, int end) {
        first = std::max(0, first);
        end = std::min(doc.lineCount(), std::max(first, end));
        visualRows_.clear();
        lineMeta_.clear();
        dirtyLines_.clear();
        firstLine_ = first;
        endLine_ = end;
        lineMeta_.reserve(static_cast<size_t>(end - first));

        int total = 0;
        for (int l = first; l < end; ++l) {
            std::vector<VisualRow> rows = makeRowsForLine(l, doc.lineAt(l));
            LineMeta lm;
            lm.firstVisualRow = total;
            lm.rowCount = static_cast<int>(rows.size());
            total += lm.rowCount;
            visualRows_.insert(visualRows_.end(), rows.begin(), rows.end());
            lineMeta_.push_back(lm);
        }
    }

    // Recalcula una sola línea de la ventana. Si el número de filas no
    // cambia, reemplaza las filas en su lugar sin tocar los metadatos
    // posteriores. Si cambia, mueve la cola UNA sola vez.
    void updateLine(const Document& doc, int lineIdx) {
        if (!coversLine(lineIdx)) return;
        const int wi = lineIdx - firstLine_;
        if (wi < 0 || wi >= static_cast<int>(lineMeta_.size())) return;

        std::vector<VisualRow> rows = makeRowsForLine(lineIdx, doc.lineAt(lineIdx));
        const int oldCount = lineMeta_[static_cast<size_t>(wi)].rowCount;
        const int first = lineMeta_[static_cast<size_t>(wi)].firstVisualRow;
        const int newCount = static_cast<int>(rows.size());
        const int delta = newCount - oldCount;

        if (delta == 0) {
            std::copy(rows.begin(), rows.end(),
                      visualRows_.begin() + first);
            return; // mismo número de filas: metadatos intactos
        }

        const size_t oldSize = visualRows_.size();
        if (delta > 0) {
            visualRows_.resize(oldSize + static_cast<size_t>(delta));
            std::move_backward(visualRows_.begin() + first + oldCount,
                               visualRows_.begin() + oldSize,
                               visualRows_.end());
        } else {
            std::move(visualRows_.begin() + first + oldCount,
                      visualRows_.end(),
                      visualRows_.begin() + first + newCount);
            visualRows_.resize(oldSize + static_cast<size_t>(delta));
        }
        std::copy(rows.begin(), rows.end(), visualRows_.begin() + first);

        lineMeta_[static_cast<size_t>(wi)].rowCount = newCount;
        for (int l = wi + 1; l < static_cast<int>(lineMeta_.size()); ++l)
            lineMeta_[static_cast<size_t>(l)].firstVisualRow += delta;
    }

    void flushDirty(const Document& doc) {
        if (dirtyLines_.empty()) return;

        // Varias ediciones pueden marcar la misma línea antes del próximo
        // render. Ordenar + unique evita recalcularla varias veces.
        std::sort(dirtyLines_.begin(), dirtyLines_.end());
        dirtyLines_.erase(std::unique(dirtyLines_.begin(), dirtyLines_.end()),
                          dirtyLines_.end());

        for (int dirtyLine : dirtyLines_) {
            if (dirtyLine >= 0 && dirtyLine < doc.lineCount())
                updateLine(doc, dirtyLine);
        }
        dirtyLines_.clear();
    }

    // Cache
    uint64_t instance_ = 0;
    uint64_t version_ = 0;
    int lineCount_ = 0;
    int width_ = 0;
    bool enabled_ = false;
    bool forceRebuild_ = false;
    bool built_ = false;

    // Ventana de líneas lógicas [firstLine_, endLine_) y parámetros de la
    // vista con los que se construyó.
    int firstLine_ = 0;
    int endLine_ = 0;
    int topLine_ = 0;
    int height_ = 0;

    // Almacenamiento plano RELATIVO A LA VENTANA.
    std::vector<VisualRow> visualRows_;
    std::vector<LineMeta> lineMeta_;
    std::vector<int> dirtyLines_;
};

} // namespace softwrap
