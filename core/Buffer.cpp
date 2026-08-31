#include "core/Buffer.h"

#include <cassert>

#include "core/utf8.h"

void Buffer::rebindCallback() {
    document.setTouchedCallback([this](int a,int b){ recordWatch(a,b); });
}

Buffer::Buffer() {
    unnamedName = "SinNombre";
    syncSavedState();
    rebindCallback();
}

Buffer::Buffer(const Buffer& other)
    : document(other.document), cursor(other.cursor), viewport(other.viewport),
      filename(other.filename), unnamedName(other.unnamedName), modified(other.modified),
      selection(other.selection), selectAllActive(other.selectAllActive),
      selectAllPrevious(other.selectAllPrevious), originalSnapshot_(other.originalSnapshot_),
      watcher_(other.watcher_), savedEndsWithNewline(other.savedEndsWithNewline),
      undoStack(other.undoStack), redoStack(other.redoStack),
      savedIdentity(other.savedIdentity) {
    rebindCallback();
}

Buffer::Buffer(Buffer&& other) noexcept
    : document(std::move(other.document)), cursor(other.cursor), viewport(other.viewport),
      filename(std::move(other.filename)), unnamedName(std::move(other.unnamedName)), modified(other.modified),
      selection(std::move(other.selection)), selectAllActive(other.selectAllActive),
      selectAllPrevious(std::move(other.selectAllPrevious)), originalSnapshot_(std::move(other.originalSnapshot_)),
      watcher_(std::move(other.watcher_)), savedEndsWithNewline(other.savedEndsWithNewline),
      undoStack(std::move(other.undoStack)), redoStack(std::move(other.redoStack)),
      savedIdentity(other.savedIdentity) {
    rebindCallback();
    other.document.setTouchedCallback(nullptr);
}

Buffer& Buffer::operator=(const Buffer& other) {
    if (this == &other) return *this;
    document = other.document;
    cursor = other.cursor;
    viewport = other.viewport;
    filename = other.filename;
    unnamedName = other.unnamedName;
    modified = other.modified;
    selection = other.selection;
    selectAllActive = other.selectAllActive;
    selectAllPrevious = other.selectAllPrevious;
    originalSnapshot_ = other.originalSnapshot_;
    watcher_ = other.watcher_;
    savedEndsWithNewline = other.savedEndsWithNewline;
    undoStack = other.undoStack;
    redoStack = other.redoStack;
    savedIdentity = other.savedIdentity;
    rebindCallback();
    return *this;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this == &other) return *this;
    document = std::move(other.document);
    cursor = other.cursor;
    viewport = other.viewport;
    filename = std::move(other.filename);
    unnamedName = std::move(other.unnamedName);
    modified = other.modified;
    selection = std::move(other.selection);
    selectAllActive = other.selectAllActive;
    selectAllPrevious = std::move(other.selectAllPrevious);
    originalSnapshot_ = std::move(other.originalSnapshot_);
    watcher_ = std::move(other.watcher_);
    savedEndsWithNewline = other.savedEndsWithNewline;
    undoStack = std::move(other.undoStack);
    redoStack = std::move(other.redoStack);
    savedIdentity = other.savedIdentity;
    rebindCallback();
    other.document.setTouchedCallback(nullptr);
    return *this;
}

void Buffer::syncSavedState() {
    originalSnapshot_ = document.snapshot();
    savedEndsWithNewline = document.endsWithNewline();
    watcher_.clear();
    modified = false;
}

void Buffer::recordWatch(int rowStart, int rowEnd) {
    if (!watcher_.empty()) {
        auto& last = watcher_.back();
        if (last.rowStart == rowStart && last.rowEnd == rowEnd) return;
    }
    watcher_.push_back({rowStart, rowEnd});
}

bool Buffer::isModified() const {
    const auto& orig = originalSnapshot_;
    int origCount = static_cast<int>(orig.size());
    int curCount = document.lineCount();
    if (curCount != origCount) return true;
    if (watcher_.empty()) return false;
    std::vector<char> seen(static_cast<size_t>(curCount), 0);
    for (auto &e : watcher_) {
        int a = e.rowStart;
        int b = e.rowEnd;
        if (a < 0) a = 0;
        if (b >= curCount) b = curCount - 1;
        if (a > b) continue;
        for (int l = a; l <= b; ++l) {
            if (seen[static_cast<size_t>(l)]) continue;
            seen[static_cast<size_t>(l)] = 1;
            if (document.lineAt(l) != orig[static_cast<size_t>(l)]) return true;
        }
    }
    return false;
}

void Buffer::recalcModified() { modified = isModified(); }

std::string Buffer::displayName() const {
    if (!filename.empty()) {
        size_t pos = filename.find_last_of('/');
        if (pos != std::string::npos) return filename.substr(pos + 1);
        return filename;
    }
    return unnamedName;
}

HistoryEntry Buffer::beginHistoryEntry() const {
    HistoryEntry e;
    e.cursorBefore = {cursor.line, cursor.col};
    e.selectionBefore = selection;
    e.endsWithNewlineBefore = document.endsWithNewline();
    return e;
}

void Buffer::commitHistoryEntry(HistoryEntry entry) {
    // Sin edits no hubo mutacion real: descartar la entrada para no
    // dejar pasos de undo "fantasma" (ni limpiar el redo en vano).
    if (entry.edits.empty()) return;

    entry.cursorAfter = {cursor.line, cursor.col};
    entry.selectionAfter = selection;
    entry.endsWithNewlineAfter = document.endsWithNewline();

    undoStack.push_back(std::move(entry));
    if (undoStack.size() > MAX_UNDO) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
}

void Buffer::extendLastEntry(Edit edit) {
    // Contrato: esta edit pertenece al grupo ya presente en el historial
    // (solo se llama con coalescingTyping_ activo, tras un commit). Si no
    // hay entrada, es un bug de logica en el llamador, no algo a reparar
    // aqui creando una entrada magica.
    assert(!undoStack.empty() &&
           "extendLastEntry sin grupo activo: bug de coalescingTyping_");

    HistoryEntry& e = undoStack.back();
    e.edits.push_back(std::move(edit));
    e.cursorAfter = {cursor.line, cursor.col};
    e.selectionAfter = selection;
    e.endsWithNewlineAfter = document.endsWithNewline();
}

void Buffer::applyForward(const Edit& e) {
    switch (e.type) {
        case EditType::Insert:
            // La posicion final la calcula Document (celdas + '\n'); el
            // `end` de la Edit ya fue registrado por quien la emitio.
            document.insertText(e.start.line, e.start.col, e.text);
            break;
        case EditType::SplitLine:
            document.splitLine(e.start.line, e.start.col);
            break;
        case EditType::Delete:
            document.deleteRange(e.start.line, e.start.col,
                                 e.end.line, e.end.col);
            break;
        case EditType::MergeLine:
            // Fundir: borrar el '\n' que separaba las dos lineas.
            document.mergeLine(e.start.line);
            break;
    }
}

void Buffer::applyBackward(const Edit& e) {
    switch (e.type) {
        case EditType::Insert:
            document.deleteRange(e.start.line, e.start.col,
                                 e.end.line, e.end.col);
            break;
        case EditType::SplitLine:
            // Deshacer Enter: fundir la linea partida con su siguiente.
            document.mergeLine(e.start.line);
            break;
        case EditType::Delete:
            document.insertText(e.start.line, e.start.col, e.text);
            break;
        case EditType::MergeLine:
            // Deshacer la fusion: re-partir en la columna donde estaba
            // el '\n' eliminado.
            document.splitLine(e.start.line, e.start.col);
            break;
    }
}

bool Buffer::undo() {
    if (undoStack.empty()) return false;
    HistoryEntry entry = undoStack.back();

    // El lado "despues" se REFRESCA con el estado vigente al momento del
    // undo: eso es exactamente lo que el redo debera restaurar (el estado
    // tal como estaba cuando se deshizo, incluido cualquier movimiento de
    // cursor posterior a la edicion).
    entry.cursorAfter = {cursor.line, cursor.col};
    entry.selectionAfter = selection;
    entry.endsWithNewlineAfter = document.endsWithNewline();

    undoStack.pop_back();

    for (auto it = entry.edits.rbegin(); it != entry.edits.rend(); ++it) {
        applyBackward(*it);
    }

    document.setEndsWithNewline(entry.endsWithNewlineBefore);
    cursor.line = entry.cursorBefore.line;
    cursor.col = entry.cursorBefore.col;
    cursor.clampToLine(document);
    restoreSelection(entry.selectionBefore);

    recalcModified();

    // La misma entrada describe la operacion hacia adelante: queda
    // pendiente en redoStack.
    redoStack.push_back(std::move(entry));
    return true;
}

bool Buffer::redo() {
    if (redoStack.empty()) return false;
    HistoryEntry entry = redoStack.back();

    // Simetrico al undo: el lado "antes" se refresca con el estado vigente,
    // de modo que re-deshacer vuelva a este mismo punto.
    entry.cursorBefore = {cursor.line, cursor.col};
    entry.selectionBefore = selection;
    entry.endsWithNewlineBefore = document.endsWithNewline();

    redoStack.pop_back();

    for (const Edit& e : entry.edits) {
        applyForward(e);
    }

    document.setEndsWithNewline(entry.endsWithNewlineAfter);
    cursor.line = entry.cursorAfter.line;
    cursor.col = entry.cursorAfter.col;
    cursor.clampToLine(document);
    restoreSelection(entry.selectionAfter);

    recalcModified();

    // La entrada vuelve al undoStack para poder deshacerla de nuevo.
    undoStack.push_back(std::move(entry));
    return true;
}

void Buffer::restoreSelection(std::optional<Selection> sel) {
    selection = std::move(sel);
    if (selection.has_value()) {
        bool outOfRange =
            selection->anchor.line >= document.lineCount() ||
            selection->position.line >= document.lineCount() ||
            selection->anchor.col > document.lineLength(selection->anchor.line) ||
            selection->position.col > document.lineLength(selection->position.line);
        if (outOfRange) selection.reset();
    }
}
