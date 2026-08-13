#include "Editor.h"

#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

#include "utf8.h"

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
    statusMessage_ = "NAVEGACION: i escribir | s seleccionar | c/x copiar/cortar | p pegar | Ctrl+K buffer/guardar/salir | Ctrl+U/Y deshacer/rehacer";
    // Invariante 1 y 2 (v0.6.3): siempre existe al menos un buffer y hay
    // exactamente uno activo. El constructor arranca con un unico buffer
    // sin nombre y lo deja activo.
    buffers_.emplace_back();
    activeBuffer_ = 0;
    unnamedCounter_ = 1; // el buffer inicial ya gasto "SinNombre"
}

bool Editor::isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

Buffer& Editor::active() {
    return buffers_[static_cast<size_t>(activeBuffer_)];
}

const Buffer& Editor::active() const {
    return buffers_[static_cast<size_t>(activeBuffer_)];
}

std::string Editor::nextUnnamedName() {
    if (unnamedCounter_ == 0) return "SinNombre";
    return "SinNombre" + std::to_string(unnamedCounter_);
}

bool Editor::openFile(const std::string& path) {
    // v0.6.2: solo archivos. Una carpeta no se trata como archivo
    // nuevo: se rechaza y el editor queda como estaba.
    if (isDirectory(path)) {
        statusMessage_ = "No se pueden abrir carpetas.";
        return false;
    }

    Buffer& b = active();
    // La barra de estado muestra siempre una ruta absoluta: si el
    // archivo se abrio con ruta relativa, la resolvemos contra cwd().
    b.filename = resolveAbsolutePath(path);
    bool existed = b.document.loadFromFile(path);
    b.modified = false;
    b.savedLines = b.document.snapshot();
    b.cursor.line = 0;
    b.cursor.col = 0;
    b.selection.reset();
    b.selectAllActive = false;
    b.selectAllPrevious.reset();
    state_ = State::Navegacion;
    statusMessage_ = "";
    if (!existed) {
        statusMessage_ = "Archivo nuevo: " + path;
    }
    return existed;
}

void Editor::syncViewportSize(Buffer& b) {
    int rows, cols;
    terminal_.getWindowSize(rows, cols);
    b.viewport.height = rows > 2 ? rows - 2 : 1;
    b.viewport.width = cols;
}

void Editor::createBuffer() {
    Buffer nuevo;
    nuevo.unnamedName = nextUnnamedName();
    unnamedCounter_++;
    // El viewport del buffer nuevo debe tener las dimensiones reales de
    // la terminal (run() solo las fijo al arrancar para los buffers ya
    // existentes). Sin esto, un buffer creado a mitad de sesion con el
    // Viewport por defecto no redibuja toda la pantalla y queda resto
    // del buffer anterior.
    syncViewportSize(nuevo);
    // El buffer nuevo se convierte inmediatamente en el buffer activo
    // (v0.6.3, invariante 13).
    buffers_.push_back(std::move(nuevo));
    activeBuffer_ = static_cast<int>(buffers_.size()) - 1;
    statusMessage_ = "Buffer nuevo: " + active().unnamedName;
}

void Editor::closeActiveBuffer() {
    Buffer& b = active();

    // Invariante 10 (v0.6.3): un buffer modificado no se puede cerrar.
    // Hay que guardar los cambios (Ctrl+K s) o restaurarlo (undo hasta el
    // ultimo estado guardado). No se ofrece confirmacion: se bloquea.
    if (b.modified) {
        statusMessage_ = "Buffer modificado: guarda con Ctrl+K s o restaura.";
        state_ = priorState_;
        return;
    }

    // Invariante 14 (v0.6.3): el ultimo buffer nunca se elimina. En lugar
    // de dejarlo con buffers_.empty(), se convierte en un buffer vacio sin
    // nombre (nuevo nombre generico, documento de una linea vacia,
    // modified = false) y seguimos en el.
    if (buffers_.size() == 1) {
        b.document = Document();
        b.cursor = Cursor();
        syncViewportSize(b);
        b.filename.clear();
        b.unnamedName = nextUnnamedName();
        unnamedCounter_++;
        b.modified = false;
        b.selection.reset();
        b.selectAllActive = false;
        b.selectAllPrevious.reset();
        b.savedLines = b.document.snapshot();
        b.undoStack.clear();
        b.redoStack.clear();
        statusMessage_ = "Buffer reiniciado: " + b.unnamedName;
        state_ = State::Navegacion;
        return;
    }

    // Varios buffers: se elimina el activo y se pasa al selector. No se
    // selecciona automaticamente otro buffer: la lista decide. El indice
    // deja de referenciar el buffer borrado (invariante 17).
    buffers_.erase(buffers_.begin() + activeBuffer_);
    activeBuffer_ = 0;
    bufferSelectorIndex_ = 0;
    priorState_ = State::Navegacion; // el contexto previo desaparecio
    state_ = State::BufferSelector;
    statusMessage_ = "Buffer cerrado. ↑/↓ y Enter para elegir.";
}

void Editor::activateBuffer(int idx) {
    activeBuffer_ = idx;
    // Reconciliar el modo global con el estado del buffer activado: un
    // buffer con rango seleccionado deja el editor en Seleccion; sin
    // rango, en Navegacion. (La seleccion y los demas estados son del
    // buffer, no del Editor.)
    const Buffer& b = active();
    state_ = (b.selection.has_value() && b.selection->anchor != b.selection->position)
           ? State::Seleccion
           : State::Navegacion;
    statusMessage_ = "";
}

std::vector<std::string> Editor::bufferNames() const {
    std::vector<std::string> names;
    names.reserve(buffers_.size());
    for (const Buffer& b : buffers_) {
        names.push_back(b.modified ? b.displayName() + " *"
                                   : b.displayName());
    }
    return names;
}

void Editor::handleBufferSelectorEvent(const Event& event) {
    switch (event.type) {
        case EventType::MoveUp:
            if (bufferSelectorIndex_ > 0) bufferSelectorIndex_--;
            break;
        case EventType::MoveDown:
            if (bufferSelectorIndex_ + 1 < static_cast<int>(buffers_.size()))
                bufferSelectorIndex_++;
            break;
        case EventType::InsertNewline: // Enter: abrir el buffer seleccionado
            activateBuffer(bufferSelectorIndex_);
            break;
        case EventType::Escape:
            // Se cancela el selector y se vuelve al buffer que estaba
            // activo antes de entrar (no se cambio nada).
            state_ = priorState_;
            statusMessage_ = "";
            break;
        // Cualquier otra tecla (i, j, k, a, c, x, p, Ctrl+K, ...) es no-op:
        // el selector es una pantalla modal y no deja filtrar nada.
        default:
            break;
    }
}

void Editor::run() {
    // La barra de estado ocupa las ultimas DOS filas: la fila fija (en
    // video inverso) y la fila de mensajes. El viewport usa el resto.
    // v0.6.3: el viewport es POR BUFFER, pero las dimensiones las fija
    // la terminal y valen para todos.
    for (Buffer& b : buffers_) {
        syncViewportSize(b);
    }

    terminal_.enableRawMode();

    // Cursor en forma de barra fina. Evita que el bloque de la terminal
    // ocupando la celda vacia tras la ultima palabra se vea como un
    // "espacio" extra al final de la linea. Lo restauramos al salir.
    write(STDOUT_FILENO, "\x1b[6 q", 5);

    // Primer render antes de esperar el primer evento.
    {
        Buffer& b = active();
        b.viewport.scrollToCursor(b.cursor);
        renderer_.renderScreen(b.document, b.cursor, b.viewport,
                               b.displayName(), b.modified, statusMessage_,
                               state_, b.selection);
    }

    while (running_) {
        Event event = terminal_.readEvent();
        handleEvent(event);

        if (!running_) break;

        Buffer& b = active();
        if (state_ == State::BufferSelector) {
            // Pantalla del selector: se dibuja la lista de buffers con la
            // barra MULTIBUFFER al final, manteniendo el aspecto del editor.
            renderer_.renderBufferList(bufferNames(), bufferSelectorIndex_,
                                       b.viewport.width, b.viewport.height);
        } else {
            b.viewport.scrollToCursor(b.cursor);
            renderer_.renderScreen(b.document, b.cursor, b.viewport,
                                   b.displayName(), b.modified, statusMessage_,
                                   state_, b.selection);
        }
    }

    terminal_.disableRawMode();
    // Limpiamos pantalla al salir para dejar la terminal prolija.
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[0 q", 12);
}

void Editor::handleEvent(const Event& event) {
    // En modo Prefix (tras Ctrl+K) todo pasa por handlePrefixKey: el
    // siguiente evento decide guardar/salir/operaciones de buffer/cancelar.
    if (state_ == State::Prefix) {
        handlePrefixKey(event);
        return;
    }

    // v0.6.3: en el selector de buffers solo se aceptan ↑/↓/Enter/ESC;
    // nada (ni siquiera Ctrl+K) se filtra al modo anterior.
    if (state_ == State::BufferSelector) {
        handleBufferSelectorEvent(event);
        return;
    }

    // v0.7: en el prompt "Guardar archivo:" solo se aceptan caracteres,
    // Backspace, Enter y ESC; nada se filtra a la edicion ni a otros modos.
    if (state_ == State::SaveAs) {
        handleSaveAsEvent(event);
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
        statusMessage_ = "Ctrl+K: s guardar | q salir | n nuevo | t buffers | w cerrar";
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
    Buffer& b = active();
    switch (event.type) {
        case EventType::InsertChar:
            // En navegacion no se escribe: las letras solo pueden ser
            // comandos de modo. 'i' entra a edicion; 's' a seleccion;
            // 'p' pega el contenido del buffer (si hay). 'c'/'x' y
            // cualquier otra letra son no-op aqui.
            if (event.text == "i") {
                state_ = State::Interaccion;
                statusMessage_ = "INTERACCION (ESC vuelve a navegacion)";
            } else if (event.text == "s") {
                beginSelection();
                state_ = State::Seleccion;
                statusMessage_ = "SELECCION (ESC/c/x terminan)";
            } else if (event.text == "p") {
                if (clipboard_.empty()) {
                    statusMessage_ = "Nada para pegar.";
                } else {
                    b.pushHistory();
                    Position end = b.document.insertBlock(b.cursor.line,
                                                          b.cursor.col,
                                                          clipboard_);
                    b.cursor.line = end.line;
                    b.cursor.col = end.col;
                    b.modified = true;
                    statusMessage_ = "Pegado.";
                }
            } else if (event.text == "j") {
                // j/k: salto por bloques (palabras), solo mueven el cursor.
                b.cursor.moveToPreviousWord(b.document);
            } else if (event.text == "k") {
                b.cursor.moveToNextWord(b.document);
            }
            break;

        // Movimientos libres, sin iniciar seleccion (a diferencia de
        // como Select extendia en v0.3-v0.4).
        case EventType::MoveLeft: b.cursor.moveLeft(b.document); break;
        case EventType::MoveRight: b.cursor.moveRight(b.document); break;
        case EventType::MoveUp: b.cursor.moveUp(b.document); break;
        case EventType::MoveDown: b.cursor.moveDown(b.document); break;
        case EventType::MoveHome: b.cursor.moveHome(); break;
        case EventType::MoveEnd: b.cursor.moveEnd(b.document); break;
        case EventType::PageUp: applyPage(-1); break;
        case EventType::PageDown: applyPage(+1); break;

        // InsertNewline/Backspace/Delete y Escape: no-op (no hay edicion
        // posible y ya estamos en navegacion, no hay a donde volver).
        default:
            break;
    }
}

void Editor::handleInteraccionEvent(const Event& event) {
    Buffer& b = active();
    switch (event.type) {
        case EventType::InsertChar:
            // Edicion libre real: cualquier letra (incluida i/s/p/c/x)
            // se inserta como texto. Aqui no son comandos de modo.
            b.pushHistory();
            b.document.insertText(b.cursor.line, b.cursor.col, event.text);
            b.cursor.col += static_cast<int>(event.text.size());
            b.modified = true;
            break;

        case EventType::InsertNewline:
            b.pushHistory();
            b.document.insertNewline(b.cursor.line, b.cursor.col);
            b.cursor.line++;
            b.cursor.col = 0;
            b.modified = true;
            break;

        case EventType::Backspace:
        case EventType::Delete: {
            b.pushHistory();
            if (event.type == EventType::Backspace) {
                bool willMergeLines = (b.cursor.col == 0 && b.cursor.line > 0);
                int prevLineLen = willMergeLines
                    ? b.document.lineLength(b.cursor.line - 1) : 0;

                // deleteCharBefore devuelve cuantos bytes borro dentro de la
                // linea (el largo del caracter UTF-8 completo), asi el cursor
                // se reposiciona sin recalcular el limite del caracter aqui.
                int deleted = b.document.deleteCharBefore(b.cursor.line,
                                                          b.cursor.col);
                if (deleted > 0 || willMergeLines) {
                    if (willMergeLines) {
                        b.cursor.line--;
                        b.cursor.col = prevLineLen;
                    } else {
                        b.cursor.col -= deleted;
                    }
                    b.modified = true;
                }
            } else if (b.document.deleteCharAt(b.cursor.line, b.cursor.col)) {
                b.modified = true;
            }
            break;
        }

        case EventType::Escape:
            state_ = State::Navegacion;
            statusMessage_ = "NAVEGACION";
            break;

        case EventType::MoveLeft: b.cursor.moveLeft(b.document); break;
        case EventType::MoveRight: b.cursor.moveRight(b.document); break;
        case EventType::MoveUp: b.cursor.moveUp(b.document); break;
        case EventType::MoveDown: b.cursor.moveDown(b.document); break;
        case EventType::MoveHome: b.cursor.moveHome(); break;
        case EventType::MoveEnd: b.cursor.moveEnd(b.document); break;
        case EventType::PageUp: applyPage(-1); break;
        case EventType::PageDown: applyPage(+1); break;

        default:
            break;
    }
}

void Editor::handleSeleccionEvent(const Event& event) {
    Buffer& b = active();
    // Si el prefijo 'a' (seleccion total) esta activo, casi todos los
    // eventos se interpretan de forma especial, no como extension normal
    // de la seleccion.
    if (b.selectAllActive) {
        handleSelectAllEvent(event);
        return;
    }

    switch (event.type) {
        // Los movimientos extienden la seleccion, igual que hacia v0.3-v0.4.
        case EventType::MoveLeft:
            beginSelection(); b.cursor.moveLeft(b.document); updateSelectionPosition(); break;
        case EventType::MoveRight:
            beginSelection(); b.cursor.moveRight(b.document); updateSelectionPosition(); break;
        case EventType::MoveUp:
            beginSelection(); b.cursor.moveUp(b.document); updateSelectionPosition(); break;
        case EventType::MoveDown:
            beginSelection(); b.cursor.moveDown(b.document); updateSelectionPosition(); break;
        case EventType::MoveHome:
            beginSelection(); b.cursor.moveHome(); updateSelectionPosition(); break;
        case EventType::MoveEnd:
            beginSelection(); b.cursor.moveEnd(b.document); updateSelectionPosition(); break;
        // RePag/AvPag extienden la seleccion como una flecha (Up/Down):
        // el anchor permanece y el cursor salta una pagina.
        case EventType::PageUp:
            beginSelection(); applyPage(-1); updateSelectionPosition(); break;
        case EventType::PageDown:
            beginSelection(); applyPage(+1); updateSelectionPosition(); break;

        // 'c' copia el rango al buffer y 'x' lo copia y lo borra; ambos
        // terminan la seleccion y vuelven a navegacion. Si la seleccion
        // esta vacia, 'c'/'x' no tocan el buffer (solo se sale del modo).
        // OJO: el gate es hasSelection() (anchor != position), NO
        // selection().has_value() (que es true incluso para un objeto
        // Selection vacio con anchor == position, ya que normalize solo
        // ordena los puntos). Usar el segundo sobreescribiria el buffer con
        // un rango vacio y, en 'x', empujaria un historial inutil que ademas
        // limpia el redo.
        case EventType::InsertChar:
            // 'a' entra al prefijo de "seleccion total": cubre el archivo
            // entero sin mover el cursor (guarda la seleccion previa para el
            // toggle). 'c' copia el rango al buffer y 'x' lo copia y lo borra;
            // ambos terminan la seleccion y vuelven a navegacion. Si la
            // seleccion esta vacia, 'c'/'x' no tocan el buffer (solo se sale
            // del modo).
            // OJO: el gate es hasSelection() (anchor != position), NO
            // selection().has_value() (que es true incluso para un objeto
            // Selection vacio con anchor == position, ya que normalize solo
            // ordena los puntos). Usar el segundo sobreescribiria el buffer con
            // un rango vacio y, en 'x', empujaria un historial inutil que ademas
            // limpia el redo.
            if (event.text == "a") {
                b.selectAllPrevious = b.selection;
                b.selection = selectAllSelection();
                b.selectAllActive = true;
                statusMessage_ = "SELECCION TOTAL (a togglea | flechas a extremos | c/x/ESC terminan)";
            } else if (event.text == "j" || event.text == "k") {
                // j/k extienden la seleccion igual que una flecha: el anchor
                // permanece y solo se mueve el cursor. 'j' va a la izquierda
                // (bloque anterior) y 'k' a la derecha (sig. bloque).
                beginSelection();
                if (event.text == "j") b.cursor.moveToPreviousWord(b.document);
                else b.cursor.moveToNextWord(b.document);
                updateSelectionPosition();
            } else if (event.text == "c" || event.text == "x") {
                bool hadSelection = hasSelection();
                if (hadSelection) {
                    auto sel = selection();
                    clipboard_ = b.document.extractRange(sel->start.line,
                                                         sel->start.col,
                                                         sel->end.line,
                                                         sel->end.col);
                    if (event.text == "x") {
                        b.pushHistory();
                        b.document.deleteRange(sel->start.line, sel->start.col,
                                               sel->end.line, sel->end.col);
                        b.cursor.line = sel->start.line;
                        b.cursor.col = sel->start.col;
                        b.modified = true;
                    }
                }
                clearSelection();
                state_ = State::Navegacion;
                statusMessage_ = hadSelection ? (event.text == "x" ? "Cortado."
                                                                    : "Copiado.")
                                              : "Nada seleccionado.";
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

// Prefijo 'a' (seleccion total): interpreta los eventos mientras
// selectAllActive es true. La seleccion entera ya esta puesta en
// selection ([BOF, EOF]); aqui se decide que hace cada tecla con ella.
void Editor::handleSelectAllEvent(const Event& event) {
    Buffer& b = active();
    switch (event.type) {
        // 'a' de nuevo: toggle -> volvemos a la seleccion previa (o, si la
        // previa era sin seleccion, a sin seleccion) y se desactiva el modo.
        case EventType::InsertChar:
            if (event.text == "a") {
                b.selection = b.selectAllPrevious;
                b.selectAllPrevious.reset();
                if (!b.selection.has_value()) clearSelection();
                b.selectAllActive = false;
                statusMessage_ = "SELECCION";
            } else if (event.text == "c" || event.text == "x") {
                // c/x operan sobre el archivo entero (la seleccion total es
                // una seleccion real): copiar copia todo; cortar borra todo.
                bool hadSelection = hasSelection();
                if (hadSelection) {
                    auto sel = selection();
                    clipboard_ = b.document.extractRange(sel->start.line,
                                                         sel->start.col,
                                                         sel->end.line,
                                                         sel->end.col);
                    if (event.text == "x") {
                        b.pushHistory();
                        b.document.deleteRange(sel->start.line, sel->start.col,
                                               sel->end.line, sel->end.col);
                        b.cursor.line = sel->start.line;
                        b.cursor.col = sel->start.col;
                        b.modified = true;
                    }
                }
                clearSelection();
                b.selectAllPrevious.reset();
                b.selectAllActive = false;
                state_ = State::Navegacion;
                statusMessage_ = hadSelection ? (event.text == "x" ? "Cortado."
                                                                    : "Copiado.")
                                              : "Nada seleccionado.";
            }
            // Cualquier otra letra: no pasa nada.
            break;

        // Flechas: saltan a los extremos, terminan la seleccion total y
        // dejan el cursor en el extremo (anchor == cursor, sin seleccion).
        case EventType::MoveRight:
        case EventType::MoveDown: {
            int last = b.document.lineCount() - 1;
            b.cursor.line = last;
            b.cursor.col = b.document.lineLength(last);
            b.selection = Selection{{b.cursor.line, b.cursor.col},
                                    {b.cursor.line, b.cursor.col}};
            b.selectAllActive = false;
            b.selectAllPrevious.reset();
            statusMessage_ = "SELECCION";
            break;
        }
        case EventType::MoveLeft:
        case EventType::MoveUp:
            b.cursor.line = 0;
            b.cursor.col = 0;
            b.selection = Selection{{0, 0}, {0, 0}};
            b.selectAllActive = false;
            b.selectAllPrevious.reset();
            statusMessage_ = "SELECCION";
            break;

        // ESC: cancela la seleccion total (y toda seleccion) y vuelve a
        // navegacion, igual que en el modo Seleccion normal.
        case EventType::Escape:
            clearSelection();
            b.selectAllPrevious.reset();
            b.selectAllActive = false;
            state_ = State::Navegacion;
            statusMessage_ = "Seleccion cancelada.";
            break;

        // Resto (incl. teclas desconocidas): no-op.
        default:
            break;
    }
}

std::optional<Selection> Editor::selectAllSelection() const {
    const Buffer& b = active();
    int last = b.document.lineCount() - 1;
    Position end{last, b.document.lineLength(last)};
    return Selection{{0, 0}, end};
}

void Editor::handlePrefixKey(const Event& event) {
    switch (event.type) {
        case EventType::Save: // Ctrl+S tras Ctrl+K = guardar archivo
            if (active().filename.empty()) {
                // v0.7: un buffer sin nombre (p.ej. creado con Ctrl+K n)
                // no se puede guardar tal cual: se abre el prompt "Guardar
                // archivo:" para elegir la ruta de destino.
                startSaveAs();
                break;
            }
            save();
            state_ = priorState_;
            break;

        case EventType::Quit:
            running_ = false;
            break;

        // v0.6.3: comandos de buffer dentro del prefijo.
        case EventType::InsertChar:
            if (event.text == "n") {           // Ctrl+K n: nuevo buffer
                createBuffer();
                state_ = State::Navegacion;
                break;
            }
            if (event.text == "t") {           // Ctrl+K t: selector de buffers
                if (buffers_.size() <= 1) {
                    statusMessage_ = "Solo hay un buffer.";
                    state_ = priorState_;
                } else {
                    bufferSelectorIndex_ = activeBuffer_;
                    state_ = State::BufferSelector;
                    statusMessage_ = "Buffers: ↑/↓ mover | Enter abrir | ESC cancelar";
                }
                break;
            }
            if (event.text == "w") {           // Ctrl+K w: cerrar buffer activo
                closeActiveBuffer();
                break;
            }
            // Cualquier otra letra: cae en el cancel del default.
            state_ = priorState_;
            statusMessage_ = "Comando cancelado.";
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

void Editor::startSaveAs() {
    // Ctrl+K Ctrl+S sobre un buffer sin nombre: en vez de fallar, se abre
    // el prompt "Guardar archivo:". El usuario escribe la ruta en la fila
    // de mensajes; Enter confirma, ESC cancela. priorState_ ya guarda el
    // modo desde el que se abrio el prefijo (aqui no se toca).
    saveAsPath_.clear();
    state_ = State::SaveAs;
    statusMessage_ = "Guardar archivo: ";
}

void Editor::handleSaveAsEvent(const Event& event) {
    switch (event.type) {
        case EventType::InsertChar:
            saveAsPath_ += event.text;
            statusMessage_ = "Guardar archivo: " + saveAsPath_;
            break;
        case EventType::Backspace:
            if (!saveAsPath_.empty()) {
                int cols = utf8::columnOf(saveAsPath_,
                                          static_cast<int>(saveAsPath_.size()));
                saveAsPath_ = utf8::truncate(saveAsPath_, cols - 1);
            }
            statusMessage_ = "Guardar archivo: " + saveAsPath_;
            break;
        case EventType::InsertNewline: // Enter: guardar en la ruta escrita
            commitSaveAs();
            break;
        case EventType::Escape:
            state_ = priorState_;
            statusMessage_ = "Guardado cancelado.";
            break;
        default:
            // Cualquier otra tecla es no-op: el prompt es una pantalla
            // modal y no deja filtrar nada (ni Ctrl+K, ni flechas, ...).
            break;
    }
}

void Editor::commitSaveAs() {
    Buffer& b = active();
    const std::string path = resolveAbsolutePath(saveAsPath_);
    if (path.empty()) {
        // Sin ruta escrita: se sigue en el prompt, esperando un nombre.
        statusMessage_ = "Guardar archivo: ";
        return;
    }
    if (isDirectory(path)) {
        statusMessage_ = "Es una carpeta: " + path;
        return;
    }
    if (b.document.saveToFile(path)) {
        b.filename = path;
        b.modified = false;
        b.savedLines = b.document.snapshot();
        statusMessage_ = "Guardado: " + path;
        state_ = priorState_;
    } else {
        statusMessage_ = "Error al guardar: " + path;
    }
}

bool Editor::hasSelection() const {
    const Buffer& b = active();
    return b.selection.has_value() && b.selection->anchor != b.selection->position;
}

std::optional<Normalized> Editor::selection() const {
    const Buffer& b = active();
    if (!b.selection.has_value()) return std::nullopt;
    return normalize(*b.selection);
}

void Editor::beginSelection() {
    Buffer& b = active();
    if (!b.selection.has_value()) {
        b.selection = Selection{};
        b.selection->anchor = {b.cursor.line, b.cursor.col};
        b.selection->position = {b.cursor.line, b.cursor.col};
    }
}

void Editor::updateSelectionPosition() {
    Buffer& b = active();
    if (b.selection.has_value()) {
        b.selection->position = {b.cursor.line, b.cursor.col};
    }
}

void Editor::applyPage(int dir) {
    Buffer& b = active();
    const int count = b.document.lineCount(); // >= 1: siempre hay una linea
    const int h = b.viewport.height;

    // Archivo pequeno: cabe entero en una pagina. RePag -> inicio, AvPag
    // -> final, sin nunca quedar fuera del documento.
    if (h >= count) {
        b.viewport.top = 0;
        b.cursor.line = (dir < 0) ? 0 : count - 1;
        b.cursor.clampToLine(b.document);
        return;
    }

    // Conservamos la posicion relativa del cursor dentro del viewport: el
    // viewport y el cursor se desplazan la misma cantidad. En los bordes
    // el viewport se clampa para que nunca muestre mas alla del documento:
    //   - Arriba:  top >= 0 (la primera linea visible es la del archivo).
    //   - Abajo:   top <= maxTop (la ultima fila visible llega a EOF).
    const int rel = b.cursor.line - b.viewport.top; // posicion relativa (0..h-1)
    const int maxTop = count - h;
    if (dir < 0) {
        b.viewport.top = std::max(0, b.viewport.top - h);
    } else {
        b.viewport.top = std::min(maxTop, b.viewport.top + h);
    }
    b.cursor.line = b.viewport.top + rel;
    b.cursor.line = std::min(std::max(b.cursor.line, 0), count - 1);
    b.cursor.clampToLine(b.document);
}

void Editor::clearSelection() {
    Buffer& b = active();
    b.selection.reset();
}

void Editor::save() {
    Buffer& b = active();
    // v0.7: save() solo se invoca para buffers CON nombre (handlePrefixKey
    // desvia los sin nombre al prompt SaveAs). El guard sigue aqui como
    // invariante defensivo: un buffer sin nombre no tiene a donde guardar.
    if (b.filename.empty()) {
        statusMessage_ = "Archivo sin nombre: usa Ctrl+K Ctrl+S para elegir ruta.";
        return;
    }
    if (b.document.saveToFile(b.filename)) {
        b.modified = false;
        b.savedLines = b.document.snapshot();
        statusMessage_ = "Guardado.";
    } else {
        statusMessage_ = "Error al guardar.";
    }
}

void Editor::undo() {
    Buffer& b = active();
    if (b.undoStack.empty()) {
        statusMessage_ = "Nada que deshacer.";
        return;
    }

    // Guardamos el estado actual para poder rehacer.
    HistoryState current;
    current.lines = b.document.snapshot();
    current.line = b.cursor.line;
    current.col = b.cursor.col;
    current.selection = b.selection;
    b.redoStack.push_back(current);

    b.applyState(b.undoStack.back());
    b.undoStack.pop_back();
    // La seleccion restaurada vuelve a estar VIGENTE. Importante: el modo
    // Seleccion solo debe activarse si el rango restaurado es realmente NO
    // vacio. Compartimos el criterio con hasSelection() (anchor != position).
    state_ = (b.selection.has_value() && b.selection->anchor != b.selection->position)
           ? State::Seleccion
           : State::Navegacion;
    statusMessage_ = "Deshecho.";
}

void Editor::redo() {
    Buffer& b = active();
    if (b.redoStack.empty()) {
        statusMessage_ = "Nada que rehacer.";
        return;
    }

    // Guardamos el estado actual en el historial de deshacer.
    HistoryState current;
    current.lines = b.document.snapshot();
    current.line = b.cursor.line;
    current.col = b.cursor.col;
    current.selection = b.selection;
    b.undoStack.push_back(current);

    b.applyState(b.redoStack.back());
    b.redoStack.pop_back();
    state_ = (b.selection.has_value() && b.selection->anchor != b.selection->position)
           ? State::Seleccion
           : State::Navegacion;
    statusMessage_ = "Rehecho.";
}
