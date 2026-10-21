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

// ---------------------------------------------------------------------------
// Índice de wrap: proyección visual plana con actualización incremental.
// ---------------------------------------------------------------------------
class Index {
public:
    Index() = default;

    // Reconstruye si cambió versión, instancia, número de líneas, ancho o modo wrap.
    // Retorna true si hubo reconstrucción o actualización.
    bool ensure(const Document& doc, int textWidth, bool enabled) {
        // El ancho efectivo se normaliza UNA sola vez: así ensure() compara
        // exactamente el mismo valor que almacena (evita reconstrucciones
        // repetidas con textWidth == 0).
        const int width = std::max(1, textWidth);
        const bool wantWrap = enabled && textWidth > 0;
        const bool sameParams =
            instance_ == doc.instanceId() &&
            lineCount_ == doc.lineCount() &&
            width_ == width &&
            enabled_ == wantWrap;

        if (!forceRebuild_ && sameParams && version_ == doc.version()) {
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
            lineCount_ = doc.lineCount();
            instance_ = doc.instanceId();
            version_ = doc.version();
            totalRows_ = lineCount_;
            lineMeta_.clear(); // liberar memoria
            visualRows_.clear();
            dirtyLines_.clear();
            return wasEnabled;
        }

        // Sin marcas dirty no se puede actualizar de forma incremental con
        // seguridad: cualquier cambio de versión no reportado fuerza rebuild.
        const bool fullRebuild =
            forceRebuild_ ||
            !enabled_ ||
            version_ == 0 ||
            instance_ != doc.instanceId() ||
            width_ != width ||
            lineCount_ != doc.lineCount() ||
            dirtyLines_.empty();

        enabled_ = true;
        width_ = width;
        lineCount_ = doc.lineCount();
        instance_ = doc.instanceId();
        version_ = doc.version();

        forceRebuild_ = false;
        if (fullRebuild) {
            rebuild(doc);
        } else {
            flushDirty(doc);
        }
        return true;
    }

    // Marca una línea como modificada para actualización incremental.
    void markLineDirty(int line) {
        if (enabled_ && line >= 0 && line < lineCount_) {
            dirtyLines_.push_back(line);
        }
    }

    // Limpia la marca de líneas sucias (útil tras reconstrucción completa).
    void clearDirtyLines() { dirtyLines_.clear(); }

    // Invalida el índice (fuerza reconstrucción completa en próximo ensure).
    void invalidate() {
        version_ = 0;
        forceRebuild_ = true;
    }

    // Estado
    bool enabled() const { return enabled_; }
    int textWidth() const { return width_; }
    int totalRows() const { return totalRows_; }
    int lineCount() const { return lineCount_; }
    uint64_t instanceId() const { return instance_; }

    // Acceso a filas visuales (O(1) por índice plano)
    // Para índices >= totalRows() devuelve una fila "marcador" (~).
    VisualRow rowAt(int visualRow) const {
        if (!enabled_ || visualRow < 0) {
            // Identidad: una fila visual por línea
            return {visualRow, 0, 0, 0, 0, true, true};
        }
        if (visualRow >= totalRows_) {
            // Marcador más allá del EOF
            return {lineCount_ + (visualRow - totalRows_), 0, 0, 0, 0, true, true};
        }
        return visualRows_[visualRow];
    }

    // Mapeo: posición lógica -> fila visual (O(log n) por búsqueda binaria en firstRow).
    // La afinidad define a qué fila pertenece una posición exactamente en el
    // límite entre dos filas visuales de la misma línea lógica.
    int visualRowOf(const Position& pos,
                    VisualAffinity affinity = VisualAffinity::Start) const {
        if (!enabled_ || pos.line < 0) return pos.line;
        if (pos.line >= lineCount_ || lineMeta_.empty()) return totalRows_ - 1;

        const int firstRow = lineMeta_[pos.line].firstVisualRow;
        const int rowCount = lineMeta_[pos.line].rowCount;
        const int byte = std::max(0, pos.col);
        for (int i = 0; i < rowCount; ++i) {
            const VisualRow& vr = visualRows_[firstRow + i];
            if (byte < vr.byteEnd) return firstRow + i;
            if (byte == vr.byteEnd) {
                if (i + 1 < rowCount && affinity == VisualAffinity::Start)
                    return firstRow + i + 1;
                return firstRow + i;
            }
        }
        return firstRow + rowCount - 1;
    }

    // Columna visual relativa dentro de la fila visual (0..columns-1)
    int visualColumnOf(const Document& doc, const Position& pos,
                       VisualAffinity affinity = VisualAffinity::Start) const {
        if (!enabled_) return utf8::columnOf(doc.lineAt(pos.line), pos.col);
        int vr = visualRowOf(pos, affinity);
        VisualRow row = rowAt(vr);
        int absCol = utf8::columnOf(doc.lineAt(pos.line), pos.col);
        int rel = absCol - row.columnBegin;
        if (rel < 0) rel = 0;
        int width = row.columnEnd - row.columnBegin;
        if (rel > width) rel = width;
        return rel;
    }

    // Posición lógica desde fila visual + columna relativa en esa fila
    Position positionAt(const Document& doc, int visualRow, int colInRow) const {
        VisualRow vr = rowAt(visualRow);
        if (vr.docLine >= lineCount_) {
            // Marker row beyond EOF
            return {lineCount_ - 1, 0};
        }
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

    int firstRowOfLine(int docLine) const {
        if (!enabled_ || docLine < 0) return docLine;
        if (docLine >= lineCount_) return totalRows_;
        return lineMeta_[docLine].firstVisualRow;
    }

    int rowsInLine(int docLine) const {
        if (!enabled_ || docLine < 0 || docLine >= lineCount_) return 0;
        return lineMeta_[docLine].rowCount;
    }

private:
    struct LineMeta {
        int firstVisualRow = 0;
        int rowCount = 0;
    };

    // Construye las filas visuales de UNA línea lógica. No toca el estado del
    // índice: lo usan tanto rebuild() como updateLine().
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

    void rebuild(const Document& doc) {
        visualRows_.clear();
        lineMeta_.clear();
        lineMeta_.reserve(doc.lineCount());
        dirtyLines_.clear();

        int total = 0;
        for (int l = 0; l < doc.lineCount(); ++l) {
            std::vector<VisualRow> rows = makeRowsForLine(l, doc.lineAt(l));
            LineMeta lm;
            lm.firstVisualRow = total;
            lm.rowCount = static_cast<int>(rows.size());
            total += lm.rowCount;
            visualRows_.insert(visualRows_.end(), rows.begin(), rows.end());
            lineMeta_.push_back(lm);
        }
        totalRows_ = total;
    }

    // Recalcula una sola línea. Si el número de filas no cambia, reemplaza
    // las filas en su lugar sin tocar los metadatos posteriores. Si cambia,
    // mueve la cola UNA sola vez (evita erase+insert y su doble desplazamiento).
    void updateLine(const Document& doc, int lineIdx) {
        if (lineIdx < 0 || lineIdx >= static_cast<int>(lineMeta_.size()))
            return;

        std::vector<VisualRow> rows = makeRowsForLine(lineIdx, doc.lineAt(lineIdx));
        const int oldCount = lineMeta_[lineIdx].rowCount;
        const int first = lineMeta_[lineIdx].firstVisualRow;
        const int newCount = static_cast<int>(rows.size());
        const int delta = newCount - oldCount;

        if (delta == 0) {
            std::copy(rows.begin(), rows.end(),
                      visualRows_.begin() + first);
            return; // mismo número de filas: metadatos y totalRows intactos
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

        lineMeta_[lineIdx].rowCount = newCount;
        for (int l = lineIdx + 1; l < static_cast<int>(lineMeta_.size()); ++l)
            lineMeta_[l].firstVisualRow += delta;
        totalRows_ += delta;
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
    int totalRows_ = 0;

    // Almacenamiento plano: todas las filas visuales consecutivas
    std::vector<VisualRow> visualRows_;
    std::vector<LineMeta> lineMeta_;
    std::vector<int> dirtyLines_;
};

} // namespace softwrap
