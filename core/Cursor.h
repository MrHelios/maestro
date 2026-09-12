#pragma once

#include <cassert>
#include <string_view>

#include "core/Document.h"
#include "core/utf8.h"

class Cursor {
public:
    int line = 0;
    int col = 0;

    void moveLeft(const Document& doc);
    void moveRight(const Document& doc);
    void moveUp(const Document& doc);
    void moveDown(const Document& doc);

    void moveHome();
    void moveEnd(const Document& doc);

    void moveToNextWord(const Document& doc);
    void moveToPreviousWord(const Document& doc);

    void clampToLine(const Document& doc);

    int visualColumn(std::string_view line) const {
        return columnOfCached(line, col, columnCache_);
    }
    int visualColumn(const Document& doc) const {
        if (line < 0 || line >= doc.lineCount()) return 0;
        return visualColumn(doc.lineAt(line));
    }
    void invalidateColumnCache() const { columnCache_.invalidate(); }

private:
    struct ColumnCache {
        const char* data = nullptr;
        int size = -1;
        int byteCol = -1;
        int col = -1;
        void invalidate() { data = nullptr; byteCol = -1; col = -1; size = -1; }
    };
    // Contrato frágil: columnOfCached considera la cache válida solo si
    // data==line.data() && size==line.size() && byteCol en rango. Una mutación
    // in-place que no cambia data/size (reemplazar ASCII por multibyte en el
    // mismo std::string sin realloc, ej. line[5]=0xC3,line[6]=0xA9) deja
    // data/size iguales pero el contenido/columna cambia → falso hit si no se
    // invalida. Por eso toda mutación al contenido de la línea cacheada debe
    // llamar cursor.invalidateColumnCache(), incluso si no cambia data/size.
    // En Maestro esto se garantiza vía Buffer::rebindCallback →
    // document.setTouchedCallback → cursor.invalidateColumnCache() en cada
    // notifyTouched. Si aparece un path que mute Document sin pasar por
    // notifyTouched, habrá bug silencioso de columna visual incorrecta.
    static inline int columnOfCached(std::string_view line, int byteCol, ColumnCache& cache) {
        int n = static_cast<int>(line.size());
        int limit = byteCol < n ? byteCol : n;
        // Precondición: el byte offset efectivo (clamped a [0, size]) está
        // alineado al inicio de una celda (isCellStart || 0 || size). Cursor
        // siempre mantiene posiciones alineadas; columnOf() acepta offsets
        // arbitrarios pero el camino inverso cellStartBefore(--col) solo es
        // correcto para offsets alineados.
        assert(limit == 0 || limit == n || utf8::isCellStart(line, limit));
        if (limit <= 0) { cache.data = line.data(); cache.size = n; cache.byteCol = 0; cache.col = 0; return 0; }
        if (cache.data == line.data() && cache.size == n && cache.byteCol >= 0 && cache.byteCol <= n) {
            if (limit == cache.byteCol) return cache.col;
            if (limit > cache.byteCol) {
                int distance = limit - cache.byteCol;
                if (distance < limit) {
                    int col = cache.col;
                    int i = cache.byteCol;
                    while (i < limit) { ++col; i += utf8::cellLen(line, i, n); }
                    cache.byteCol = limit; cache.col = col;
                    return col;
                }
            }
            if (limit < cache.byteCol && cache.byteCol - limit < limit) {
                int col = cache.col;
                int i = cache.byteCol;
                while (i > limit) { i = utf8::cellStartBefore(line, i); --col; }
                cache.byteCol = limit; cache.col = col;
                return col;
            }
        }
        int col = utf8::columnOf(line, limit);
        cache.data = line.data(); cache.size = n; cache.byteCol = limit; cache.col = col;
        return col;
    }

    int preferredCol_ = 0;
    mutable ColumnCache columnCache_;

    void applyPreferredCol(const Document& doc);
};
