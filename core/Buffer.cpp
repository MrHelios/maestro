#include "core/Buffer.h"

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
    state.endsWithNewline = document.endsWithNewline();
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
    document.setEndsWithNewline(state.endsWithNewline);
    cursor.line = state.line;
    cursor.col = state.col;
    cursor.clampToLine(document);
    // Restauramos la seleccion del momento. La descartamos SOLO si quedo
    // fuera de rango tras el undo/redo (p.ej. por restauration de un
    // documento distinto). Una seleccion DEGENERADA (anchor == position)
    // dentro de rango se CONSERVA tal cual, para que undo/redo sean
    // simetricos: una seleccion vacia que existia al momento de la edicion
    // tambien debe restablecerse (tiene hasSelection() == false, como la
    // original, asi que no cambia el modo).
    selection = state.selection;
    if (selection.has_value()) {
        bool outOfRange =
            selection->anchor.line >= document.lineCount() ||
            selection->position.line >= document.lineCount() ||
            selection->anchor.col > document.lineLength(selection->anchor.line) ||
            selection->position.col > document.lineLength(selection->position.line);
        if (outOfRange) selection.reset();
    }
    // modified = "¿el contenido difiere del ultimo guardado?"
    modified = (document.snapshot() != savedLines);
}
