#include "Editor.h"

#include <unistd.h>

namespace {

// Convierte una ruta posiblemente relativa en una absoluta (cwd() + "/"
// + ruta) para mostrarla en la barra de estado. Las rutas ya absolutas
// o vacias se devuelven tal cual.
std::string resolveAbsolutePath(const std::string& path) {
    if (path.empty() || path.front() == '/') return path;
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) return path;
    return std::string(buf) + "/" + path;
}

} // namespace

Editor::Editor() {
    statusMessage_ = "NAVEGACION: i escribir | s seleccionar | Ctrl+K guardar/salir | Ctrl+U/Y deshacer/rehacer";
    savedLines_ = document_.snapshot();
}

bool Editor::openFile(const std::string& path) {
    // La barra de estado muestra siempre una ruta absoluta: si el
    // archivo se abrio con ruta relativa, la resolvemos contra cwd().
    filename_ = resolveAbsolutePath(path);
    bool existed = document_.loadFromFile(path);
    modified_ = false;
    savedLines_ = document_.snapshot();
    cursor_.line = 0;
    cursor_.col = 0;
    clearSelection();
    state_ = State::Navegacion;
    statusMessage_ = "";
    if (!existed) {
        statusMessage_ = "Archivo nuevo: " + path;
    }
    return existed;
}

void Editor::run() {
    int rows, cols;
    terminal_.getWindowSize(rows, cols);
    // La barra de estado ocupa las ultimas DOS filas: la fila fija (en
    // video inverso) y la fila de mensajes. El viewport usa el resto.
    viewport_.height = rows > 2 ? rows - 2 : 1;
    viewport_.width = cols;

    terminal_.enableRawMode();

    // Cursor en forma de barra fina. Evita que el bloque de la terminal
    // ocupando la celda vacia tras la ultima palabra se vea como un
    // "espacio" extra al final de la linea. Lo restauramos al salir.
    write(STDOUT_FILENO, "\x1b[6 q", 5);

    // Primer render antes de esperar el primer evento.
    viewport_.scrollToCursor(cursor_);
    renderer_.render(document_, cursor_, viewport_, filename_, modified_, statusMessage_, state_, selection_);

    while (running_) {
        Event event = terminal_.readEvent();
        handleEvent(event);

        if (!running_) break;

        viewport_.scrollToCursor(cursor_);
        renderer_.render(document_, cursor_, viewport_, filename_, modified_, statusMessage_, state_, selection_);
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

    // Undo/Redo y la entrada al prefijo estan disponibles en los 3 modos
    // y no dependen de state_, asi que se evaluan ANTES del despacho por
    // modo. Nota: Ctrl+S (Save) SOLO tiene efecto tras el prefijo (lo
    // consume handlePrefixKey). Sin prefijo llega aqui y cae como no-op
    // en cada modo: Ctrl+S deja de tener significado especial.
    if (event.type == EventType::Undo) { undo(); return; }
    if (event.type == EventType::Redo) { redo(); return; }
    if (event.type == EventType::Prefix) {
        priorState_ = state_;
        state_ = State::Prefix;
        statusMessage_ = "Ctrl+K: Ctrl+S guardar | Ctrl+Q salir";
        return;
    }

    // El resto se interpreta segun el modo actual. Terminal emite los
    // InsertChar ('i'/'s'/'c'/'x' y cualquier letra) tal cual; es el
    // Editor quien decide, segun state_, si una letra puntual es un
    // comando de modo o texto real.
    switch (state_) {
        case State::Navegacion:
            handleNavegacionEvent(event);
            break;
        case State::Interaccion:
            handleInteraccionEvent(event);
            break;
        case State::Seleccion:
            handleSeleccionEvent(event);
            break;
        default:
            break;
    }
}

void Editor::handleNavegacionEvent(const Event& event) {
    switch (event.type) {
        case EventType::InsertChar:
            // En navegacion no se escribe: las letras solo pueden ser
            // comandos de modo. 'i' entra a edicion; 's' a seleccion.
            // 'c'/'x'/'p' y cualquier otra letra son no-op en v0.5.
            if (event.text == "i") {
                state_ = State::Interaccion;
                statusMessage_ = "INTERACCION (ESC vuelve a navegacion)";
            } else if (event.text == "s") {
                beginSelection();
                state_ = State::Seleccion;
                statusMessage_ = "SELECCION (ESC/c/x terminan)";
            }
            break;

        // Movimientos libres, sin iniciar seleccion (a diferencia de
        // como Select extendia en v0.3-v0.4).
        case EventType::MoveLeft: cursor_.moveLeft(document_); break;
        case EventType::MoveRight: cursor_.moveRight(document_); break;
        case EventType::MoveUp: cursor_.moveUp(document_); break;
        case EventType::MoveDown: cursor_.moveDown(document_); break;
        case EventType::MoveHome: cursor_.moveHome(); break;
        case EventType::MoveEnd: cursor_.moveEnd(document_); break;

        // InsertNewline/Backspace/Delete y Escape: no-op (no hay edicion
        // posible y ya estamos en navegacion, no hay a donde volver).
        default:
            break;
    }
}

void Editor::handleInteraccionEvent(const Event& event) {
    switch (event.type) {
        case EventType::InsertChar:
            // Edicion libre real: cualquier letra (incluida i/s/p/c/x)
            // se inserta como texto. Aqui no son comandos de modo.
            pushHistory();
            document_.insertText(cursor_.line, cursor_.col, event.text);
            cursor_.col += static_cast<int>(event.text.size());
            modified_ = true;
            break;

        case EventType::InsertNewline:
            pushHistory();
            document_.insertNewline(cursor_.line, cursor_.col);
            cursor_.line++;
            cursor_.col = 0;
            modified_ = true;
            break;

        case EventType::Backspace:
        case EventType::Delete: {
            pushHistory();
            if (event.type == EventType::Backspace) {
                bool willMergeLines = (cursor_.col == 0 && cursor_.line > 0);
                int prevLineLen = willMergeLines ? document_.lineLength(cursor_.line - 1) : 0;

                int charStart = cursor_.col;
                if (!willMergeLines) {
                    const std::string& ln_ = document_.lineAt(cursor_.line);
                    charStart = cursor_.col - 1;
                    while (charStart > 0 &&
                           (static_cast<unsigned char>(ln_[charStart]) & 0xC0) == 0x80) {
                        charStart--;
                    }
                }

                if (document_.deleteCharBefore(cursor_.line, cursor_.col)) {
                    if (willMergeLines) {
                        cursor_.line--;
                        cursor_.col = prevLineLen;
                    } else {
                        cursor_.col = charStart;
                    }
                    modified_ = true;
                }
            } else if (document_.deleteCharAt(cursor_.line, cursor_.col)) {
                modified_ = true;
            }
            break;
        }

        case EventType::Escape:
            state_ = State::Navegacion;
            statusMessage_ = "NAVEGACION";
            break;

        case EventType::MoveLeft: cursor_.moveLeft(document_); break;
        case EventType::MoveRight: cursor_.moveRight(document_); break;
        case EventType::MoveUp: cursor_.moveUp(document_); break;
        case EventType::MoveDown: cursor_.moveDown(document_); break;
        case EventType::MoveHome: cursor_.moveHome(); break;
        case EventType::MoveEnd: cursor_.moveEnd(document_); break;

        default:
            break;
    }
}

void Editor::handleSeleccionEvent(const Event& event) {
    switch (event.type) {
        // Los movimientos extienden la seleccion, igual que hacia v0.3-v0.4.
        case EventType::MoveLeft:
            beginSelection(); cursor_.moveLeft(document_); updateSelectionPosition(); break;
        case EventType::MoveRight:
            beginSelection(); cursor_.moveRight(document_); updateSelectionPosition(); break;
        case EventType::MoveUp:
            beginSelection(); cursor_.moveUp(document_); updateSelectionPosition(); break;
        case EventType::MoveDown:
            beginSelection(); cursor_.moveDown(document_); updateSelectionPosition(); break;
        case EventType::MoveHome:
            beginSelection(); cursor_.moveHome(); updateSelectionPosition(); break;
        case EventType::MoveEnd:
            beginSelection(); cursor_.moveEnd(document_); updateSelectionPosition(); break;

        // 'c' y 'x' terminan la seleccion y vuelven a navegacion. En v0.5
        // ambos hacen lo mismo (sin efecto de buffer); la diferencia real
        // llegara con el buffer en v0.55.
        case EventType::InsertChar:
            if (event.text == "c" || event.text == "x") {
                clearSelection();
                state_ = State::Navegacion;
                statusMessage_ = "NAVEGACION";
            }
            // Cualquier otra letra ya NO reemplaza la seleccion: se ignora.
            break;

        case EventType::Escape:
            clearSelection();
            state_ = State::Navegacion;
            statusMessage_ = "Seleccion cancelada.";
            break;

        // InsertNewline/Backspace/Delete (y el resto): no-op. Salir de
        // seleccion es siempre a navegacion, nunca a interaccion con
        // reemplazo del rango.
        default:
            break;
    }
}

void Editor::handlePrefixKey(const Event& event) {
    switch (event.type) {
        case EventType::Save: // Ctrl+S tras Ctrl+K = guardar archivo
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
            statusMessage_ = "Comando cancelado.";
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
    // La seleccion restaurada vuelve a estar VIGENTE. Importante: el modo
    // Seleccion solo debe activarse si el rango restaurado es realmente NO
    // vacio. Usar `has_value()` como criterio dejaria el estado en Seleccion
    // para una seleccion vacia (anchor == position), con la barra de estado
    // mostrando "SELECCION" sin texto resaltado. Compartimos el criterio
    // con hasSelection() (anchor != position). Nota: si el usuario estaba
    // en Interaccion al momento del pushHistory (sin seleccion), el undo
    // vuelve a Navegacion (el historial no distinguie Navegacion de
    // Interaccion; solo sabe si hay o no seleccion). Es un criterio
    // aceptado: undo es del documento, no de la UI.
    state_ = (selection_.has_value() && selection_->anchor != selection_->position)
           ? State::Seleccion
           : State::Navegacion;
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
