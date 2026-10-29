#include "app/Editor.h"
#include "app/EditorHandlersCommon.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <cerrno>
#include <sys/stat.h>

#include "base/utf8.h"
#include "app/ChromePresentation.h"
#include "layout/Gutter.h"
#include "layout/ScreenToCursor.h"
#include "syntax/SyntaxHighlighter.h"
#include "platform/clipboard/ClipboardFactory.h"
#include "filesystem/FileWatcherFactory.h"
#include "rendering/NullScreenRenderer.h"

using namespace editor_internal;

void Editor::handleFileChange(const FileChangeEvent& ev) {
    bool any = false;
    for (int i = 0; i < buffers.count(); ++i) {
        Buffer& b = buffers.at(i);
        if (b.filename != ev.path) continue;
        auto cur = captureIdentity(b.filename);
        if (cur == b.savedIdentity) continue;
        if (ev.kind == FileChangeKind::Deleted) {
            if (b.modified) {
                setActionMessage("¡ALERTA! '" + b.filename + "' cambió en disco. Tus cambios locales tienen prioridad.", MessageKind::Warning);
            } else {
                if (!cur.valid) {
                    setActionMessage("El archivo '" + b.filename + "' fue eliminado del disco.", MessageKind::Error);
                    b.savedIdentity = Buffer::FileIdentity{};
                } else {
                    watchFile(b.filename);
                    LoadResult res = b.document.loadFromFile(b.filename);
                    if (res == LoadResult::Success) {
                        b.syncSavedState();
                        b.savedIdentity = captureIdentity(b.filename);
                        if (b.cursor.line >= b.document.lineCount()) b.cursor.line = b.document.lineCount() - 1;
                        if (b.cursor.line < 0) b.cursor.line = 0;
                        b.cursor.clampToLine(b.document);
                        invalidateWrapIndex();
                        resetPreferredVisualColumn();
                        setActionMessage("Archivo recargado desde disco.", MessageKind::Success);
                    } else if (res == LoadResult::NotFound) {
                        setActionMessage("El archivo '" + b.filename + "' fue eliminado del disco.", MessageKind::Error);
                        b.savedIdentity = Buffer::FileIdentity{};
                    } else if (res == LoadResult::PermissionDenied) {
                        setActionMessage("Sin permisos de lectura: " + b.filename, MessageKind::Error);
                    } else {
                        setActionMessage("No se pudo recargar: " + b.filename, MessageKind::Error);
                    }
                }
            }
            any = true;
            continue;
        }
        if (ev.kind == FileChangeKind::Created) {
            watchFile(b.filename);
        }
        if (b.modified) {
            setActionMessage("¡ALERTA! '" + b.filename + "' cambió en disco. Tus cambios locales tienen prioridad.", MessageKind::Warning);
            any = true;
            continue;
        }
        LoadResult res = b.document.loadFromFile(b.filename);
        if (res == LoadResult::Success) {
            b.syncSavedState();
            b.savedIdentity = captureIdentity(b.filename);
            if (b.cursor.line >= b.document.lineCount()) b.cursor.line = b.document.lineCount() - 1;
            if (b.cursor.line < 0) b.cursor.line = 0;
            b.cursor.clampToLine(b.document);
            invalidateWrapIndex();
            resetPreferredVisualColumn();
            setActionMessage("Archivo recargado desde disco.", MessageKind::Success);
        } else if (res == LoadResult::NotFound) {
            setActionMessage("El archivo '" + b.filename + "' fue eliminado del disco.", MessageKind::Error);
            b.savedIdentity = Buffer::FileIdentity{};
        } else if (res == LoadResult::PermissionDenied) {
            setActionMessage("Sin permisos de lectura: " + b.filename, MessageKind::Error);
        } else {
            setActionMessage("No se pudo recargar: " + b.filename, MessageKind::Error);
        }
        any = true;
    }
    if (any) renderFrame();
}

void Editor::handleBufferSelectorEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::MoveUp:
            if (bufferSelectorIndex_ > 0) bufferSelectorIndex_--;
            break;
        case InputEventType::MoveDown:
            if (bufferSelectorIndex_ + 1 < buffers.count())
                bufferSelectorIndex_++;
            break;
        case InputEventType::ScrollUp:
            if (bufferSelectorIndex_ > 0) bufferSelectorIndex_--;
            break;
        case InputEventType::ScrollDown:
            if (bufferSelectorIndex_ + 1 < buffers.count())
                bufferSelectorIndex_++;
            break;
        case InputEventType::InsertNewline: // Enter: abrir el buffer seleccionado
            activateBufferAtMruPosition(bufferSelectorIndex_);
            break;
        case InputEventType::Escape:
            // Se cancela el selector y se vuelve al buffer que estaba
            // activo antes de entrar (no se cambio nada).
            state_ = priorState_;
            setStatusMessage("");
            break;
        // Cualquier otra tecla (i, j, k, a, c, x, p, Ctrl+K, ...) es no-op:
        // el selector es una pantalla modal y no deja filtrar nada.
        default:
            break;
    }
}

void Editor::startFileBrowser() {
    const std::string& fn = active().filename;
    if (!fn.empty()) {
        const std::filesystem::path dir =
            std::filesystem::path(fn).parent_path();
        if (!dir.empty() && FileBrowser::isDirectory(dir.string())) {
            fileBrowser.startAt(dir.string());
        } else {
            fileBrowser.start();
        }
    } else {
        fileBrowser.start();
    }
    const std::string err = fileBrowser.reload();
    if (!err.empty()) {
        setActionMessage(err, MessageKind::Error);
    } else {
        //setStatusMessage("MOVER: ↑/↓ | ABRIR: enter | CANCEL: esc");
        setStatusMessage(kHelpBufferSelector);
    }
    state_ = State::FileBrowser;
}

void Editor::fileBrowserEnterSelected() {
    switch (fileBrowser.enter()) {
        case FileBrowser::EnterResult::None:
            break;
        case FileBrowser::EnterResult::EnteredDirectory: {
            const std::string err = fileBrowser.reload();
            if (!err.empty()) {
                setActionMessage(err, MessageKind::Error);
            } else {
                //setStatusMessage("MOVER: ↑/↓ | ABRIR: enter | CANCEL: esc");
                setStatusMessage(kHelpBufferSelector);
            }
            break;
        }
        case FileBrowser::EnterResult::OpenedFile:
            openFileInBuffer(fileBrowser.pendingPath());
            break;
    }
}

void Editor::fileBrowserEnterDirectoryOnly() {
    switch (fileBrowser.enterDirectoryOnly()) {
        case FileBrowser::EnterResult::None:
            break;  // archivo o lista vacia: no-op, se sigue en SaveAsFileBrowser
        case FileBrowser::EnterResult::EnteredDirectory: {
            const std::string err = fileBrowser.reload();
            if (!err.empty()) {
                setActionMessage(err, MessageKind::Error);
            }
            break;
        }
        case FileBrowser::EnterResult::OpenedFile:
            break;  // inalcanzable: enterDirectoryOnly() nunca lo devuelve
    }
}

void Editor::openFileInBuffer(const std::string& path) {
    // Normalizar la ruta ANTES de comparar y guardar, para que el chequeo
    // de duplicados funcione aunque dos rutas escriban el mismo archivo de
    // forma distinta ("foo/../bar" == "bar", "." y "..", etc).
    const std::string filePath = resolveAbsolutePath(path);

    // Si ya hay un buffer con esta ruta, se activa ese buffer en vez de
    // crear otro (v0.6.4: no duplicar archivos abiertos).
    for (int i = 0; i < buffers.count(); ++i) {
        if (buffers.at(i).filename == filePath) {
            activateBuffer(i);
            return;
        }
    }

    // Buffer nuevo. NO pasa por createBuffer(): un archivo abierto no es un
    // buffer anonimo, asi que no debe consumir un nombre "SinNombre" del
    // contador (v0.6.4, invitado 2).
    Buffer nuevo;
    syncViewportSize(nuevo);
    nuevo.filename = filePath;
    LoadResult result = nuevo.document.loadFromFile(filePath);
    if (result != LoadResult::Success && result != LoadResult::NotFound) {
        // Error real (permisos, E/S): no se crea ni se toca nada.
        setActionMessage((result == LoadResult::PermissionDenied)
                             ? "Sin permisos de lectura: " + filePath
                             : "No se pudo leer: " + filePath,
                         MessageKind::Error);
        return;
    }
    nuevo.syncSavedState();
    nuevo.savedIdentity = (result == LoadResult::Success) ? captureIdentity(filePath) : Buffer::FileIdentity{};
    nuevo.cursor.line = 0;
    nuevo.cursor.col = 0;
    invalidateWrapIndex();
    resetPreferredVisualColumn();
    nuevo.selection.reset();
    nuevo.selectAllActive = false;
    nuevo.selectAllPrevious.reset();
    watchFile(nuevo.filename);
    // push() deja el buffer nuevo activo y al frente de la MRU.
    buffers.push(std::move(nuevo));
    state_ = State::Navegacion;
    renderer_->invalidateCache();
    if (result == LoadResult::Success) {
        setStatusMessage("");
    } else {
        setActionMessage("Archivo nuevo: " + path, MessageKind::Success);
    }
}

void Editor::handleFileBrowserEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::PageUp:
            fileBrowser.movePage(-1, active().viewport.height);
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::PageDown:
            fileBrowser.movePage(+1, active().viewport.height);
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::MoveUp:
            fileBrowser.moveUp();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::MoveDown:
            fileBrowser.moveDown();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::MoveRight: {
            if (fileBrowser.entries_.size() > 0 && fileBrowser.index_ < static_cast<int>(fileBrowser.entries_.size())) {
                const FileBrowserEntry& e = fileBrowser.entries_[static_cast<size_t>(fileBrowser.index_)];
                if (e.isDirectory  && e.name != "..") {
                    fileBrowserEnterDirectoryOnly();
                    const std::string err = fileBrowser.reload();
                    if (!err.empty()) {
                        setActionMessage(err, MessageKind::Error);
                    } else {
                        setStatusMessage(kHelpBufferSelector);
                    }
                }
            }
            break;
        }
        case InputEventType::MoveLeft: {
            if (fileBrowser.entries_.size() > 0 && fileBrowser.index_ < static_cast<int>(fileBrowser.entries_.size())) {
                const FileBrowserEntry& e = fileBrowser.entries_[static_cast<size_t>(fileBrowser.index_)];
                if (e.name == "..") {
                    fileBrowser.goUp();
                    const std::string err = fileBrowser.reload();
                    if (!err.empty()) {
                        setActionMessage(err, MessageKind::Error);
                    } else {
                        setStatusMessage(kHelpBufferSelector);
                    }
                }
            }
            break;
        }
        case InputEventType::ScrollUp:
            fileBrowser.moveUp();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::ScrollDown:
            fileBrowser.moveDown();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::InsertNewline: // Enter: abrir/entrar la seleccion
            fileBrowserEnterSelected();
            break;
        case InputEventType::Escape:
        case InputEventType::Prefix: // Ctrl+K dentro tambien cancela (modal puro)
            state_ = priorState_;
            setStatusMessage("");
            break;
        // Cualquier otra tecla es no-op: pantalla modal, nada se filtra.
        default:
            break;
    }
}

void Editor::handleMousePress(const InputEvent& event) {
    if (state_ != State::Navegacion && state_ != State::Interaccion &&
        state_ != State::Seleccion) {
        return;
    }
    Buffer& b = active();
    const Layout layout =
        computeLayout(b.viewport.height + kChromeRows, b.viewport.width);

    // El chrome ocupa el rectangulo layout.chrome (filas contiguas justo
    // despues del contenido, exactamente kChromeRows filas segun
    // Layout::computeLayout). Identificarlo por su rectangulo real, no
    // deducido de content.height, para no romper si el layout cambia.
    const bool chromeClick = (event.cell.row >= layout.chrome.row &&
                              event.cell.row < layout.chrome.row + layout.chrome.height);

    if (chromeClick) {
        // El chrome (StatusBar / MessageBar) es puramente informativo: los
        // clicks no producen ninguna accion. No se mueve el cursor, no se
        // toca la seleccion, no se arma el gesto de mouse.
        return;
    }

    std::optional<Position> pos;
    if (isWrapEnabledFor(b)) {
        const softwrap::Index& idx = wrapFor(this, b);
        int gutterW = gutterWidth(b.document.lineCount(), layout.content.width);
        pos = screenToCursorWrap(idx, event.cell, layout, b.viewport, b.document, gutterW);
    } else {
        pos = screenToCursor(event.cell, layout, b.viewport, b.document);
    }

    if (!pos.has_value()) {
        if (state_ == State::Seleccion) {
            clearSelection();
            b.selectAllActive = false;
            state_ = State::Navegacion;
        }
        mouseGestureActive_ = false;
        mouseDragStarted_ = false;
        dragAnchor_.reset();
        mouseAutoscrollDirection_ = MouseAutoscrollDirection::None;
        return;
    }
    // Press VALIDO: si venia de Seleccion, el click para ir a otra
    // posicion cancela el highlight EN EL PRESS (sin esperar al release).
    // Luego se arma el gesto normal: el drag posterior crea la nueva
    // seleccion desde dragAnchor_, el release sin drag es no-op.
    if (state_ == State::Seleccion) {
        clearSelection();
        b.selectAllActive = false;
        state_ = State::Navegacion;
    }
    // Press ARMADO: mueve el cursor y guarda el anchor, SIN crear rango.
    // La decision click-vs-drag sigue en release (no-op) / drag (nueva
    // seleccion).
    b.cursor.line = pos->line;
    b.cursor.col = pos->col;
    b.cursor.clampToLine(b.document);
    resetPreferredVisualColumn();
    dragAnchor_ = *pos;
    pressState_ = state_;
    mouseGestureActive_ = true;
    mouseDragStarted_ = false;
    mouseAutoscrollDirection_ = MouseAutoscrollDirection::None;
}

void Editor::handleMouseDrag(const InputEvent& event) {
    if (state_ != State::Navegacion && state_ != State::Interaccion &&
        state_ != State::Seleccion) {
        return;
    }
    if (!mouseGestureActive_) return;
    if (!dragAnchor_.has_value()) return;
    auto pos = resolveMouseDragPosition(event.cell);
    if (!pos.has_value()) return;
    applyMouseDragPosition(*pos);
}

void Editor::handleMouseRelease(const InputEvent& event) {
    (void)event;
    if (!mouseGestureActive_) return;
    if (state_ != State::Navegacion && state_ != State::Interaccion &&
        state_ != State::Seleccion) {
        mouseGestureActive_ = false;
        mouseDragStarted_ = false;
        mouseAutoscrollDirection_ = MouseAutoscrollDirection::None;
        return;
    }
    if (!mouseDragStarted_) {
        // Click simple: conducta historica diferida al release. El cursor
        // ya quedo en la pos del press; en Seleccion se cancela a
        // Navegacion, fuera de ella no hay cambio de modo.
        if (pressState_ == State::Seleccion) {
            Buffer& b = active();
            clearSelection();
            b.selectAllActive = false;
            state_ = State::Navegacion;
        }
    }
    // Con drag previo se permanece en Seleccion sin mas cambios.
    // Cierre del gesto: el autoscroll temporal se detiene (release fuera
    // tambien lo apaga).
    mouseGestureActive_ = false;
    mouseDragStarted_ = false;
    mouseAutoscrollDirection_ = MouseAutoscrollDirection::None;
}

void Editor::handleEvent(const InputEvent& event) {
    // Suspensión (Ctrl+Z): global, en cualquier modo (incluidos los
    // modales). Solo marca la petición; el loop decide y ejecuta.
    if (event.type == InputEventType::Suspend) {
        suspendRequested_ = true;
        return;
    }
    if (event.type == InputEventType::Resize) {
        resize(event.resizeRows, event.resizeCols);
        return;
    }
    if (event.type == InputEventType::MousePress) {
        handleMousePress(event);
        return;
    }
    if (event.type == InputEventType::MouseDrag) {
        handleMouseDrag(event);
        return;
    }
    if (event.type == InputEventType::MouseRelease) {
        handleMouseRelease(event);
        return;
    }
    // v0.6.4: el explorador es modal y se despacha ANTES del prefijo: un
    // Ctrl+K dentro no abre un nuevo prefijo sino que cancela el explorador.
    if (state_ == State::FileBrowser) {
        handleFileBrowserEvent(event);
        return;
    }

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

    if (state_ == State::Busqueda) {
        handleBusquedaEvent(event);
        return;
    }

    // v0.9: explorador para "Guardar como" - navegacion + input nombre + Ctrl+S
    if (state_ == State::SaveAsFileBrowser) {
        handleSaveAsFileBrowserEvent(event);
        return;
    }

    // Renombrar es modal puro como Busqueda/SaveAsFileBrowser: se despacha
    // ANTES del bloque global (Undo/Redo/Prefix) para que esas teclas no
    // se filtren al documento ni aniden un prefijo dentro del prompt.
    if (state_ == State::Renombrar) {
        handleRenameEvent(event);
        return;
    }

    // Undo/Redo y la entrada al prefijo estan disponibles en los 3 modos
    // y no dependen de state_, asi que se evaluan ANTES del despacho por
    // modo. Nota: Ctrl+S (Save) SOLO tiene efecto tras el prefijo (lo
    // consume handlePrefixKey). Sin prefijo llega aqui y cae como no-op
    // en cada modo: Ctrl+S deja de tener significado especial.
    if (event.type == InputEventType::Undo) { commands_.execute("edicion.deshacer"); return; }
    if (event.type == InputEventType::Redo) { commands_.execute("edicion.rehacer"); return; }
    // Ctrl+Shift+S (GUI, SaveAs): atajo directo a `buffer.guardar.como`
    // sin pasar por Prefix (vía comando semántico existente, nunca
    // sintetizando Prefix+tecla TTY). Solo en modos de edición
    // (Navegacion/Interaccion/Seleccion). Política explícita por estado
    // (probada en test_guikeymap.cpp):
    //   - FileBrowser/BufferSelector/Busqueda/IrAFila/Renombrar: no-op
    //     (rama default de su handler: no confirma nombres, no cancela la
    //     operación, no pierde el input del prompt), igual que Save fuera
    //     de prefijo.
    //   - Prefix: equivale a Save (abre el explorador SaveAs).
    //   - SaveAsFileBrowser: confirma igual que Save (con nombre vacío
    //     avisa y conserva el estado, sin confirmar nada).
    if (event.type == InputEventType::SaveAs &&
        (state_ == State::Navegacion || state_ == State::Interaccion ||
         state_ == State::Seleccion)) {
        commands_.execute("buffer.guardar.como");
        return;
    }
    if (event.type == InputEventType::Prefix) {
        priorState_ = state_;
        state_ = State::Prefix;
        setStatusMessage(kHelpPrefix);
        return;
    }

    // El grupo de escritura coalescente solo sobrevive mientras siga
    // habiendo InsertChar consecutivos dentro de Interaccion tras haber
    // empezado el grupo con un reemplazo de seleccion. Cualquier otro
    // evento lo sella: la proxima escritura empieza un grupo nuevo (por
    // caracter, si es escritura normal).
    if (!(state_ == State::Interaccion &&
          event.type == InputEventType::InsertChar && coalescingTyping_)) {
        coalescingTyping_ = false;
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
        case State::IrAFila:
            handleIrAFilaEvent(event);
            break;
        default:
            break;
    }
}

void Editor::handleNavegacionEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::Paste:
            commands_.execute("navegacion.pegar");
            break;
        case InputEventType::InsertChar:
            // En navegacion no se escribe: las letras solo pueden ser
            // comandos de modo. 'i' entra a edicion; 's' a seleccion;
            // 'p' pega el contenido del buffer (si hay). 'c'/'x' y
            // cualquier otra letra son no-op aqui. El mapeo tecla ->
            // comando -> handler vive en commands_.
            if (event.text == "i") {
                commands_.execute("navegacion.interaccion");
            } else if (event.text == "s") {
                commands_.execute("navegacion.seleccion");
            } else if (event.text == "p") {
                commands_.execute("navegacion.pegar");
            } else if (event.text == "j") {
                commands_.execute("navegacion.palabra.atras");
            } else if (event.text == "k") {
                commands_.execute("navegacion.palabra.adelante");
            } else if (event.text == "a") {
                commands_.execute("seleccion.total");
            } else if (event.text == "g") {
                commands_.execute("navegacion.ir_a_fila");
            } else if (event.text == "f") {
                commands_.execute("navegacion.buscar");
            } else if (event.text == "}") {
                commands_.execute("navegacion.indentar");
            } else if (event.text == "{") {
                commands_.execute("navegacion.desindentar");
            }
            break;

        // Movimientos libres, sin iniciar seleccion (a diferencia de
        // como Select extendia en v0.3-v0.4). La acción vive en commands_.
        case InputEventType::MoveLeft: commands_.execute("cursor.mover.izquierda"); break;
        case InputEventType::MoveRight: commands_.execute("cursor.mover.derecha"); break;
        case InputEventType::MoveUp: commands_.execute("cursor.mover.arriba"); break;
        case InputEventType::MoveDown: commands_.execute("cursor.mover.abajo"); break;
        case InputEventType::MoveHome: commands_.execute("cursor.mover.inicio"); break;
        case InputEventType::MoveEnd: commands_.execute("cursor.mover.fin"); break;
        case InputEventType::PageUp: commands_.execute("cursor.pagina.arriba"); break;
        case InputEventType::PageDown: commands_.execute("cursor.pagina.abajo"); break;
        case InputEventType::ScrollUp: commands_.execute("vista.scroll.arriba"); break;
        case InputEventType::ScrollDown: commands_.execute("vista.scroll.abajo"); break;

        // InsertNewline/Backspace/Delete y Escape: no-op (no hay edicion
        // posible y ya estamos en navegacion, no hay a donde volver).
        default:
            break;
    }
}

void Editor::handleInteraccionEvent(const InputEvent& event) {
    Buffer& b = active();
    // Una seleccion DEGENERADA (anchor == position) no tiene significado en
    // edicion: significa "no hay nada seleccionado". Si queda persistida
    // aqui (p.ej. restaurada por undo tras un reemplazo) y luego el cursor
    // se mueve (Backspace/Delete la encogen), se desincronizaria del cursor
    // y podria quedar fuera de rango. Se elimina al entrar a Interaccion.
    if (b.selection.has_value() &&
        b.selection->anchor == b.selection->position) {
        b.selection.reset();
    }
    switch (event.type) {
        case InputEventType::Paste: commands_.execute("navegacion.pegar"); break;
        case InputEventType::InsertChar:
            // Edicion libre real: cualquier letra (incluida i/s/p/c/x)
            // se inserta como texto. Aqui no son comandos de modo.
            // P0 interaction: si venimos de un reemplazo de seleccion
            // (la entrada del grupo ya esta en el historial), NO creamos
            // una nueva: el texto consecutivo se absorbe en la MISMA
            // entrada de undo via extendLastEntry.
            if (!coalescingTyping_) {
                HistoryEntry e = b.beginHistoryEntry();
                Position start{b.cursor.line, b.cursor.col};
                Position end = b.document.insertText(start.line, start.col,
                                                     event.text);
                e.edits.push_back({EditType::Insert, start, end, event.text});
                b.cursor.line = end.line;
                b.cursor.col = end.col;
                updateModified(b);
                b.commitHistoryEntry(std::move(e));
                markWrapLineDirty(this, start.line);
                if (end.line != start.line) markWrapLineDirty(this, end.line);
            } else {
                Position start{b.cursor.line, b.cursor.col};
                Position end = b.document.insertText(start.line, start.col,
                                                     event.text);
                b.extendLastEntry({EditType::Insert, start, end, event.text});
                b.cursor.line = end.line;
                b.cursor.col = end.col;
                updateModified(b);
                markWrapLineDirty(this, start.line);
                if (end.line != start.line) markWrapLineDirty(this, end.line);
            }
            resetPreferredVisualColumn();
            break;

        case InputEventType::InsertNewline: {
            HistoryEntry e = b.beginHistoryEntry();
            Position at{b.cursor.line, b.cursor.col};
            // Indentación lógica de la línea original: prefijo
            // [ ' ' | '\t' ]* verbatim, sin normalizar.
            std::string orig = b.document.lineAt(at.line);
            size_t indentLen = 0;
            while (indentLen < orig.size() &&
                   (orig[indentLen] == ' ' || orig[indentLen] == '\t'))
                ++indentLen;
            std::string indent = orig.substr(0, indentLen);
            b.document.splitLine(at.line, at.col);
            e.edits.push_back({EditType::SplitLine, at,
                               {at.line + 1, 0}, ""});
            markWrapLineDirty(this, at.line);
            markWrapLineDirty(this, at.line + 1);
            if (!indent.empty()) {
                // Nueva línea = I + ltrim(sufijo): evita duplicar cuando
                // el corte cae dentro del propio indent o en solo-blancos.
                const int newLine = at.line + 1;
                std::string suffix = b.document.lineAt(newLine);
                size_t k = 0;
                while (k < suffix.size() &&
                       (suffix[k] == ' ' || suffix[k] == '\t'))
                    ++k;
                if (k > 0) {
                    std::string removed = suffix.substr(0, k);
                    b.document.deleteRange(newLine, 0, newLine,
                                           static_cast<int>(k));
                    e.edits.push_back({EditType::Delete,
                                        {newLine, 0},
                                        {newLine, static_cast<int>(k)},
                                        removed});
                    markWrapLineDirty(this, newLine);
                }
                Position end =
                    b.document.insertText(newLine, 0, indent);
                e.edits.push_back({EditType::Insert,
                                    {newLine, 0}, end, indent});
                b.cursor.line = newLine;
                b.cursor.col = static_cast<int>(indent.size());
            } else {
                b.cursor.line++;
                b.cursor.col = 0;
            }
            updateModified(b);
            b.commitHistoryEntry(std::move(e));
            resetPreferredVisualColumn();
            break;
        }

        case InputEventType::Backspace:
        case InputEventType::Delete: {
            // interaction P0: si hay una seleccion vigente (p.ej. por entrar
            // a edicion sin cancelarla antes), Backspace/Delete borran el
            // rango entero en vez de un solo caracter. deleteSelection()
            // ya registra su propia entrada de historial.
            if (deleteSelection()) break;
            HistoryEntry e = b.beginHistoryEntry();
            if (event.type == InputEventType::Backspace) {
                bool willMergeLines = (b.cursor.col == 0 && b.cursor.line > 0);
                if (willMergeLines) {
                    int prevLineLen = b.document.lineLength(b.cursor.line - 1);
                    Position nl{b.cursor.line - 1, prevLineLen};
                    b.document.mergeLine(nl.line);
                    e.edits.push_back({EditType::MergeLine, nl,
                                       {b.cursor.line, 0}, ""});
                    b.cursor.line--;
                    b.cursor.col = prevLineLen;
                    updateModified(b);
                    markWrapLineDirty(this, nl.line);
                    markWrapLineDirty(this, nl.line + 1);
                } else {
                    int line = b.cursor.line, col = b.cursor.col;
                    // Smart-backspace: si [0, col) es todo indentación
                    // lógica, borra 1 nivel (1x '\t' o hasta kIndentLen
                    // espacios) en vez de 1 solo char.
                    const std::string cur = b.document.lineAt(line);
                    bool allBlank = col > 0 && col <= static_cast<int>(cur.size());
                    for (int i = 0; allBlank && i < col; ++i) {
                        if (cur[static_cast<size_t>(i)] != ' ' &&
                            cur[static_cast<size_t>(i)] != '\t')
                            allBlank = false;
                    }
                    if (allBlank) {
                        int n = 0;
                        if (cur[static_cast<size_t>(col - 1)] == '\t') {
                            n = 1;
                        } else {
                            while (n < kIndentLen && n < col &&
                                   cur[static_cast<size_t>(col - 1 - n)] == ' ')
                                ++n;
                        }
                        if (n > 0) {
                            std::string removed =
                                cur.substr(static_cast<size_t>(col - n),
                                           static_cast<size_t>(n));
                            if (b.document.deleteRange(line, col - n, line, col)) {
                                e.edits.push_back({EditType::Delete,
                                                   {line, col - n},
                                                   {line, col}, removed});
                                b.cursor.col -= n;
                                updateModified(b);
                                markWrapLineDirty(this, line);
                            }
                        }
                    } else {
                        std::string removed =
                            b.document.cellTextBefore(line, col);
                        int deleted = b.document.deleteCharBefore(line, col);
                        if (deleted > 0) {
                            e.edits.push_back({EditType::Delete, {line, col - deleted},
                                               {line, col}, removed});
                            b.cursor.col -= deleted;
                            updateModified(b);
                            markWrapLineDirty(this, line);
                        }
                    }
                }
            } else {
                int line = b.cursor.line, col = b.cursor.col;
                int len = b.document.lineLength(line);
                if (col < len) {
                    std::string removed =
                        b.document.cellTextAt(line, col);
                    int deleted = b.document.deleteCharAt(line, col);
                    if (deleted > 0) {
                        e.edits.push_back({EditType::Delete, {line, col},
                                           {line, col + deleted}, removed});
                        updateModified(b);
                        markWrapLineDirty(this, line);
                    }
                } else if (line + 1 < b.document.lineCount()) {
                    b.document.mergeLine(line);
                    e.edits.push_back({EditType::MergeLine, {line, col},
                                       {line + 1, 0}, ""});
                    updateModified(b);
                    markWrapLineDirty(this, line);
                    markWrapLineDirty(this, line + 1);
                }
            }
            b.commitHistoryEntry(std::move(e));
            resetPreferredVisualColumn();
            break;
        }

        case InputEventType::Escape:
            state_ = State::Navegacion;
            setStatusMessage(kHelpEmpty);
            break;

        case InputEventType::MoveLeft: commands_.execute("cursor.mover.izquierda"); break;
        case InputEventType::MoveRight: commands_.execute("cursor.mover.derecha"); break;
        case InputEventType::MoveUp: commands_.execute("cursor.mover.arriba"); break;
        case InputEventType::MoveDown: commands_.execute("cursor.mover.abajo"); break;
        case InputEventType::MoveHome: commands_.execute("cursor.mover.inicio"); break;
        case InputEventType::MoveEnd: commands_.execute("cursor.mover.fin"); break;
        case InputEventType::PageUp: commands_.execute("cursor.pagina.arriba"); break;
        case InputEventType::PageDown: commands_.execute("cursor.pagina.abajo"); break;
        case InputEventType::ScrollUp: commands_.execute("vista.scroll.arriba"); break;
        case InputEventType::ScrollDown: commands_.execute("vista.scroll.abajo"); break;

        default:
            break;
    }
}

void Editor::handleSeleccionEvent(const InputEvent& event) {
    Buffer& b = active();
    // Si el prefijo 'a' (seleccion total) esta activo, casi todos los
    // eventos se interpretan de forma especial, no como extension normal
    // de la seleccion.
    if (b.selectAllActive) {
        handleSelectAllEvent(event);
        return;
    }

    switch (event.type) {
        // Movimientos: pura resolución InputEvent -> nombre. La semántica
        // completa (begin + mover + update, o salto a extremo en seleccion
        // total) vive en el comando (regla InputEvent/CommandMap), así el
        // botón GUI produce lo mismo que el teclado. Igual que v0.3-v0.4.
        case InputEventType::MoveLeft:
            commands_.execute("cursor.mover.izquierda"); break;
        case InputEventType::MoveRight:
            commands_.execute("cursor.mover.derecha"); break;
        case InputEventType::MoveUp:
            commands_.execute("cursor.mover.arriba"); break;
        case InputEventType::MoveDown:
            commands_.execute("cursor.mover.abajo"); break;
        case InputEventType::MoveHome:
            commands_.execute("cursor.mover.inicio"); break;
        case InputEventType::MoveEnd:
            commands_.execute("cursor.mover.fin"); break;
        // RePag/AvPag extienden la seleccion como una flecha (Up/Down):
        // el anchor permanece y el cursor salta una pagina (en el comando).
        case InputEventType::PageUp:
            commands_.execute("cursor.pagina.arriba"); break;
        case InputEventType::PageDown:
            commands_.execute("cursor.pagina.abajo"); break;
        case InputEventType::ScrollUp:
            commands_.execute("vista.scroll.arriba"); break;
        case InputEventType::ScrollDown:
            commands_.execute("vista.scroll.abajo"); break;
        case InputEventType::Copy:
            commands_.execute("seleccion.copiar"); break;
        case InputEventType::Cut:
            commands_.execute("seleccion.cortar"); break;
        case InputEventType::Paste:
            commands_.execute("navegacion.pegar"); break;

        // 'c' copia el rango al buffer y 'x' lo copia y lo borra; ambos
        // terminan la seleccion y vuelven a navegacion. Si la seleccion
        // esta vacia, 'c'/'x' no tocan el buffer (solo se sale del modo).
        // OJO: el gate es hasSelection() (anchor != position), NO
        // selection().has_value() (que es true incluso para un objeto
        // Selection vacio con anchor == position, ya que normalize solo
        // ordena los puntos). Usar el segundo sobreescribiria el buffer con
        // un rango vacio y, en 'x', empujaria un historial inutil que ademas
        // limpia el redo.
        case InputEventType::InsertChar:
            // 'a' entra al prefijo de "seleccion total": cubre el archivo
            // entero sin mover el cursor. 'c' copia el rango al buffer y 'x'
            // lo copia y lo borra; ambos terminan la seleccion y vuelven a
            // navegacion. 'j'/'k' extienden por bloques. Cualquier otra
            // letra ya NO reemplaza la seleccion: se ignora. El despacho por
            // comando vive en commands_.
            if (event.text == "a") {
                commands_.execute("seleccion.total");
            } else if (event.text == "j") {
                commands_.execute("seleccion.j");
            } else if (event.text == "k") {
                commands_.execute("seleccion.k");
            } else if (event.text == "c") {
                commands_.execute("seleccion.copiar");
            } else if (event.text == "x") {
                commands_.execute("seleccion.cortar");
            } else if (event.text == "}") {
                commands_.execute("seleccion.indentar");
            } else if (event.text == "{") {
                commands_.execute("seleccion.desindentar");
            } else if (event.text == "p") {
                // P0 interaction: 'p' desde Seleccion reemplaza el rango
                // seleccionado por el clipboard (navegacion.pegar ya
                // borra la seleccion y vuelve a Navegacion).
                commands_.execute("navegacion.pegar");
            } else {
                // P0 interaction: escribir una letra que NO es comando de
                // Seleccion REEMPLAZA el rango marcado por esa letra y entra
                // a Interaccion, abriendo un "grupo de escritura": el
                // historial se empuja UNA vez aqui y la escritura consecutiva
                // posterior se absorbe en la misma entrada, de modo que
                // "reemplazo + tecleo" se deshace en una sola operacion.
                // Si el rango es vacio (solo se entro al modo), no hay nada
                // que reemplazar: se inserta igual pero el grupo NO coalesce
                // (escritura normal, por caracter).
                // P0 interaction: escribir una letra que NO es comando de
                // Seleccion REEMPLAZA el rango marcado por esa letra y entra
                // a Interaccion, abriendo un "grupo de escritura": se crea
                // UNA entrada de historial aqui (Delete del rango + Insert
                // de la letra) y la escritura consecutiva posterior se
                // absorbe en la misma entrada via extendLastEntry, de modo
                // que "reemplazo + tecleo" se deshace en una sola operacion.
                // Si el rango es vacio (solo se entro al modo), no hay nada
                // que reemplazar: se inserta igual pero el grupo NO coalesce
                // (escritura normal, por caracter).
                bool hadSel = hasSelection();
                auto sel = selection();
                HistoryEntry e = b.beginHistoryEntry();
                if (hadSel) {
                    auto removed = b.document.extractRange(sel->start.line, sel->start.col,
                                                           sel->end.line, sel->end.col);
                    b.document.deleteRange(sel->start.line, sel->start.col,
                                           sel->end.line, sel->end.col);
                    e.edits.push_back({EditType::Delete, sel->start, sel->end,
                                       blockToString(removed)});
                    b.cursor.line = sel->start.line;
                    b.cursor.col = sel->start.col;
                }
                Position start{b.cursor.line, b.cursor.col};
                Position end = b.document.insertText(start.line, start.col,
                                                     event.text);
                e.edits.push_back({EditType::Insert, start, end, event.text});
                b.cursor.line = end.line;
                b.cursor.col = end.col;
                invalidateWrapIndex();
                resetPreferredVisualColumn();
                updateModified(b);
                clearSelection();
                state_ = State::Interaccion;
                coalescingTyping_ = hadSel;
                b.commitHistoryEntry(std::move(e));
                setActionMessage("Reemplazando seleccion...");
            }
            break;
        case InputEventType::Escape:
            clearSelection();
            state_ = State::Navegacion;
            setActionMessage("Seleccion cancelada.");
            break;

        // Delete/Backspace si borran el rango seleccionado (interaction P0)
        // y vuelven a navegacion.
        case InputEventType::Delete:
        case InputEventType::Backspace:
            if (deleteSelection()) {
                state_ = State::Navegacion;
                setActionMessage("Borrado.");
            }
            break;

        // InsertNewline y el resto: no-op. Salir de seleccion es siempre a
        // navegacion, nunca a interaccion con reemplazo del rango.
        default:
            break;
    }
}

// Prefijo 'a' (seleccion total): interpreta los eventos mientras
// selectAllActive es true. La seleccion entera ya esta puesta en
// selection ([BOF, EOF]); aqui se decide que hace cada tecla con ella.
void Editor::handleSelectAllEvent(const InputEvent& event) {
    Buffer& b = active();
    switch (event.type) {
        // 'a' de nuevo: toggle -> volvemos a la seleccion previa (o, si la
        // previa era sin seleccion, a sin seleccion) y se desactiva el modo.
        case InputEventType::InsertChar:
            if (event.text == "a") {
                b.selection = b.selectAllPrevious;
                b.selectAllPrevious.reset();
                b.selectAllActive = false;
                const bool restoredSelection = hasSelection();
                if (restoredSelection) {
                    state_ = State::Seleccion;
                    setStatusMessage("SELECCION");
                } else {
                    b.selection.reset();
                    state_ = State::Navegacion;
                    setStatusMessage("");
                }
            } else if (event.text == "c" || event.text == "x") {
                commands_.execute(event.text == "c" ? "seleccion.copiar" : "seleccion.cortar");
            } else if (event.text == "}") {
                // Tabulacion sobre el archivo ENTERO (la seleccion total es
                // una seleccion real): indentar todo. indentSelection deja
                // el modo 'a' intacto, asi que se puede seguir tabulando.
                commands_.execute("seleccion.indentar");
            } else if (event.text == "{") {
                commands_.execute("seleccion.desindentar");
            }
            // Cualquier otra letra: no pasa nada.
            break;

        // Flechas y movimientos: delegan a los comandos cursor.mover.* /
        // cursor.pagina.* / vista.scroll.*, que son dueños del salto a
        // extremo en seleccion total (jumpSelectAllEdge) y del no-op donde
        // el teclado no hace nada (regla InputEvent/CommandMap).
        case InputEventType::MoveRight:
            commands_.execute("cursor.mover.derecha");
            break;
        case InputEventType::MoveDown:
            commands_.execute("cursor.mover.abajo");
            break;
        case InputEventType::MoveLeft:
            commands_.execute("cursor.mover.izquierda");
            break;
        case InputEventType::MoveUp:
            commands_.execute("cursor.mover.arriba");
            break;
        case InputEventType::MoveHome:
            commands_.execute("cursor.mover.inicio");
            break;
        case InputEventType::MoveEnd:
            commands_.execute("cursor.mover.fin");
            break;
        case InputEventType::PageUp:
            commands_.execute("cursor.pagina.arriba");
            break;
        case InputEventType::PageDown:
            commands_.execute("cursor.pagina.abajo");
            break;
        case InputEventType::ScrollUp:
            commands_.execute("vista.scroll.arriba");
            break;
        case InputEventType::ScrollDown:
            commands_.execute("vista.scroll.abajo");
            break;

        // ESC: cancela la seleccion total (y toda seleccion) y vuelve a
        // navegacion, igual que en el modo Seleccion normal.
        case InputEventType::Escape:
            clearSelection();
            b.selectAllPrevious.reset();
            b.selectAllActive = false;
            state_ = State::Navegacion;
            setActionMessage("Seleccion cancelada.");
            break;

        // Backspace/Delete: borra el archivo entero (la seleccion total es
        // un rango real) y vuelve a Navegacion, igual que en Seleccion
        // normal. deleteSelection() ya empuja historial y deja el cursor
        // al inicio. Si el rango es degenerado (archivo vacio) no hay
        // nada que borrar: no-op, el prefijo sigue activo.
        case InputEventType::Delete:
        case InputEventType::Backspace:
            // Si el archivo está vacío, no hay nada que borrar.
            if (b.document.lineCount() == 1 && b.document.lineAt(0).empty()) {
                // No-op: el prefijo sigue activo, no cambiamos estado.
                setActionMessage("Archivo vacío.");
                break;
            }

            if (deleteSelection()) {
                b.selectAllPrevious.reset();
                b.selectAllActive = false;
                state_ = State::Navegacion;
                setActionMessage("Borrado.");
            }
            break;

        // Resto (incl. teclas desconocidas): no-op.
        default:
            break;
    }
}

void Editor::handlePrefixKey(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::Save:
        case InputEventType::SaveAs:  // Ctrl+K Ctrl+Shift+S: igual que Ctrl+K Ctrl+S
            commands_.execute("buffer.guardar.como");
            break;

        case InputEventType::Quit:
            commands_.execute("app.salir.forzado");
            break;

        // v0.6.3: comandos de buffer dentro del prefijo. El mapeo tecla ->
        // comando -> handler vive en commands_.
        case InputEventType::InsertChar:
            if (event.text == "n") {           // Ctrl+K n: nuevo buffer
                commands_.execute("buffer.nuevo");
                break;
            }
            if (event.text == "t") {           // Ctrl+K t: selector de buffers
                commands_.execute("buffer.selector");
                break;
            }
            if (event.text == "w") {           // Ctrl+K w: cerrar buffer activo
                commands_.execute("buffer.cerrar");
                break;
            }
            if (event.text == "o") {           // Ctrl+K o: explorador de archivos
                commands_.execute("buffer.abrir");
                break;
            }
            if (event.text == "s") {           // Ctrl+K s: guardar archivo
                commands_.execute("buffer.guardar");
                break;
            }
            if (event.text == "q") {           // Ctrl+K q: salida segura
                commands_.execute("app.salir");
                break;
            }
            if (event.text == "b") {           // Ctrl+K b: buffer anterior
                commands_.execute("buffer.anterior");
                break;
            }
            if (event.text == "l" || event.text == "L") { // Ctrl+K l: alternar tema claro/oscuro
                commands_.execute("theme.toggle");
                break;
            }
            if (event.text == "m" || event.text == "M") { // Ctrl+K m: salto bracket
                commands_.execute("bracket.jump");
                break;
            }
            if (event.text == "r" || event.text == "R") { // Ctrl+K r: renombrar archivo
                commands_.execute("archivo.renombrar");
                break;
            }
            // Cualquier otra letra: cae en el cancel del default.
            state_ = priorState_;
            setActionMessage("Comando cancelado.");
            break;

        default:
            // Cualquier otra tecla (incl. ESC, flechas, caracteres...):
            // se descarta el evento y se cancela el prefijo, volviendo al
            // estado anterior sin tocar la seleccion ni el documento.
            state_ = priorState_;
            setActionMessage("Comando cancelado.");
            break;
    }
}

// ---- Guardar como con explorador (v0.9) ----

void Editor::startSaveAs() {
    const std::string& fn = active().filename;
    if (!fn.empty()) {
        const std::filesystem::path dir =
            std::filesystem::path(fn).parent_path();
        if (!dir.empty() && FileBrowser::isDirectory(dir.string())) {
            fileBrowser.startAt(dir.string());
        } else {
            fileBrowser.start();
        }
    } else {
        fileBrowser.start();
    }
    const std::string err = fileBrowser.reload();
    if (!err.empty()) {
        setActionMessage(err, MessageKind::Error);
    }

    // Inicializar el nombre del archivo: si el buffer tiene nombre, usarlo;
    // si no, dejar vacío para que el usuario lo escriba.
    if (!fn.empty()) {
        saveAsFileName_ = std::filesystem::path(fn).filename().string();
    } else {
        saveAsFileName_.clear();
    }
    saveAsConfirmPath_.clear();
    state_ = State::SaveAsFileBrowser;
    // El mensaje de ayuda se muestra en el MessageBar via renderer
    setStatusMessage("", MessageKind::Info);
}

void Editor::commitSaveAsFileBrowser() {
    Buffer& b = active();
    if (saveAsFileName_.empty()) {
        setActionMessage("Nombre de archivo vacio.", MessageKind::Warning);
        return;
    }
    // Defensa del contrato basename (el input ya filtra '/' y '\', pero el
    // commit no confia en ello): rechaza rutas residuales y los nombres
    // "." / ".." que escaparian del directorio seleccionado.
    if (saveAsFileName_ == "." || saveAsFileName_ == ".." ||
        saveAsFileName_.find('/') != std::string::npos ||
        saveAsFileName_.find('\\') != std::string::npos) {
        setActionMessage("Nombre de archivo invalido: use solo el nombre, "
                         "sin rutas.", MessageKind::Warning);
        return;
    }
    // Armar path completo: directorio del browser + nombre escrito
    std::string dirPath = fileBrowser.path_;
    if (!dirPath.empty() && dirPath.back() != '/') dirPath += '/';
    std::string fullPath = dirPath + saveAsFileName_;
    const std::string path = resolveAbsolutePath(fullPath);

    if (path.empty()) {
        setActionMessage("Ruta invalida.", MessageKind::Error);
        return;
    }
    if (isDirectory(path)) {
        setActionMessage("Es una carpeta: " + path, MessageKind::Error);
        return;
    }
    // Fase 1: destino ya abierto en otro buffer -> rechazar
    for (int i = 0; i < buffers.count(); ++i) {
        if (&buffers.at(i) == &b) continue;
        if (buffers.at(i).filename == path) {
            setActionMessage("Destino ya abierto: " + path, MessageKind::Error);
            return;
        }
    }
    const bool samePath = !b.filename.empty() && b.filename == path;
    // Sobrescritura con confirmación
    if (!samePath) {
        if (saveAsConfirmPath_ == path) {
            saveAsConfirmPath_.clear();
        } else {
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                saveAsConfirmPath_ = path;
                setActionMessage("El archivo ya existe: " + path +
                                     ". Ctrl+S para sobrescribir, Esc para cancelar.",
                             MessageKind::Warning);
                return;
            }
            saveAsConfirmPath_.clear();
        }
    }
    // Sin nombre o mismo path: guardado in-place
    if (b.filename.empty() || samePath) {
        if (b.document.saveToFile(path)) {
            b.filename = path;
            b.syncSavedState();
            b.savedIdentity = captureIdentity(path);
            watchFile(b.filename);
            setActionMessage("Guardado: " + path, MessageKind::Success);
            state_ = priorState_;
        } else {
            setActionMessage("Error al guardar: " + path, MessageKind::Error);
        }
        return;
    }
    // Save a copy
    if (!b.document.saveToFile(path)) {
        setActionMessage("Error al guardar: " + path, MessageKind::Error);
        return;
    }
    Buffer copy = b;
    copy.filename = path;
    copy.syncSavedState();
    copy.savedIdentity = captureIdentity(path);
    // push() deja la copia activa y al frente de la MRU.
    buffers.push(std::move(copy));
    watchFile(path);
    renderer_->invalidateCache();
    setActionMessage("Guardado: " + path, MessageKind::Success);
    state_ = priorState_;
}

void Editor::handleSaveAsFileBrowserEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::PageUp:
            fileBrowser.movePage(-1, active().viewport.height);
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::PageDown:
            fileBrowser.movePage(+1, active().viewport.height);
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::MoveUp:
            fileBrowser.moveUp();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::MoveDown:
            fileBrowser.moveDown();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::MoveRight: {
            const FileBrowserEntry& e = fileBrowser.entries_[static_cast<size_t>(fileBrowser.index_)];
            if(e.isDirectory && e.name != "..")
                fileBrowserEnterDirectoryOnly();
            break;
        }
        case InputEventType::MoveLeft: {
            const FileBrowserEntry& e = fileBrowser.entries_[static_cast<size_t>(fileBrowser.index_)];
            if (e.name == "..") {
                fileBrowser.goUp();
                const std::string err = fileBrowser.reload();
                if (!err.empty()) {
                    setActionMessage(err, MessageKind::Error);
                }
            }
            break;
        }
        case InputEventType::ScrollUp:
            fileBrowser.moveUp();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::ScrollDown:
            fileBrowser.moveDown();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case InputEventType::InsertNewline: // Enter: confirmar guardado
            commitSaveAsFileBrowser();
            break;
        case InputEventType::Paste: {
            auto maybe = clipboard_->paste();
            if (!maybe || maybe->empty()) {
                break;
            }

            bool sawPathSeparator = false;

            for (char ch : *maybe) {
                // Mismo contrato que InsertChar: '/' y '\' no entran al nombre.
                if (ch == '/' || ch == '\\') {
                    sawPathSeparator = true;
                    continue;
                }

                // Evitar controles como \r, \n, \t, NUL, DEL, etc.
                const unsigned char uc = static_cast<unsigned char>(ch);
                if (uc < 0x20 || uc == 0x7F) {
                    continue;
                }

                saveAsFileName_ += ch;
            }

            if (sawPathSeparator) {
                setActionMessage(
                    "El nombre no puede contener rutas: "
                    "cambie de carpeta con flecha derecha.",
                    MessageKind::Warning);
            }

            break;
        }
        case InputEventType::InsertChar:
            // Contrato SaveAs: saveAsFileName_ es SOLO basename, sin path.
            // Se filtran '/' y '\' para que el input nunca se convierta en
            // ruta: el directorio solo cambia navegando carpetas con flecha derecha.
            // El filtrado es a nivel byte y seguro para UTF-8: 0x2F y 0x5C
            // nunca aparecen como bytes de continuacion multibyte.
            for (char ch : event.text) {
                if (ch != '/' && ch != '\\') saveAsFileName_ += ch;
            }
            if (event.text.find('/') != std::string::npos ||
                event.text.find('\\') != std::string::npos) {
                setActionMessage("El nombre no puede contener rutas: "
                                 "cambie de carpeta con flecha derecha.",
                                 MessageKind::Warning);
            }
            break;
        case InputEventType::Backspace:
            if (!saveAsFileName_.empty()) {
                int cols = utf8::columnOf(saveAsFileName_,
                                          static_cast<int>(saveAsFileName_.size()));
                saveAsFileName_ = utf8::truncate(saveAsFileName_, cols - 1);
            }
            break;
        case InputEventType::Escape:
        case InputEventType::Prefix: // Ctrl+K dentro tambien cancela
            saveAsConfirmPath_.clear();
            saveAsFileName_.clear();
            state_ = priorState_;
            setActionMessage("Guardado cancelado.", MessageKind::Warning);
            break;
        default:
            break;
    }
}

void Editor::startSearch() {
    searchQuery_.clear();
    searchOrigin_ = {active().cursor.line, active().cursor.col};
    clearSearchHighlight();
    state_ = State::Busqueda;
    setStatusMessage(std::string(kHelpBusqueda), MessageKind::Prompt,
                     promptCursor(kHelpBusqueda, searchQuery_),
                     promptBoldPrefix(kHelpBusqueda));
}

std::vector<Position> Editor::collectMatches(const std::string& query) const {
    std::vector<Position> out;
    if (query.empty()) return out;
    const Document& doc = active().document;
    for (int l = 0; l < doc.lineCount(); ++l) {
        const std::string& line = doc.lineAt(l);
        size_t pos = 0;
        while (true) {
            pos = line.find(query, pos);
            if (pos == std::string::npos) break;
            out.push_back({l, static_cast<int>(pos)});
            pos += query.size();
            if (pos >= line.size()) break;
        }
    }
    return out;
}

void Editor::updateSearchMessage(bool found, int current, int total) {
    std::string msg = std::string(kHelpBusqueda) + searchQuery_;
    if (!found && !searchQuery_.empty()) msg += " - not found";
    else if (found && !searchQuery_.empty() && total > 0) {
        if (total > 100) msg += " (100+)";
        else msg += " (" + std::to_string(current) + "/" + std::to_string(total) + ")";
    }
    // El cursor queda al final de la query, antes de " - not found"/" (i/N)".
    setStatusMessage(msg, MessageKind::Prompt,
                     promptCursor(kHelpBusqueda, searchQuery_),
                     promptBoldPrefix(kHelpBusqueda));
}

void Editor::setSearchHighlight(const Position& pos, int len) {
    Buffer& b = active();
    Position end{pos.line, pos.col + len};
    int lineLen = b.document.lineLength(pos.line);
    if (end.col > lineLen) end.col = lineLen;
    searchHighlight_ = Selection{pos, end};
}

void Editor::clearSearchHighlight() {
    searchHighlight_.reset();
}

void Editor::centerViewportOnCursor() {
    Buffer& b = active();
    if (isWrapEnabledFor(b)) {
        const softwrap::Index& idx = wrapFor(this, b);
        int totalRows = idx.totalRows();
        int h = b.viewport.height;
        if (h <= 0) return;
        int vr = idx.visualRowOf({b.cursor.line, b.cursor.col});
        int maxTop = std::max(0, totalRows - h);
        int desiredTop = vr - h / 2;
        desiredTop = std::max(0, std::min(desiredTop, maxTop));
        b.viewport.top = desiredTop;
        b.viewport.left = 0;
        return;
    }
    int h = b.viewport.height;
    if (h > 0) {
        int totalLines = b.document.lineCount();
        int maxTop = std::max(0, totalLines - h);
        int desiredTop = b.cursor.line - h / 2;
        desiredTop = std::max(0, std::min(desiredTop, maxTop));
        b.viewport.top = desiredTop;
    }
    int tw = textWidthFor(b.viewport, b.document.lineCount());
    if (tw > 0) {
        int absoluteCol = utf8::columnOf(b.document.lineAt(b.cursor.line), b.cursor.col);
        int desiredLeft = absoluteCol - tw / 2;
        if (desiredLeft < 0) desiredLeft = 0;
        b.viewport.left = desiredLeft;
    } else {
        b.viewport.left = 0;
    }
}

void Editor::updateSearch() {
    Buffer& b = active();
    if (searchQuery_.empty()) {
        b.cursor.line = searchOrigin_.line;
        b.cursor.col = searchOrigin_.col;
        b.cursor.clampToLine(b.document);
        clearSearchHighlight();
        centerViewportOnCursor();
        updateSearchMessage(true, 0, 0);
        return;
    }
    auto matches = collectMatches(searchQuery_);
    if (matches.empty()) {
        clearSearchHighlight();
        updateSearchMessage(false, 0, 0);
        return;
    }
    auto it = std::find_if(matches.begin(), matches.end(), [&](const Position& p) {
        return !(p < searchOrigin_);
    });
    int idx = (it != matches.end()) ? static_cast<int>(it - matches.begin()) : 0;
    Position target = (it != matches.end()) ? *it : matches.front();
    b.cursor.line = target.line;
    b.cursor.col = target.col;
    resetPreferredVisualColumn();
    setSearchHighlight(target, static_cast<int>(searchQuery_.size()));
    centerViewportOnCursor();
    updateSearchMessage(true, idx + 1, static_cast<int>(matches.size()));
}

void Editor::navigateSearch(int dir) {
    if (searchQuery_.empty()) return;
    auto matches = collectMatches(searchQuery_);
    if (matches.empty()) {
        clearSearchHighlight();
        updateSearchMessage(false, 0, 0);
        return;
    }
    if (matches.size() == 1) {
        active().cursor.line = matches[0].line;
        active().cursor.col = matches[0].col;
        resetPreferredVisualColumn();
        setSearchHighlight(matches[0], static_cast<int>(searchQuery_.size()));
        centerViewportOnCursor();
        updateSearchMessage(true, 1, static_cast<int>(matches.size()));
        return;
    }
    Buffer& b = active();
    Position cur{b.cursor.line, b.cursor.col};
    int idx = -1;
    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
        if (matches[i] == cur) { idx = i; break; }
    }
    if (idx == -1) {
        updateSearch();
        return;
    }
    int next = (dir > 0) ? (idx + 1) % static_cast<int>(matches.size())
                         : (idx - 1 + static_cast<int>(matches.size())) % static_cast<int>(matches.size());
    b.cursor.line = matches[next].line;
    b.cursor.col = matches[next].col;
    resetPreferredVisualColumn();
    setSearchHighlight(matches[next], static_cast<int>(searchQuery_.size()));
    centerViewportOnCursor();
    updateSearchMessage(true, next + 1, static_cast<int>(matches.size()));
}

void Editor::handleBusquedaEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::Paste: {
            auto maybe = clipboard_->paste();
            if (!maybe || maybe->empty()) {
                break;
            }
        
            searchQuery_ += *maybe;
            updateSearch();
            break;
        }
        case InputEventType::InsertChar:
            searchQuery_ += event.text;
            updateSearch();
            break;
        case InputEventType::Backspace:
            if (!searchQuery_.empty()) {
                int cols = utf8::columnOf(searchQuery_, static_cast<int>(searchQuery_.size()));
                searchQuery_ = utf8::truncate(searchQuery_, cols - 1);
            }
            updateSearch();
            break;
        case InputEventType::MoveDown:
            navigateSearch(+1);
            break;
        case InputEventType::MoveUp:
            navigateSearch(-1);
            break;
        case InputEventType::InsertNewline:
            clearSearchHighlight();
            state_ = State::Navegacion;
            searchQuery_.clear();
            setStatusMessage("", MessageKind::Info);
            break;
        case InputEventType::Escape: {
            Buffer& b = active();
            b.cursor.line = searchOrigin_.line;
            b.cursor.col = searchOrigin_.col;
            b.cursor.clampToLine(b.document);
            resetPreferredVisualColumn();
            clearSearchHighlight();
            centerViewportOnCursor();
            searchQuery_.clear();
            state_ = State::Navegacion;
            setStatusMessage("", MessageKind::Info);
            break;
        }
        default:
            break;
    }
}

void Editor::handleIrAFilaEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::Paste: {
            auto maybe = clipboard_->paste();
            if (!maybe || maybe->empty()) {
                break;
            }

            // El prompt de "Ir a fila" solo acepta dígitos.
            // Del clipboard tomamos únicamente caracteres 0-9.
            std::string digits;
            digits.reserve(maybe->size());

            for (char ch : *maybe) {
                if (ch >= '0' && ch <= '9') {
                    digits.push_back(ch);
                }
            }

            if (digits.empty()) {
                break;
            }

            goToLineQuery_ += digits;

            setStatusMessage(
                std::string(kHelpIrAFila) + goToLineQuery_,
                MessageKind::Prompt,
                promptCursor(kHelpIrAFila, goToLineQuery_),
                promptBoldPrefix(kHelpIrAFila));

            break;
        }
        case InputEventType::InsertChar:
            // Solo aceptar dígitos, descartando cualquier otro carácter
            if (event.text.size() == 1 && event.text[0] >= '0' && event.text[0] <= '9') {
                goToLineQuery_ += event.text;
                setStatusMessage(std::string(kHelpIrAFila) + goToLineQuery_, MessageKind::Prompt,
                                 promptCursor(kHelpIrAFila, goToLineQuery_),
                                 promptBoldPrefix(kHelpIrAFila));
            }
            break;
        case InputEventType::Backspace:
            if (!goToLineQuery_.empty()) {
                goToLineQuery_.pop_back();
                setStatusMessage(std::string(kHelpIrAFila) + goToLineQuery_, MessageKind::Prompt,
                                 promptCursor(kHelpIrAFila, goToLineQuery_),
                                 promptBoldPrefix(kHelpIrAFila));
            }
            break;
        case InputEventType::InsertNewline: {
            if (!goToLineQuery_.empty()) {
                try {
                    int lineNum = std::stoi(goToLineQuery_);
                    int maxLines = active().document.lineCount();

                    // Validar que la línea esté entre 1 y la longitud máxima del documento
                    if (lineNum >= 1 && lineNum <= maxLines) {
                        Buffer& b = active();
                        b.cursor.line = lineNum - 1; // Las líneas internas son 0-indexed
                        b.cursor.col = 0;
                        b.cursor.clampToLine(b.document);
                        resetPreferredVisualColumn();
                        centerViewportOnCursor();

                        state_ = State::Navegacion;
                        goToLineQuery_.clear();
                        setStatusMessage("", MessageKind::Info);
                        setActionMessage("Fila " + std::to_string(lineNum), MessageKind::Success);
                    } else {
                        // Unificamos el feedback en el propio prompt para evitar
                        // la sobrescritura inmediata entre setActionMessage y setStatusMessage.
                        setStatusMessage(std::string(kHelpIrAFila) + " [1-" + std::to_string(maxLines) + "]", MessageKind::Error,
                                         std::nullopt, promptBoldPrefix(kHelpIrAFila));
                        goToLineQuery_.clear();
                    }
                } catch (const std::out_of_range&) {
                    setStatusMessage(std::string(kHelpIrAFila) + " [valor invalido]", MessageKind::Error,
                                     std::nullopt, promptBoldPrefix(kHelpIrAFila));
                    goToLineQuery_.clear();
                }
            }
            break;
        }
        case InputEventType::Escape:
            state_ = State::Navegacion;
            goToLineQuery_.clear();
            setStatusMessage("", MessageKind::Info);
            break;
        default:
            // Cualquier otra tecla es no-op (se descarta)
            break;
    }
}

// ---- Renombrar archivo (Ctrl+K r) ----
void Editor::startRename() {
    Buffer& b = active();
    if (b.filename.empty()) {
        state_ = priorState_;
        setActionMessage("El archivo aun no esta guardado.",
                         MessageKind::Warning);
        return;
    }
    if (b.modified) {
        state_ = priorState_;
        setActionMessage("Guarda antes de renombrar.",
                         MessageKind::Warning);
        return;
    }
    renameQuery_ =
        std::filesystem::path(b.filename).filename().string();
    state_ = State::Renombrar;
    setStatusMessage(std::string(kHelpNombrar) + renameQuery_,
                     MessageKind::Prompt,
                     promptCursor(kHelpNombrar, renameQuery_),
                     promptBoldPrefix(kHelpNombrar));
}

void Editor::commitRename() {
    Buffer& b = active();
    const std::string& q = renameQuery_;
    if (q.empty()) {
        setStatusMessage(std::string(kHelpNombrar) + "[nombre vacio]",
                         MessageKind::Error,
                         promptCursor(kHelpNombrar, q),
                         promptBoldPrefix(kHelpNombrar));
        return;
    }
    if (q.size() > kMaxRenameBytes) {
        setStatusMessage(std::string(kHelpNombrar) + "[max 255 bytes]",
                         MessageKind::Error,
                         promptCursor(kHelpNombrar, q),
                         promptBoldPrefix(kHelpNombrar));
        return;
    }
    if (q == "." || q == ".." ||
        q.find('/') != std::string::npos ||
        q.find('\\') != std::string::npos) {
        setStatusMessage(
            std::string(kHelpNombrar) + "[nombre invalido: sin rutas]",
            MessageKind::Error,
            promptCursor(kHelpNombrar, q),
            promptBoldPrefix(kHelpNombrar));
        return;
    }
    const std::string oldPath = b.filename;
    if (oldPath.empty()) {
        renameQuery_.clear();
        state_ = priorState_;
        setActionMessage("El archivo aun no esta guardado.",
                         MessageKind::Warning);
        return;
    }
    std::string dir =
        std::filesystem::path(oldPath).parent_path().string();
    std::string newPath = resolveAbsolutePath(
        (dir.empty() ? std::string(".") : dir) + "/" + q);
    if (newPath.empty()) {
        setActionMessage("Ruta invalida.", MessageKind::Error);
        return;
    }
    if (newPath.size() > 4000) {
        setActionMessage("Ruta demasiado larga.", MessageKind::Error);
        return;
    }
    if (newPath == oldPath) {
        renameQuery_.clear();
        state_ = priorState_;
        setStatusMessage("", MessageKind::Info);
        setActionMessage("Sin cambios.", MessageKind::Info);
        return;
    }
    if (isDirectory(newPath)) {
        setActionMessage("Es una carpeta: " + newPath,
                         MessageKind::Error);
        return;
    }
    for (int i = 0; i < buffers.count(); ++i) {
        if (&buffers.at(i) == &b) continue;
        if (buffers.at(i).filename == newPath) {
            setActionMessage("Destino ya abierto: " + newPath,
                             MessageKind::Error);
            return;
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(newPath, ec)) {
        setActionMessage("Ya existe: " + newPath, MessageKind::Error);
        return;
    }
    std::filesystem::rename(oldPath, newPath, ec);
    if (ec) {
        setActionMessage("No se pudo renombrar: " + ec.message(),
                         MessageKind::Error);
        return;
    }
    // Orden como loadIntoActiveBuffer: primero se actualiza filename para
    // que unwatchFile(oldPath) no vea al propio buffer activo y salte el
    // unwatch por su guarda ("otro buffer aun usa ese path").
    b.filename = newPath;
    unwatchFile(oldPath);
    b.syncSavedState();
    b.savedIdentity = captureIdentity(newPath);
    watchFile(newPath);
    renderer_->invalidateCache();
    renameQuery_.clear();
    state_ = priorState_;
    setStatusMessage("", MessageKind::Info);
    setActionMessage("Renombrado: " + newPath, MessageKind::Success);
}

void Editor::handleRenameEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::InsertChar: {
            // Contrato basename: el input con '/' o '\' se RECHAZA entero
            // (no se filtra: "a/b" no se convierte en "ab", se descarta).
            // Chequeo a nivel byte, seguro UTF-8: 0x2F y 0x5C nunca son
            // bytes de continuacion multibyte. Nunca se interpreta como
            // ruta ni se crean directorios.
            if (event.text.find('/') != std::string::npos ||
                event.text.find('\\') != std::string::npos) {
                setActionMessage("El nombre no puede contener rutas.",
                                 MessageKind::Warning);
                break;
            }
            if (!event.text.empty()) {
                if (renameQuery_.size() + event.text.size() >
                    kMaxRenameBytes) {
                    setStatusMessage(
                        std::string(kHelpNombrar) + renameQuery_ +
                            " [max 255 bytes]",
                        MessageKind::Error,
                        promptCursor(kHelpNombrar, renameQuery_),
                        promptBoldPrefix(kHelpNombrar));
                } else {
                    renameQuery_ += event.text;
                    setStatusMessage(
                        std::string(kHelpNombrar) + renameQuery_,
                        MessageKind::Prompt,
                        promptCursor(kHelpNombrar, renameQuery_),
                        promptBoldPrefix(kHelpNombrar));
                }
            }
            break;
        }
        case InputEventType::Paste: {
            auto maybe = clipboard_->paste();
            if (!maybe || maybe->empty()) {
                break;
            }

            const std::string& raw = *maybe;

            // Mismo contrato que InsertChar: si el pegado contiene ruta,
            // se rechaza entero. No se filtra parcialmente.
            if (raw.find('/') != std::string::npos ||
                raw.find('\\') != std::string::npos) {
                setActionMessage("El nombre no puede contener rutas.",
                                 MessageKind::Warning);
                break;
            }

            // Un paste puede traer \r\n, \n, \t, NUL u otros controles.
            // En un prompt de nombre eso no debería entrar.
            // Se conservan bytes UTF-8 válidos (>= 0x80).
            std::string clean;
            clean.reserve(raw.size());

            for (char ch : raw) {
                const unsigned char uc = static_cast<unsigned char>(ch);

                // Consideramos imprimibles/sanos desde 0x20, excluyendo DEL.
                if (uc >= 0x20 && uc != 0x7F) {
                    clean.push_back(ch);
                }
            }

            if (clean.empty()) {
                break;
            }

            // Respetar el límite actual del rename.
            if (renameQuery_.size() + clean.size() > kMaxRenameBytes) {
                setStatusMessage(
                    std::string(kHelpNombrar) + renameQuery_ +
                        " [max 255 bytes]",
                    MessageKind::Error,
                    promptCursor(kHelpNombrar, renameQuery_),
                    promptBoldPrefix(kHelpNombrar));
                break;
            }

            renameQuery_ += clean;

            setStatusMessage(
                std::string(kHelpNombrar) + renameQuery_,
                MessageKind::Prompt,
                promptCursor(kHelpNombrar, renameQuery_),
                promptBoldPrefix(kHelpNombrar));

            break;
        }
        case InputEventType::Backspace:
            if (!renameQuery_.empty()) {
                int cols = utf8::columnOf(
                    renameQuery_,
                    static_cast<int>(renameQuery_.size()));
                renameQuery_ =
                    utf8::truncate(renameQuery_, cols - 1);
                setStatusMessage(
                    std::string(kHelpNombrar) + renameQuery_,
                    MessageKind::Prompt,
                    promptCursor(kHelpNombrar, renameQuery_),
                    promptBoldPrefix(kHelpNombrar));
            }
            break;
        case InputEventType::InsertNewline:
            commitRename();
            break;
        case InputEventType::Escape:
            renameQuery_.clear();
            state_ = priorState_;
            setStatusMessage("", MessageKind::Info);
            setActionMessage("Renombrado cancelado.",
                             MessageKind::Warning);
            break;
        default:
            break;
    }
}
