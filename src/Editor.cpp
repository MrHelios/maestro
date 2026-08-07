#include "Editor.h"

#include <unistd.h>

Editor::Editor() {
    statusMessage_ = "Ctrl+S guardar | Ctrl+Q salir";
    savedLines_ = document_.snapshot();
}

bool Editor::openFile(const std::string& path) {
    filename_ = path;
    bool existed = document_.loadFromFile(path);
    modified_ = false;
    savedLines_ = document_.snapshot();
    cursor_.line = 0;
    cursor_.col = 0;
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
    renderer_.render(document_, cursor_, viewport_, filename_, modified_, statusMessage_);

    while (running_) {
        Event event = terminal_.readEvent();
        handleEvent(event);

        if (!running_) break;

        viewport_.scrollToCursor(cursor_);
        renderer_.render(document_, cursor_, viewport_, filename_, modified_, statusMessage_);
    }

    terminal_.disableRawMode();
    // Limpiamos pantalla al salir para dejar la terminal prolija.
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[0 q", 12);
}

void Editor::handleEvent(const Event& event) {
    switch (event.type) {
        case EventType::InsertChar:
            pushHistory();
            document_.insertChar(cursor_.line, cursor_.col, event.ch);
            cursor_.col++;
            modified_ = true;
            break;

        case EventType::InsertNewline:
            pushHistory();
            document_.insertNewline(cursor_.line, cursor_.col);
            cursor_.line++;
            cursor_.col = 0;
            modified_ = true;
            break;

        case EventType::Backspace: {
            pushHistory();
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
            break;
        }

        case EventType::Delete:
            pushHistory();
            if (document_.deleteCharAt(cursor_.line, cursor_.col)) {
                modified_ = true;
            }
            break;

        case EventType::Undo:
            undo();
            break;

        case EventType::Redo:
            redo();
            break;

        case EventType::MoveLeft:
            cursor_.moveLeft(document_);
            break;

        case EventType::MoveRight:
            cursor_.moveRight(document_);
            break;

        case EventType::MoveUp:
            cursor_.moveUp(document_);
            break;

        case EventType::MoveDown:
            cursor_.moveDown(document_);
            break;

        case EventType::MoveHome:
            cursor_.moveHome();
            break;

        case EventType::MoveEnd:
            cursor_.moveEnd(document_);
            break;

        case EventType::Save:
            save();
            break;

        case EventType::Quit:
            running_ = false;
            break;

        case EventType::None:
        default:
            break;
    }
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
    undoStack_.push_back(state);
    if (undoStack_.size() > 1000) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

void Editor::applyState(const HistoryState& state) {
    document_.restore(state.lines);
    cursor_.line = state.line;
    cursor_.col = state.col;
    cursor_.clampToLine(document_);
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
    undoStack_.push_back(current);

    applyState(redoStack_.back());
    redoStack_.pop_back();
    statusMessage_ = "Rehecho.";
}
