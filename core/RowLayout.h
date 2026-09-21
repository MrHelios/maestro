#pragma once
// RowLayout minimo - dos estrategias para evitar multiplicacion O(n) en renderEditorRow
// Ver Instrument.h para hipotesis: buildScreen -> renderEditorRow -> utf8::columnOf/range/truncate
//
// Estrategia 1: tabla completa (Full) - un unico scan construye mapping byte<->col O(n),
//               luego columnAt/range O(1) (array) o O(log n) (binary search).
// Estrategia 2: checkpoints incrementales - scan parcial desde checkpoint mas cercano,
//               memoria O(n / K), query O(K) con K ~ 512-1024 bytes.

#include <string_view>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include "core/utf8.h"

namespace rowlayout {

// ---------------------------------------------------------------------------
// Estrategia 1: tabla completa
// ---------------------------------------------------------------------------
class RowLayoutFull {
public:
    explicit RowLayoutFull(std::string_view line) : line_(line) {
        int n = static_cast<int>(line.size());
        // byte -> col : size n+1, col de la celda que contiene byte
        byteToCol_.resize(n + 1);
        // Para col -> byte necesitamos totalCols.
        // Primero escaneamos para calcular totalCols y tambien construir cells
        // para luego rellenar colToByte.
        int col = 0;
        int i = 0;
        // estimar totalCols upper bound: cada byte max 1 col + tabs expanden,
        // en peor caso n*4 (si todo tabs). Reservamos lazy.
        std::vector<std::pair<int,int>> cells; // (bytePos, col)
        cells.reserve(n); // over-estimate for ASCII
        while (i < n) {
            cells.emplace_back(i, col);
            int w = utf8::cellWidth(line_, i, n);
            if (line_[i] == '\t') w = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - col;
            int len = utf8::cellLen(line_, i, n);
            // byteToCol para bytes dentro de la celda
            for (int b = i; b < i + len && b <= n; ++b) {
                byteToCol_[b] = col;
            }
            col += w;
            i += len;
        }
        byteToCol_[n] = col;
        totalCols_ = col;
        // col -> byte : size totalCols+1
        colToByte_.assign(totalCols_ + 1, n);
        // Rellenar colToByte: para cada celda, sus w columnas mapean al byte start
        col = 0; i = 0;
        // Re-scan para rellenar colToByte usando cells vector
        for (auto &c : cells) {
            int b = c.first;
            int ccol = c.second;
            int w = utf8::cellWidth(line_, b, n);
            if (line_[b] == '\t') w = ((ccol / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - ccol;
            for (int cc = ccol; cc < ccol + w && cc <= totalCols_; ++cc) {
                colToByte_[cc] = b;
            }
        }
        // col totalCols -> n
        if (totalCols_ >= 0 && totalCols_ < (int)colToByte_.size())
            colToByte_[totalCols_] = n;
        // Para bytes entre cells (continuaciones) ya estan mapeados via loop byteToCol
        // Asegurar que colToByte esta completo: cualquier hueco (no deberia) mapear al proximo cell
        // Ya esta cubierto porque cada col pertenece a exactamente una celda.
    }

    int columnAt(int byte) const {
        // byte puede ser no alineado (dentro de celda). Devolvemos col de la celda que lo contiene,
        // igual que utf8::columnOf con alignStart implicito? utf8::columnOf cuenta hasta byte,
        // pero si byte cae dentro de celda, cuenta hasta limite sin incluir celda parcial?
        // En Renderer se usa columnOf(line,p.first) donde p.first es cell start, asi que no cae dentro.
        // Para seguridad, si byte no es cell start, devolvemos col del cell start.
        if (byte <= 0) return 0;
        int n = static_cast<int>(line_.size());
        if (byte >= n) return totalCols_;
        // si byte cae dentro de celda multibyte, byteToCol[byte] ya es col del cell start (por llenado)
        return byteToCol_[byte];
    }

    std::string_view range(int fromCol, int toCol) const {
        if (toCol <= fromCol) return {};
        if (fromCol >= totalCols_) return {};
        if (fromCol < 0) fromCol = 0;
        if (toCol > totalCols_) toCol = totalCols_;
        int startByte = colToByte_[fromCol];
        int endByte = colToByte_[toCol];
        // Si fromCol/toCol cae dentro de tab/wide, colToByte ya apunta al cell start, correcto.
        // Pero necesitamos asegurar que endByte es byte donde col == toCol, no col+ w.
        // Nuestro colToByte[toCol] es start del cell que cubre toCol, pero si toCol esta justo en
        // limite de celda, queremos start de esa celda (correcto para substring [from,to) ).
        if (startByte < 0) startByte = 0;
        if (endByte < startByte) endByte = startByte;
        if (endByte > (int)line_.size()) endByte = line_.size();
        return line_.substr(startByte, endByte - startByte);
    }

    std::string expandVisible(int fromCol, int toCol) const {
        auto v = range(fromCol, toCol);
        // v ya es bytes originales sin expandir tabs excepto que tabs ocupan w cols.
        // Para expandir tabs a espacios dentro del rango, usamos utf8::expandTabs sobre v
        // pero v ya esta recortado por columnas, asi que expandTabs es correcto.
        // Nota: range ya respeto w de tabs, asi que expandir es simplemente convertir cada \t en espacios.
        return utf8::expandTabs(v);
    }

    int totalCols() const { return totalCols_; }
    int totalBytes() const { return static_cast<int>(line_.size()); }
    size_t memoryBytes() const {
        return line_.size() + byteToCol_.size()*sizeof(int) + colToByte_.size()*sizeof(int);
    }

private:
    std::string_view line_;
    std::vector<int> byteToCol_; // n+1
    std::vector<int> colToByte_; // totalCols+1
    int totalCols_ = 0;
};

// ---------------------------------------------------------------------------
// Estrategia 2: checkpoints incrementales
// ---------------------------------------------------------------------------
class RowLayoutCheckpoint {
public:
    static constexpr int kIntervalBytes = 1024; // tunable
    explicit RowLayoutCheckpoint(std::string_view line, int intervalBytes = kIntervalBytes)
        : line_(line), intervalBytes_(intervalBytes) {
        int n = static_cast<int>(line.size());
        int col = 0;
        int i = 0;
        // checkpoint inicial
        chkByte_.push_back(0);
        chkCol_.push_back(0);
        int nextChk = intervalBytes_;
        while (i < n) {
            if (i >= nextChk) {
                chkByte_.push_back(i);
                chkCol_.push_back(col);
                nextChk += intervalBytes_;
            }
            int w = utf8::cellWidth(line_, i, n);
            if (line_[i] == '\t') w = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - col;
            int len = utf8::cellLen(line_, i, n);
            col += w;
            i += len;
        }
        totalCols_ = col;
        // asegurar ultimo checkpoint no es necesario, pero guardar n
        // para busqueda no hace falta
    }

    int columnAt(int byte) const {
        if (byte <= 0) return 0;
        int n = static_cast<int>(line_.size());
        if (byte >= n) return totalCols_;
        // binary search checkpoint por byte
        int idx = int(std::upper_bound(chkByte_.begin(), chkByte_.end(), byte) - chkByte_.begin()) - 1;
        if (idx < 0) idx = 0;
        int col = chkCol_[idx];
        int i = chkByte_[idx];
        // scan hasta byte
        while (i < byte) {
            int w = utf8::cellWidth(line_, i, n);
            if (line_[i] == '\t') w = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - col;
            int len = utf8::cellLen(line_, i, n);
            // si byte cae dentro de celda, no sumamos w completo (columnOf para byte dentro
            // deberia devolver col del cell start, que es col actual, no col+w)
            if (i + len > byte) break;
            col += w;
            i += len;
        }
        return col;
    }

    std::string_view range(int fromCol, int toCol) const {
        if (toCol <= fromCol) return {};
        if (fromCol >= totalCols_) return {};
        if (fromCol < 0) fromCol = 0;
        if (toCol > totalCols_) toCol = totalCols_;
        int n = static_cast<int>(line_.size());
        // encontrar checkpoint por col
        int idx = int(std::upper_bound(chkCol_.begin(), chkCol_.end(), fromCol) - chkCol_.begin()) - 1;
        if (idx < 0) idx = 0;
        int col = chkCol_[idx];
        int i = chkByte_[idx];
        // scan hasta fromCol
        while (col < fromCol && i < n) {
            int w = utf8::cellWidth(line_, i, n);
            if (line_[i] == '\t') w = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - col;
            int len = utf8::cellLen(line_, i, n);
            if (col + w > fromCol) break; // fromCol cae dentro de esta celda -> start es i
            col += w;
            i += len;
        }
        int startByte = i;
        // continuar scan hasta toCol
        while (col < toCol && i < n) {
            int w = utf8::cellWidth(line_, i, n);
            if (line_[i] == '\t') w = ((col / utf8::TAB_WIDTH) + 1) * utf8::TAB_WIDTH - col;
            int len = utf8::cellLen(line_, i, n);
            if (col + w > toCol) break;
            col += w;
            i += len;
        }
        int endByte = i;
        if (endByte < startByte) endByte = startByte;
        return line_.substr(startByte, endByte - startByte);
    }

    std::string expandVisible(int fromCol, int toCol) const {
        return utf8::expandTabs(range(fromCol, toCol));
    }

    int totalCols() const { return totalCols_; }
    size_t memoryBytes() const {
        return line_.size() + chkByte_.size()*sizeof(int)*2 + sizeof(*this);
    }
    int checkpointCount() const { return static_cast<int>(chkByte_.size()); }

private:
    std::string_view line_;
    int intervalBytes_;
    std::vector<int> chkByte_;
    std::vector<int> chkCol_;
    int totalCols_ = 0;
};

} // namespace rowlayout
