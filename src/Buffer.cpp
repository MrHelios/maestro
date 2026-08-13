#include "Buffer.h"

Buffer::Buffer() {
    unnamedName = "SinNombre";
    savedLines = document.snapshot();
}

std::string Buffer::displayName() const {
    if (!filename.empty()) {
        size_t pos = filename.find_last_of('/');
        if (pos != std::string::npos) return filename.substr(pos + 1);
        return filename;
    }
    return unnamedName;
}

void Buffer::pushHistory() {
    HistoryState state;
    state.lines = document.snapshot();
    state.line = cursor.line;
    state.col = cursor.col;
    state.selection = selection;
    undoStack.push_back(state);
    if (undoStack.size() > MAX_UNDO) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
}

void Buffer::applyState(const HistoryState& state) {
    document.restore(state.lines);
    cursor.line = state.line;
    cursor.col = state.col;
    cursor.clampToLine(document);
    // Restauramos la seleccion del momento. Si quedo fuera de rango
    // (p.ej. por undo de un documento distinto), la descartamos.
    selection = state.selection;
    if (selection.has_value()) {
        auto n = normalize(*selection);
        if (!n.has_value() || n->start.line >= document.lineCount() ||
            n->end.line >= document.lineCount() ||
            n->start.col > document.lineLength(n->start.line) ||
            n->end.col > document.lineLength(n->end.line)) {
            selection.reset();
        }
    }
    // modified = "¿el contenido difiere del ultimo guardado?"
    modified = (document.snapshot() != savedLines);
}
