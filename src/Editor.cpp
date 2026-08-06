#include "Editor.h"

#include <unistd.h>

Editor::Editor() {
    statusMessage_ = "Ctrl+S guardar | Ctrl+Q salir";
}

bool Editor::openFile(const std::string& path) {
    filename_ = path;
    bool existed = document_.loadFromFile(path);
    modified_ = false;
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
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 8);
}

void Editor::handleEvent(const Event& event) {
    switch (event.type) {
        case EventType::InsertChar:
            document_.insertChar(cursor_.line, cursor_.col, event.ch);
            cursor_.col++;
            modified_ = true;
            break;

        case EventType::InsertNewline:
            document_.insertNewline(cursor_.line, cursor_.col);
            cursor_.line++;
            cursor_.col = 0;
            modified_ = true;
            break;

        case EventType::Backspace: {
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
            if (document_.deleteCharAt(cursor_.line, cursor_.col)) {
                modified_ = true;
            }
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
        statusMessage_ = "Guardado.";
    } else {
        statusMessage_ = "Error al guardar.";
    }
}
