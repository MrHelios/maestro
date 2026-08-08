#include "Editor.h"

#include <unistd.h>

Editor::Editor() {
    statusMessage_ = "Ctrl+S seleccion | Ctrl+K guardar/salir | Ctrl+U deshacer | Ctrl+Y rehacer";
    savedLines_ = document_.snapshot();
}

bool Editor::openFile(const std::string& path) {
    filename_ = path;
    bool existed = document_.loadFromFile(path);
    modified_ = false;
    savedLines_ = document_.snapshot();
    cursor_.line = 0;
    cursor_.col = 0;
    clearSelection();
    state_ = State::Normal;
    statusMessage_ = "";
    if (!existed) {
        statusMessage_ = "Archivo nuevo: " + path;
    }
    return existed;
}

void Editor::run() {
    int rows, cols;
    terminal_.getWindowSize(rows, cols);
    // Reservamos la ultima fila para la barra de estado.
    viewport_.height = rows > 1 ? rows - 1 : 1;
    viewport_.width = cols;

    terminal_.enableRawMode();

    // Cursor en forma de barra fina. Evita que el bloque de la terminal
    // ocupando la celda vacia tras la ultima palabra se vea como un
    // "espacio" extra al final de la linea. Lo restauramos al salir.
    write(STDOUT_FILENO, "\x1b[6 q", 5);

    // Primer render antes de esperar el primer evento.
    viewport_.scrollToCursor(cursor_);
    renderer_.render(document_, cursor_, viewport_, filename_, modified_, statusMessage_, selection_);

    while (running_) {
        Event event = terminal_.readEvent();
        handleEvent(event);

        if (!running_) break;

        viewport_.scrollToCursor(cursor_);
        renderer_.render(document_, cursor_, viewport_, filename_, modified_, statusMessage_, selection_);
    }

    terminal_.disableRawMode();
    // Limpiamos pantalla al salir para dejar la terminal prolija.
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[0 q", 12);
}

void Editor::handleEvent(const Event& event) {
    // En modo Prefix (tras Ctrl+K) todo pasa por handlePrefixKey: el
    // siguiente evento decide guardar/salir/cancelar.
    if (state_ == State::Prefix) {
        handlePrefixKey(event);
        return;
    }

    switch (event.type) {
        case EventType::InsertChar: {
            // Si hay seleccion, escribir reemplaza el texto seleccionado:
            // borra el rango y luego inserta el caracter. Es UNA sola
            // operacion de Undo (un unico pushHistory).
            if (selection_.has_value() && selection_->anchor != selection_->position) {
                pushHistory();
                auto sel = normalize(*selection_);
                document_.deleteRange(sel->start.line, sel->start.col,
                                      sel->end.line, sel->end.col);
                cursor_.line = sel->start.line;
                cursor_.col = sel->start.col;
                document_.insertChar(cursor_.line, cursor_.col, event.ch);
                cursor_.col++;
                clearSelection();
                state_ = State::Normal;
                modified_ = true;
                break;
            }

            // Sin seleccion: insertar como siempre. Salir del modo
            // seleccion si estaba activo (seleccion vacia).
            clearSelection();
            pushHistory();
            document_.insertChar(cursor_.line, cursor_.col, event.ch);
            cursor_.col++;
            state_ = State::Normal;
            clearSelection();
            modified_ = true;
            break;
        }

        case EventType::Prefix:
            // Ctrl+K: entra al modo prefijo. Guardamos el estado previo
            // para poder volver a el (guardar sin salir de seleccion).
            priorState_ = state_;
            state_ = State::Prefix;
            statusMessage_ = "Ctrl+K: Ctrl+S guardar | Ctrl+Q salir";
            break;

        case EventType::Select:
            // Ctrl+S: entrar al modo seleccion. Si ya estabamos en
            // seleccion, se ignora (sin efecto). En modo prefijo este
            // evento ya fue consumido arriba y equivale a guardar.
            if (state_ == State::Select) break;
            beginSelection();
            state_ = State::Select;
            statusMessage_ = "SELECCION (ESC sale)";
            break;

        case EventType::InsertNewline:
            // En modo seleccion, Enter reemplaza el rango por una nueva
            // linea (el "reemplaza la seleccion" de v0.3).
            if (selection_.has_value() && selection_->anchor != selection_->position) {
                pushHistory();
                auto sel = normalize(*selection_);
                document_.deleteRange(sel->start.line, sel->start.col,
                                      sel->end.line, sel->end.col);
                cursor_.line = sel->start.line;
                cursor_.col = sel->start.col;
                document_.insertNewline(cursor_.line, cursor_.col);
                cursor_.line++;
                cursor_.col = 0;
                clearSelection();
                state_ = State::Normal;
                modified_ = true;
                break;
            }

            clearSelection();
            pushHistory();
            document_.insertNewline(cursor_.line, cursor_.col);
            cursor_.line++;
            cursor_.col = 0;
            state_ = State::Normal;
            modified_ = true;
            break;

        case EventType::Backspace:
        case EventType::Delete: {
            // Si hay texto seleccionado, lo borramos completo (ambas
            // teclas hacen lo mismo) y el cursor queda en el inicio del
            // rango. Es una UNICA operacion de Undo.
            if (selection_.has_value() && selection_->anchor != selection_->position) {
                pushHistory();
                auto sel = normalize(*selection_);
                document_.deleteRange(sel->start.line, sel->start.col,
                                      sel->end.line, sel->end.col);
                cursor_.line = sel->start.line;
                cursor_.col = sel->start.col;
                cursor_.clampToLine(document_);
                clearSelection();
                state_ = State::Normal;
                modified_ = true;
                statusMessage_ = "Seleccion borrada.";
                break;
            }

            // Sin seleccion: comportamiento clasico de cada tecla.
            // Salimos del modo seleccion si estaba activo (seleccion
            // vacia): aunque no haya texto borrado, el modo ya no aplica.
            clearSelection();
            state_ = State::Normal;
            statusMessage_ = "";
            pushHistory();
            if (event.type == EventType::Backspace) {
                bool willMergeLines = (cursor_.col == 0 && cursor_.line > 0);
                int prevLineLen = willMergeLines ? document_.lineLength(cursor_.line - 1) : 0;

                if (document_.deleteCharBefore(cursor_.line, cursor_.col)) {
                    if (willMergeLines) {
                        // La linea se fundio con la anterior: el cursor
                        // queda justo donde terminaba esa linea anterior.
                        cursor_.line--;
                        cursor_.col = prevLineLen;
                    } else {
                        cursor_.col--;
                    }
                    modified_ = true;
                }
            } else if (document_.deleteCharAt(cursor_.line, cursor_.col)) {
                modified_ = true;
            }
            break;
        }

        case EventType::Undo:
            undo();
            break;

        case EventType::Redo:
            redo();
            break;

        case EventType::MoveLeft: {
            if (state_ == State::Select) beginSelection();
            else clearSelection();
            cursor_.moveLeft(document_);
            updateSelectionPosition();
            break;
        }

        case EventType::MoveRight: {
            if (state_ == State::Select) {
                // En modo seleccion la flecha derecha siempre extiende.
                beginSelection();
                cursor_.moveRight(document_);
                updateSelectionPosition();
            } else {
                // Flecha derecha con una seleccion hacia adelante: el
                // cursor ya esta en el extremo derecho de la seleccion.
                // Se cancela la seleccion y el cursor MANTIENE su posicion
                // actual (no avanza sobre el texto seleccionado).
                bool cursorAtSelectionEnd = selection_.has_value() &&
                                            selection_->anchor < selection_->position;
                clearSelection();
                if (!cursorAtSelectionEnd) {
                    cursor_.moveRight(document_);
                }
            }
            break;
        }

        case EventType::MoveUp: {
            if (state_ == State::Select) beginSelection();
            else clearSelection();
            cursor_.moveUp(document_);
            updateSelectionPosition();
            break;
        }

        case EventType::MoveDown: {
            if (state_ == State::Select) beginSelection();
            else clearSelection();
            cursor_.moveDown(document_);
            updateSelectionPosition();
            break;
        }

        case EventType::MoveHome: {
            if (state_ == State::Select) beginSelection();
            else clearSelection();
            cursor_.moveHome();
            updateSelectionPosition();
            break;
        }

        case EventType::MoveEnd: {
            if (state_ == State::Select) beginSelection();
            else clearSelection();
            cursor_.moveEnd(document_);
            updateSelectionPosition();
            break;
        }

        case EventType::Save:
            save();
            break;

        case EventType::Escape:
            // ESC suelto: cancela la seleccion activa sin mover el cursor.
            // Si estabamos en modo seleccion, salimos de el.
            if (state_ == State::Select) {
                state_ = State::Normal;
                statusMessage_ = "Seleccion cancelada.";
            }
            clearSelection();
            break;

        // Quit deliberadamente NO se maneja aqui: el usuario debe pasar
        // por el prefijo (Ctrl+K -> Ctrl+Q) para salir. Un evento Quit
        // suelto (p.ej. Ctrl+Q sin prefix) se ignora y no mata el editor.
        // Esto es una mejora de seguridad: evitar salir por accidente con
        // una sola tecla. handlePrefixKey se encarga del Quit real.
        case EventType::None:
        default:
            break;
    }
}

void Editor::handlePrefixKey(const Event& event) {
    switch (event.type) {
        case EventType::Save:
        case EventType::Select: // Ctrl+S tras Ctrl+K = guardar archivo
            save();
            state_ = priorState_;
            break;

        case EventType::Quit:
            running_ = false;
            break;

        default:
            // Cualquier otra tecla (incl. ESC, flechas, caracteres...):
            // se descarta el evento y se cancela el prefijo, volviendo al
            // estado anterior sin tocar la seleccion ni el documento.
            state_ = priorState_;
            break;
    }
}

bool Editor::hasSelection() const {
    return selection_.has_value() && selection_->anchor != selection_->position;
}

std::optional<Normalized> Editor::selection() const {
    if (!selection_.has_value()) return std::nullopt;
    return normalize(*selection_);
}

void Editor::beginSelection() {
    if (!selection_.has_value()) {
        selection_ = Selection{};
        selection_->anchor = {cursor_.line, cursor_.col};
        selection_->position = {cursor_.line, cursor_.col};
    }
}

void Editor::updateSelectionPosition() {
    if (selection_.has_value()) {
        selection_->position = {cursor_.line, cursor_.col};
    }
}

void Editor::clearSelection() {
    selection_.reset();
}

void Editor::save() {
    if (document_.saveToFile(filename_)) {
        modified_ = false;
        savedLines_ = document_.snapshot();
        statusMessage_ = "Guardado.";
    } else {
        statusMessage_ = "Error al guardar.";
    }
}

void Editor::pushHistory() {
    HistoryState state;
    state.lines = document_.snapshot();
    state.line = cursor_.line;
    state.col = cursor_.col;
    state.selection = selection_;
    undoStack_.push_back(state);
    if (undoStack_.size() > MAX_UNDO) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

void Editor::applyState(const HistoryState& state) {
    document_.restore(state.lines);
    cursor_.line = state.line;
    cursor_.col = state.col;
    cursor_.clampToLine(document_);
    // Restauramos la seleccion del momento. Si quedo fuera de rango
    // (p.ej. por undo de un documento distinto), la descartamos.
selection_ = state.selection;
    if (selection_.has_value()) {
        auto n = normalize(*selection_);
        if (!n.has_value() || n->start.line >= document_.lineCount() ||
            n->end.line >= document_.lineCount() ||
            n->start.col > document_.lineLength(n->start.line) ||
            n->end.col > document_.lineLength(n->end.line)) {
            clearSelection();
        }
}
    // La seleccion restaurada vuelve a estar VIGENTE (modo seleccion
    // si hay algo seleccionado, Normal si no).
    state_ = selection_.has_value() ? State::Select : State::Normal;
    // modified_ = "¿el contenido difiere del ultimo guardado?"
    modified_ = (document_.snapshot() != savedLines_);
}

void Editor::undo() {
    if (undoStack_.empty()) {
        statusMessage_ = "Nada que deshacer.";
        return;
    }

    // Guardamos el estado actual para poder rehacer.
    HistoryState current;
    current.lines = document_.snapshot();
    current.line = cursor_.line;
    current.col = cursor_.col;
    current.selection = selection_;
    redoStack_.push_back(current);

    applyState(undoStack_.back());
    undoStack_.pop_back();
    statusMessage_ = "Deshecho.";
}

void Editor::redo() {
    if (redoStack_.empty()) {
        statusMessage_ = "Nada que rehacer.";
        return;
    }

    // Guardamos el estado actual en el historial de deshacer.
    HistoryState current;
    current.lines = document_.snapshot();
    current.line = cursor_.line;
    current.col = cursor_.col;
    current.selection = selection_;
    undoStack_.push_back(current);

    applyState(redoStack_.back());
    redoStack_.pop_back();
    statusMessage_ = "Rehecho.";
}
