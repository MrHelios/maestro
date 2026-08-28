#include "ui/Editor.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <poll.h>
#include <signal.h>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

#include "core/utf8.h"
#include "clipboard/FakeClipboard.h"
#include "clipboard/X11Clipboard.h"
#include "filesystem/InotifyFileWatcher.h"
#include "filesystem/NullFileWatcher.h"

namespace {

// Mensajes de ayuda fijos por modo (v?.?): centralizados aca para que
// cambiar el texto sea tocar un solo lugar, no buscarlo repetido por el
// archivo. Antes "ABRIR: ..." estaba duplicado en startFileBrowser() y
// fileBrowserEnterSelected(); con esto solo hay una fuente de verdad.
constexpr const char* kHelpBufferSelector =
    "ENTER: open | ESC: cancel | \u2191/\u2193: move";
constexpr const char* kHelpSaveAsPrompt = "Save file: ";
constexpr const char* kHelpBusqueda = "Find: ";
constexpr const char* kHelpNavegacion =
    "NAVEGACION: i escribir | s seleccionar | c/x copiar/cortar | p pegar | "
    "Ctrl+K buffer/guardar/salir | Ctrl+U/Y deshacer/rehacer";
constexpr const char* kHelpEmpty = "";
constexpr const char* kHelpPrefix =
    "command: Ctrl+k";
}

namespace {

inline int gutterWidthFor(int totalLines) {
    int digits = 1;
    for (int n = totalLines; n >= 10; n /= 10) ++digits;
    return std::max(3, digits + 1);
}

inline int textWidthFor(const Viewport& vp, int totalLines) {
    int gutterW = std::min(gutterWidthFor(totalLines), vp.width);
    int w = vp.width - gutterW;
    return w < 0 ? 0 : w;
}

} // namespace

namespace {

// Convierte una ruta a su forma CANONICA-LEXICA: absoluta contra cwd() y
// con "." / ".." reducidos ("foo/../bar" -> "bar"). Unifica rutas que
// apuntan al mismo archivo pero se escriben distinto, de modo que el
// chequeo de duplicados de buffers funcione ("a/../b" == "b").
//
// NO resuelve symlinks: eso requiere filesystem::canonical(), que exige
// que el archivo EXISTA y falla sobre archivos nuevos (que el editor
// tambien abre). Resolver symlinks queda como limitacion documentada.
std::string resolveAbsolutePath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    return std::filesystem::absolute(path, ec).lexically_normal().string();
}

static Buffer::FileIdentity captureIdentity(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return Buffer::FileIdentity{};
    Buffer::FileIdentity id;
    id.valid = true;
    id.dev = st.st_dev;
    id.ino = st.st_ino;
    id.size = st.st_size;
    id.mtime = st.st_mtim;
    return id;
}

} // namespace

Editor::Editor() : Editor(std::make_unique<X11Clipboard>()) {}

Editor::Editor(std::unique_ptr<SystemClipboard> clipboard)
    : Editor(std::move(clipboard), std::make_unique<InotifyFileWatcher>()) {}

Editor::Editor(std::unique_ptr<SystemClipboard> clipboard, std::unique_ptr<FileWatcher> watcher)
    : clipboard_(std::move(clipboard)), watcher_(std::move(watcher)) {
    if (!clipboard_) clipboard_ = std::make_unique<FakeClipboard>();
    if (!watcher_) watcher_ = std::make_unique<NullFileWatcher>();
    setStatusMessage(kHelpPrefix);
    registerCommands();
}

Editor::~Editor() = default;

void Editor::watchFile(const std::string& path) {
    if (path.empty() || !watcher_) return;
    if (watchedFiles_.find(path) != watchedFiles_.end()) return;
    watcher_->watch(path);
    watchedFiles_.insert(path);
}

void Editor::unwatchFile(const std::string& path) {
    if (path.empty() || !watcher_) return;
    if (watchedFiles_.find(path) == watchedFiles_.end()) return;
    for (int i = 0; i < buffers.count(); ++i) {
        if (buffers.at(i).filename == path) return;
    }
    watcher_->unwatch(path);
    watchedFiles_.erase(path);
}

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
                        b.savedLines = b.document.snapshot();
                        b.modified = false;
                        b.savedIdentity = captureIdentity(b.filename);
                        if (b.cursor.line >= b.document.lineCount()) b.cursor.line = b.document.lineCount() - 1;
                        if (b.cursor.line < 0) b.cursor.line = 0;
                        b.cursor.clampToLine(b.document);
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
            b.savedLines = b.document.snapshot();
            b.modified = false;
            b.savedIdentity = captureIdentity(b.filename);
            if (b.cursor.line >= b.document.lineCount()) b.cursor.line = b.document.lineCount() - 1;
            if (b.cursor.line < 0) b.cursor.line = 0;
            b.cursor.clampToLine(b.document);
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

std::string Editor::blockToString(const std::vector<std::string>& block) {
    if (block.empty()) return "";
    std::string s = block[0];
    for (size_t i = 1; i < block.size(); ++i) { s += "\n"; s += block[i]; }
    return s;
}

std::vector<std::string> Editor::stringToBlock(const std::string& text) {
    if (text.empty()) return {};
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            out.emplace_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::vector<std::string> Editor::getClipboardBlock() const {
    auto opt = clipboard_->paste();
    if (!opt) return {};
    return stringToBlock(*opt);
}

void Editor::setClipboardBlock(const std::vector<std::string>& block) {
    clipboard_->copy(blockToString(block));
}

std::string Editor::getClipboardText() const {
    auto opt = clipboard_->paste();
    return opt ? *opt : "";
}

bool Editor::isClipboardEmpty() const {
    auto opt = clipboard_->paste();
    return !opt || opt->empty();
}

void Editor::setStatusMessage(const std::string& msg, MessageKind kind) {
    statusMessage_ = Message{msg, kind, std::nullopt};
}

void Editor::setActionMessage(const std::string& msg, MessageKind kind) {
    statusMessage_ = Message{msg, kind,
                             std::chrono::steady_clock::now() + kActionMessageTimeout};
}

void Editor::clearExpiredActionMessage() {
    // Un mensaje de accion expira por si solo pasados los 5 segundos. Los
    // mensajes persistentes NO se tocan (persistent() es true).
    if (statusMessage_.persistent()) return;
    if (statusMessage_.expired()) {
        statusMessage_ = Message{};
    }
}

void Editor::registerCommands() {
    // --- Navegacion ---
    commands_.registerCommand("navegacion.interaccion", [this] {
        state_ = State::Interaccion;
        setStatusMessage(kHelpEmpty);
    });
    commands_.registerCommand("navegacion.seleccion", [this] {
        beginSelection();
        state_ = State::Seleccion;
        setStatusMessage(kHelpEmpty);
    });
    commands_.registerCommand("navegacion.pegar", [this] {
        Buffer& b = active();
        auto maybe = clipboard_->paste();
        if (!maybe || maybe->empty()) {
            setActionMessage("Nada para pegar.", MessageKind::Warning);
            return;
        }
        auto block = stringToBlock(*maybe);
        if (block.empty()) {
            setActionMessage("Nada para pegar.", MessageKind::Warning);
            return;
        }
        auto sel = selection();
        HistoryEntry e = b.beginHistoryEntry();
        if (sel.has_value()) {
            auto removed = b.document.extractRange(sel->start.line, sel->start.col,
                                                   sel->end.line, sel->end.col);
            b.document.deleteRange(sel->start.line, sel->start.col,
                                   sel->end.line, sel->end.col);
            e.edits.push_back({EditType::Delete, sel->start, sel->end,
                               blockToString(removed)});
            b.cursor.line = sel->start.line;
            b.cursor.col = sel->start.col;
        }
        int insLine = b.cursor.line, insCol = b.cursor.col;
        Position end = b.document.insertBlock(insLine, insCol, block);
        e.edits.push_back({EditType::Insert, {insLine, insCol}, end, *maybe});
        b.cursor.line = end.line;
        b.cursor.col = end.col;
        b.modified = true;
        clearSelection();
        state_ = State::Navegacion;
        setActionMessage("Pegado.", MessageKind::Success);
        b.commitHistoryEntry(std::move(e));
    });
    commands_.registerCommand("navegacion.palabra.atras", [this] {
        active().cursor.moveToPreviousWord(active().document);
    });
    commands_.registerCommand("navegacion.palabra.adelante", [this] {
        active().cursor.moveToNextWord(active().document);
    });
    commands_.registerCommand("seleccion.total", [this] {
        Buffer& b = active();
        b.selectAllPrevious = b.selection;
        b.selection = selectAllSelection();
        b.selectAllActive = true;
        state_ = State::Seleccion;
        setStatusMessage(kHelpEmpty);
    });

    // --- Seleccion ---
    commands_.registerCommand("seleccion.j", [this] {
        beginSelection();
        active().cursor.moveToPreviousWord(active().document);
        updateSelectionPosition();
    });
    commands_.registerCommand("seleccion.k", [this] {
        beginSelection();
        active().cursor.moveToNextWord(active().document);
        updateSelectionPosition();
    });
    commands_.registerCommand("seleccion.copiar", [this] {
        Buffer& b = active();
        bool hadSelection = hasSelection();
        bool ok = true;
        if (hadSelection) {
            auto sel = selection();
            auto block = b.document.extractRange(sel->start.line, sel->start.col,
                                                 sel->end.line, sel->end.col);
            ok = clipboard_->copy(blockToString(block));
        }
        clearSelection();
        state_ = State::Navegacion;
        if (!hadSelection) setActionMessage("Nada seleccionado.");
        else if (!ok) setActionMessage("Error al copiar al portapapeles.", MessageKind::Error);
        else setActionMessage("Copiado.");
    });
    commands_.registerCommand("seleccion.cortar", [this] {
        Buffer& b = active();
        bool hadSelection = hasSelection();
        if (hadSelection) {
            auto sel = selection();
            auto block = b.document.extractRange(sel->start.line, sel->start.col,
                                                 sel->end.line, sel->end.col);
            std::string text = blockToString(block);
            if (!clipboard_->copy(text)) {
                setActionMessage("Error al copiar al portapapeles.", MessageKind::Error);
                return;
            }
            HistoryEntry e = b.beginHistoryEntry();
            b.document.deleteRange(sel->start.line, sel->start.col,
                                   sel->end.line, sel->end.col);
            e.edits.push_back({EditType::Delete, sel->start, sel->end, text});
            b.cursor.line = sel->start.line;
            b.cursor.col = sel->start.col;
            b.modified = true;
            b.commitHistoryEntry(std::move(e));
        }
        clearSelection();
        state_ = State::Navegacion;
        setActionMessage(hadSelection ? "Cortado." : "Nada seleccionado.");
    });
    // '}' tabula el rango seleccionado hacia adentro; '{' le quita un nivel.
    // Solo tienen significado en modo Seleccion ('}' / '{' se ignoran en
    // el resto de modos) y solo si hay rango NO vacio.
    commands_.registerCommand("seleccion.indentar", [this] {
        indentSelection(true);
    });
    commands_.registerCommand("seleccion.desindentar", [this] {
        indentSelection(false);
    });

    // --- Prefijo (Ctrl+K) ---
    commands_.registerCommand("buffer.nuevo", [this] {
        createBuffer();
        state_ = State::Navegacion;
    });
    commands_.registerCommand("buffer.selector", [this] {
        if (buffers.count() <= 1) {
            setActionMessage("Solo hay un buffer.", MessageKind::Warning);
            state_ = priorState_;
        } else {
            bufferSelectorIndex_ = buffers.activeIndex();
            state_ = State::BufferSelector;
            setStatusMessage("Buffers: ↑/↓ mover | Enter abrir | ESC cancelar");
        }
    });
    commands_.registerCommand("buffer.cerrar", [this] {
        closeActiveBuffer();
    });
    commands_.registerCommand("buffer.abrir", [this] {
        startFileBrowser();
    });
    commands_.registerCommand("navegacion.buscar", [this] {
        startSearch();
    });
}

bool Editor::isDirectory(const std::string& path) {
    return FileBrowser::isDirectory(path);
}

Buffer& Editor::active() {
    return buffers.active();
}

const Buffer& Editor::active() const {
    return buffers.active();
}

bool Editor::openFile(const std::string& path) {
    // v0.6.2: solo archivos. Una carpeta no se trata como archivo
    // nuevo: se rechaza y el editor queda como estaba.
    if (isDirectory(path)) {
        setActionMessage("No se pueden abrir carpetas.", MessageKind::Error);
        return false;
    }

    Buffer& b = active();
    // La barra de estado muestra siempre una ruta absoluta: si el
    // archivo se abrio con ruta relativa, la resolvemos contra cwd().
    const std::string oldFilename = b.filename;
    b.filename = resolveAbsolutePath(path);
    LoadResult result = b.document.loadFromFile(path);

    if (result != LoadResult::Success && result != LoadResult::NotFound) {
        // Error real (permisos, E/S): NO se trata como archivo nuevo y no se
        // toca el documento, para no aparentar que un archivo existente sin
        // permisos es nuevo (eso llevaria a sobrescribirlo desde cero).
        b.filename = oldFilename;
        setActionMessage((result == LoadResult::PermissionDenied)
                                  ? "Sin permisos de lectura: " + path
                                  : "No se pudo leer: " + path,
                         MessageKind::Error);
        return false;
    }
    b.modified = false;
    b.savedLines = b.document.snapshot();
    b.savedIdentity = (result == LoadResult::Success) ? captureIdentity(b.filename) : Buffer::FileIdentity{};
    b.cursor.line = 0;
    b.cursor.col = 0;
    b.selection.reset();
    b.selectAllActive = false;
    b.selectAllPrevious.reset();
    if (oldFilename != b.filename && !oldFilename.empty()) {
        bool stillNeeded = false;
        for (int i = 0; i < buffers.count(); ++i) {
            if (&buffers.at(i) == &b) continue;
            if (buffers.at(i).filename == oldFilename) { stillNeeded = true; break; }
        }
        if (!stillNeeded) {
            watchedFiles_.erase(oldFilename);
            if (watcher_) watcher_->unwatch(oldFilename);
        }
    }
    watchFile(b.filename);
    state_ = State::Navegacion;
    setStatusMessage("");
    if (result == LoadResult::NotFound) {
        setActionMessage("Archivo nuevo: " + path, MessageKind::Success);
    }
    return result == LoadResult::Success;
}

void Editor::handleResize() {
    for (int i = 0; i < buffers.count(); ++i) {
        syncViewportSize(buffers.at(i));
        buffers.at(i).cursor.clampToLine(buffers.at(i).document);
    }
}

void Editor::syncViewportSize(Buffer& b) {
    int rows, cols;
    terminal_.getWindowSize(rows, cols);
    BufferManager::fitViewport(b, rows, cols);
}

void Editor::createBuffer() {
    int rows, cols;
    terminal_.getWindowSize(rows, cols);
    const int idx = buffers.createBuffer(rows, cols);
    setActionMessage("Buffer nuevo: " + buffers.at(idx).unnamedName, MessageKind::Success);
}

void Editor::closeActiveBuffer() {
    Buffer& cur = active();
    std::string oldPath = cur.filename;
    int rows, cols;
    terminal_.getWindowSize(rows, cols);
    auto cr = buffers.closeActive(rows, cols);
    if (cr != CloseResult::ModifiedBlocked && !oldPath.empty()) {
        bool stillNeeded = false;
        for (int i = 0; i < buffers.count(); ++i) {
            if (buffers.at(i).filename == oldPath) { stillNeeded = true; break; }
        }
        if (!stillNeeded) {
            watchedFiles_.erase(oldPath);
            if (watcher_) watcher_->unwatch(oldPath);
        }
    }
    switch (cr) {
        case CloseResult::ModifiedBlocked:
            setActionMessage("Buffer modificado: guarda con Ctrl+K s o restaura.", MessageKind::Warning);
            state_ = priorState_;
            break;
        case CloseResult::ResetLast:
            setActionMessage("Buffer reiniciado: " + active().unnamedName);
            state_ = State::Navegacion;
            break;
        case CloseResult::Removed: {
            // UX (v0.8): cerrar NO abre el selector. El buffer que heredo
            // la ranura (misma posicion, clamp al final) queda activo de
            // inmediato. Para elegir deliberadamente existe Ctrl+K t.
            const bool hasSelection = buffers.activate(buffers.activeIndex());
            state_ = hasSelection ? State::Seleccion : State::Navegacion;
            setActionMessage("Buffer cerrado. Activo: " + active().displayName());
            break;
        }
    }
}

void Editor::activateBuffer(int idx) {
    const bool hasSelection = buffers.activate(idx);
    // Reconciliar el modo global con el estado del buffer activado: un
    // buffer con rango seleccionado deja el editor en Seleccion; sin
    // rango, en Navegacion. (La seleccion y los demas estados son del
    // buffer, no del Editor.)
    state_ = hasSelection ? State::Seleccion : State::Navegacion;
    setStatusMessage("");
}

std::vector<std::string> Editor::bufferNames() const {
    return buffers.names();
}

void Editor::handleBufferSelectorEvent(const Event& event) {
    switch (event.type) {
        case EventType::MoveUp:
            if (bufferSelectorIndex_ > 0) bufferSelectorIndex_--;
            break;
        case EventType::MoveDown:
            if (bufferSelectorIndex_ + 1 < buffers.count())
                bufferSelectorIndex_++;
            break;
        case EventType::InsertNewline: // Enter: abrir el buffer seleccionado
            activateBuffer(bufferSelectorIndex_);
            break;
        case EventType::Escape:
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
    fileBrowser.start();
    const std::string err = fileBrowser.reload();
    if (!err.empty()) {
        setActionMessage(err, MessageKind::Error);
    } else {
        //setStatusMessage("MOVER: ↑/↓ | ABRIR: enter | CANCEL: esc");
        setStatusMessage(kHelpBufferSelector);
    }
    state_ = State::FileBrowser;
}

void Editor::handleFileBrowserEvent(const Event& event) {
    switch (event.type) {
        case EventType::MoveUp:
            fileBrowser.moveUp();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case EventType::MoveDown:
            fileBrowser.moveDown();
            fileBrowser.clampScroll(active().viewport.height);
            break;
        case EventType::InsertNewline: // Enter: abrir/entrar la seleccion
            fileBrowserEnterSelected();
            break;
        case EventType::Escape:
        case EventType::Prefix: // Ctrl+K dentro tambien cancela (modal puro)
            state_ = priorState_;
            setStatusMessage("");
            break;
        // Cualquier otra tecla es no-op: pantalla modal, nada se filtra.
        default:
            break;
    }
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
            openFileToBuffer(fileBrowser.pendingPath());
            break;
    }
}

void Editor::openFileToBuffer(const std::string& path) {
    // Normalizar la ruta ANTES de comparar y guardar, para que el chequeo
    // de duplicados funcione aunque dos rutas escriban el mismo archivo de
    // forma distinta ("foo/../bar" == "bar", "." y "..", etc).
    const std::string filePath = resolveAbsolutePath(path);

    // Si ya hay un buffer con esta ruta, se activa ese buffer en vez de
    // crear otro (v0.6.4: no duplicar archivos abiertos).
    for (int i = 0; i < buffers.count(); ++i) {
        if (buffers.at(i).filename == filePath) {
            buffers.activate(i);
            state_ = State::Navegacion;
            setStatusMessage("");
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
    nuevo.modified = false;
    nuevo.savedLines = nuevo.document.snapshot();
    nuevo.savedIdentity = (result == LoadResult::Success) ? captureIdentity(filePath) : Buffer::FileIdentity{};
    nuevo.cursor.line = 0;
    nuevo.cursor.col = 0;
    nuevo.selection.reset();
    nuevo.selectAllActive = false;
    nuevo.selectAllPrevious.reset();
    watchFile(nuevo.filename);
    buffers.push(std::move(nuevo));
    state_ = State::Navegacion;
    if (result == LoadResult::Success) {
        setStatusMessage("");
    } else {
        setActionMessage("Archivo nuevo: " + path, MessageKind::Success);
    }
}

void Editor::run() {
    for (int i = 0; i < buffers.count(); ++i) {
        syncViewportSize(buffers.at(i));
    }

    terminal_.enableRawMode();

    sigset_t blockMask, origMask;
    sigemptyset(&blockMask);
    sigaddset(&blockMask, SIGWINCH);
    sigprocmask(SIG_BLOCK, &blockMask, &origMask);

    {
        Buffer& b = active();
        b.viewport.scrollToCursor(b.cursor, b.document, textWidthFor(b.viewport, b.document.lineCount()));
        renderer_.renderScreenDiff(b.document, b.cursor, b.viewport,
                                   b.filename, b.modified, statusMessage_,
                                   state_, b.selection, searchHighlight_);
    }

    while (running_) {
        if (terminal_.hasResized()) {
            handleResize();
            renderFrame();
            continue;
        }

        int waitMs = -1;
        if (clipboard_ && clipboard_->hasPending()) {
            waitMs = 20;
        }
        if (statusMessage_.expiry) {
            const auto remaining = *statusMessage_.expiry -
                                   std::chrono::steady_clock::now();
            const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                remaining).count();
            int msgMs = ms <= 0 ? 0 : static_cast<int>(std::min<long>(ms, INT_MAX));
            if (waitMs < 0) waitMs = msgMs;
            else waitMs = std::min(waitMs, msgMs);
        }
        if (clipboard_) clipboard_->processEvents();
        int cfd = clipboard_ ? clipboard_->fd() : -1;
        struct pollfd pfds[3];
        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        int nfds = 1;
        int clipboardIdx = -1;
        if (cfd >= 0) {
            clipboardIdx = nfds;
            pfds[nfds].fd = cfd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        int watcherIdx = -1;
        int wfd = watcher_ ? watcher_->fd() : -1;
        if (wfd >= 0) {
            watcherIdx = nfds;
            pfds[nfds].fd = wfd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        struct timespec ts;
        struct timespec* tsp = nullptr;
        if (waitMs >= 0) {
            ts.tv_sec = waitMs / 1000;
            ts.tv_nsec = (waitMs % 1000) * 1000000L;
            tsp = &ts;
        }
        int pr = ppoll(pfds, nfds, tsp, &origMask);
        if (pr < 0) {
            if (errno == EINTR && terminal_.hasResized()) {
                handleResize();
                renderFrame();
            }
            continue;
        }
        if (pr == 0) {
            clearExpiredActionMessage();
            renderFrame();
            continue;
        }
        bool xReady = (clipboardIdx >= 0 && (pfds[clipboardIdx].revents & POLLIN));
        bool inReady = (pfds[0].revents & POLLIN);
        bool watcherReady = (watcherIdx >= 0 && (pfds[watcherIdx].revents & POLLIN));
        if (xReady) clipboard_->processEvents();
        if (watcherReady && watcher_) {
            watcher_->pollEvents([this](const FileChangeEvent& ev) {
                handleFileChange(ev);
            });
        }
        if (inReady) {
            Event event;
            if (!terminal_.readEvent(event, 0)) {
                if (xReady || watcherReady) continue;
            } else {
                handleEvent(event);
                if (!running_) break;
                clearExpiredActionMessage();
                renderFrame();
            }
        } else if (xReady || watcherReady) {
            clearExpiredActionMessage();
        }
    }

    sigprocmask(SIG_SETMASK, &origMask, nullptr);
    terminal_.disableRawMode();
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[0 q", 12);
}

void Editor::renderFrame() {
    Buffer& b = active();
    if (state_ == State::BufferSelector) {
        // Pantalla del selector: se dibuja la lista de buffers con la
        // barra MULTIBUFFER al final, manteniendo el aspecto del editor.
        renderer_.renderBufferList(bufferNames(), bufferSelectorIndex_,
                                   b.viewport.width, b.viewport.height);
    } else if (state_ == State::FileBrowser) {
        // Pantalla del explorador de archivos: la lista con la ruta
        // actual en la barra de estado y la ayuda en la fila de mensajes.
        renderer_.renderFileList(fileBrowser.displayNames_,
                                 fileBrowser.index_, fileBrowser.scroll_,
                                 fileBrowser.path_, statusMessage_,
                                 b.viewport.width, b.viewport.height);
    } else {
        b.viewport.scrollToCursor(b.cursor, b.document, textWidthFor(b.viewport, b.document.lineCount()));
        renderer_.renderScreenDiff(b.document, b.cursor, b.viewport,
                                   b.filename, b.modified, statusMessage_,
                                   state_, b.selection, searchHighlight_);
    }
}

void Editor::handleEvent(const Event& event) {
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

    // v0.7: en el prompt "Guardar archivo:" solo se aceptan caracteres,
    // Backspace, Enter y ESC; nada se filtra a la edicion ni a otros modos.
    if (state_ == State::SaveAs) {
        handleSaveAsEvent(event);
        return;
    }

    if (state_ == State::Busqueda) {
        handleBusquedaEvent(event);
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
        setStatusMessage(kHelpPrefix);
        return;
    }

    // El grupo de escritura coalescente solo sobrevive mientras siga
    // habiendo InsertChar consecutivos dentro de Interaccion tras haber
    // empezado el grupo con un reemplazo de seleccion. Cualquier otro
    // evento lo sella: la proxima escritura empieza un grupo nuevo (por
    // caracter, si es escritura normal).
    if (!(state_ == State::Interaccion &&
          event.type == EventType::InsertChar && coalescingTyping_)) {
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
            } else if (event.text == "f") {
                commands_.execute("navegacion.buscar");
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
        case EventType::InsertChar:
            // Edicion libre real: cualquier letra (incluida i/s/p/c/x)
            // se inserta como texto. Aqui no son comandos de modo.
            // P0 interaction: si venimos de un reemplazo de seleccion
            // (la entrada del grupo ya esta en el historial), NO creamos
            // una nueva: el texto consecutivo se absorbe en la MISMA
            // entrada de undo via extendLastEntry.
            if (!coalescingTyping_) {
                HistoryEntry e = b.beginHistoryEntry();
                Position start{b.cursor.line, b.cursor.col};
                // La posicion final la calcula Document (UTF-8, '\n').
                Position end = b.document.insertText(start.line, start.col,
                                                     event.text);
                e.edits.push_back({EditType::Insert, start, end, event.text});
                b.cursor.line = end.line;
                b.cursor.col = end.col;
                b.modified = true;
                b.commitHistoryEntry(std::move(e));
            } else {
                Position start{b.cursor.line, b.cursor.col};
                Position end = b.document.insertText(start.line, start.col,
                                                     event.text);
                b.extendLastEntry({EditType::Insert, start, end, event.text});
                b.cursor.line = end.line;
                b.cursor.col = end.col;
                b.modified = true;
            }
            break;

        case EventType::InsertNewline: {
            HistoryEntry e = b.beginHistoryEntry();
            Position at{b.cursor.line, b.cursor.col};
            b.document.splitLine(at.line, at.col);
            e.edits.push_back({EditType::SplitLine, at,
                               {at.line + 1, 0}, ""});
            b.cursor.line++;
            b.cursor.col = 0;
            b.modified = true;
            b.commitHistoryEntry(std::move(e));
            break;
        }

        case EventType::Backspace:
        case EventType::Delete: {
            // interaction P0: si hay una seleccion vigente (p.ej. por entrar
            // a edicion sin cancelarla antes), Backspace/Delete borran el
            // rango entero en vez de un solo caracter. deleteSelection()
            // ya registra su propia entrada de historial.
            if (deleteSelection()) break;
            HistoryEntry e = b.beginHistoryEntry();
            if (event.type == EventType::Backspace) {
                bool willMergeLines = (b.cursor.col == 0 && b.cursor.line > 0);
                if (willMergeLines) {
                    int prevLineLen = b.document.lineLength(b.cursor.line - 1);
                    Position nl{b.cursor.line - 1, prevLineLen};
                    b.document.mergeLine(nl.line);
                    e.edits.push_back({EditType::MergeLine, nl,
                                       {b.cursor.line, 0}, ""});
                    b.cursor.line--;
                    b.cursor.col = prevLineLen;
                    b.modified = true;
                } else {
                    int line = b.cursor.line, col = b.cursor.col;
                    std::string removed =
                        b.document.cellTextBefore(line, col);
                    // deleteCharBefore devuelve cuantos bytes borro dentro
                    // de la linea (el largo del caracter UTF-8 completo).
                    int deleted = b.document.deleteCharBefore(line, col);
                    if (deleted > 0) {
                        e.edits.push_back({EditType::Delete, {line, col - deleted},
                                           {line, col}, removed});
                        b.cursor.col -= deleted;
                        b.modified = true;
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
                        b.modified = true;
                    }
                } else if (line + 1 < b.document.lineCount()) {
                    // Delete al final de linea: funde la linea siguiente.
                    b.document.mergeLine(line);
                    e.edits.push_back({EditType::MergeLine, {line, col},
                                       {line + 1, 0}, ""});
                    b.modified = true;
                }
            }
            b.commitHistoryEntry(std::move(e));
            break;
        }

        case EventType::Escape:
            state_ = State::Navegacion;
            setStatusMessage(kHelpEmpty);
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
                HistoryEntry e = b.beginHistoryEntry();   // UNA entrada para el grupo
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
                b.modified = true;
                clearSelection();
                state_ = State::Interaccion;
                coalescingTyping_ = hadSel;      // solo el reemplazo coalesce
                b.commitHistoryEntry(std::move(e));
                setActionMessage("Reemplazando seleccion...");
            }
            break;
        case EventType::Escape:
            clearSelection();
            state_ = State::Navegacion;
            setActionMessage("Seleccion cancelada.");
            break;

        // Delete/Backspace si borran el rango seleccionado (interaction P0)
        // y vuelven a navegacion.
        case EventType::Delete:
        case EventType::Backspace:
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
                setStatusMessage("SELECCION");
            } else if (event.text == "c" || event.text == "x") {
                bool hadSelection = hasSelection();
                if (hadSelection) {
                    auto sel = selection();
                    auto block = b.document.extractRange(sel->start.line,
                                                         sel->start.col,
                                                         sel->end.line,
                                                         sel->end.col);
                    std::string text = blockToString(block);
                    if (!clipboard_->copy(text)) {
                        setActionMessage("Error al copiar al portapapeles.", MessageKind::Error);
                        return;
                    }
                    if (event.text == "x") {
                        HistoryEntry e = b.beginHistoryEntry();
                        b.document.deleteRange(sel->start.line, sel->start.col,
                                               sel->end.line, sel->end.col);
                        e.edits.push_back({EditType::Delete, sel->start, sel->end,
                                           text});
                        b.cursor.line = sel->start.line;
                        b.cursor.col = sel->start.col;
                        b.modified = true;
                        b.commitHistoryEntry(std::move(e));
                    }
                }
                clearSelection();
                b.selectAllPrevious.reset();
                b.selectAllActive = false;
                state_ = State::Navegacion;
                setActionMessage(hadSelection ? (event.text == "x" ? "Cortado."
                                                                    : "Copiado.")
                                              : "Nada seleccionado.");
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
            setStatusMessage("SELECCION");
            break;
        }
        case EventType::MoveLeft:
        case EventType::MoveUp:
            b.cursor.line = 0;
            b.cursor.col = 0;
            b.selection = Selection{{0, 0}, {0, 0}};
            b.selectAllActive = false;
            b.selectAllPrevious.reset();
            setStatusMessage("SELECCION");
            break;

        // ESC: cancela la seleccion total (y toda seleccion) y vuelve a
        // navegacion, igual que en el modo Seleccion normal.
        case EventType::Escape:
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
        case EventType::Delete:
        case EventType::Backspace:
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

std::optional<Selection> Editor::selectAllSelection() const {
    const Buffer& b = active();
    int last = b.document.lineCount() - 1;
    Position end{last, b.document.lineLength(last)};
    return Selection{{0, 0}, end};
}

void Editor::handlePrefixKey(const Event& event) {
    switch (event.type) {
        case EventType::Save:
            startSaveAs();
            break;

        case EventType::Quit:
            running_ = false;
            break;

        // v0.6.3: comandos de buffer dentro del prefijo. El mapeo tecla ->
        // comando -> handler vive en commands_.
        case EventType::InsertChar:
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
                if (active().filename.empty()) {
                    startSaveAs();
                    break;
                }
                save();
                state_ = priorState_;
                break;
            }
            if (event.text == "q") {           // Ctrl+K q: salida segura
                bool anyModified = false;
                for (int i = 0; i < buffers.count(); ++i) {
                    if (buffers.at(i).modified) { anyModified = true; break; }
                }
                if (anyModified) {
                    state_ = priorState_;
                    setActionMessage("Hay archivos sin guardar", MessageKind::Warning);
                } else {
                    running_ = false;
                }
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

void Editor::startSaveAs() {
    if (active().filename.empty()) {
        std::string cwd = FileBrowser::getCwd();
        if (!cwd.empty()) {
            saveAsPath_ = cwd;
            if (saveAsPath_.back() != '/') saveAsPath_ += '/';
        } else {
            saveAsPath_.clear();
        }
    } else {
        saveAsPath_ = active().filename;
    }
    state_ = State::SaveAs;
    setStatusMessage(kHelpSaveAsPrompt + saveAsPath_, MessageKind::Prompt);
}

void Editor::handleSaveAsEvent(const Event& event) {
    switch (event.type) {
        case EventType::InsertChar:
            saveAsPath_ += event.text;
            setStatusMessage(kHelpSaveAsPrompt + saveAsPath_,
                             MessageKind::Prompt);
            break;
        case EventType::Backspace:
            if (!saveAsPath_.empty()) {
                int cols = utf8::columnOf(saveAsPath_,
                                          static_cast<int>(saveAsPath_.size()));
                saveAsPath_ = utf8::truncate(saveAsPath_, cols - 1);
            }
            setStatusMessage(kHelpSaveAsPrompt + saveAsPath_,
                             MessageKind::Prompt);
            break;
        case EventType::InsertNewline: // Enter: guardar en la ruta escrita
            commitSaveAs();
            break;
        case EventType::Escape:
            state_ = priorState_;
            setActionMessage("Guardado cancelado.", MessageKind::Warning);
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
        setStatusMessage(kHelpSaveAsPrompt, MessageKind::Prompt);
        return;
    }
    if (isDirectory(path)) {
        setActionMessage("Es una carpeta: " + path, MessageKind::Error);
        return;
    }
    bool isNew = b.filename != path;
    std::string oldPath = b.filename;
    if (b.document.saveToFile(path)) {
        if (isNew && !oldPath.empty()) {
            bool stillNeeded = false;
            for (int i = 0; i < buffers.count(); ++i) {
                if (&buffers.at(i) == &b) continue;
                if (buffers.at(i).filename == oldPath) { stillNeeded = true; break; }
            }
            if (!stillNeeded) {
                watchedFiles_.erase(oldPath);
                if (watcher_) watcher_->unwatch(oldPath);
            }
        }
        b.filename = path;
        b.modified = false;
        b.savedLines = b.document.snapshot();
        b.savedIdentity = captureIdentity(path);
        if (watchedFiles_.find(b.filename) == watchedFiles_.end()) {
            watcher_->watch(b.filename);
            watchedFiles_.insert(b.filename);
        }
        setActionMessage("Guardado: " + path, MessageKind::Success);
        state_ = priorState_;
    } else {
        setActionMessage("Error al guardar: " + path, MessageKind::Error);
    }
}

void Editor::startSearch() {
    searchQuery_.clear();
    searchOrigin_ = {active().cursor.line, active().cursor.col};
    clearSearchHighlight();
    state_ = State::Busqueda;
    setStatusMessage(std::string(kHelpBusqueda), MessageKind::Prompt);
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
    setStatusMessage(msg, MessageKind::Prompt);
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
    setSearchHighlight(matches[next], static_cast<int>(searchQuery_.size()));
    centerViewportOnCursor();
    updateSearchMessage(true, next + 1, static_cast<int>(matches.size()));
}

void Editor::handleBusquedaEvent(const Event& event) {
    switch (event.type) {
        case EventType::InsertChar:
            searchQuery_ += event.text;
            updateSearch();
            break;
        case EventType::Backspace:
            if (!searchQuery_.empty()) {
                int cols = utf8::columnOf(searchQuery_, static_cast<int>(searchQuery_.size()));
                searchQuery_ = utf8::truncate(searchQuery_, cols - 1);
            }
            updateSearch();
            break;
        case EventType::MoveDown:
            navigateSearch(+1);
            break;
        case EventType::MoveUp:
            navigateSearch(-1);
            break;
        case EventType::InsertNewline:
            clearSearchHighlight();
            state_ = State::Navegacion;
            searchQuery_.clear();
            setStatusMessage("", MessageKind::Info);
            break;
        case EventType::Escape: {
            Buffer& b = active();
            b.cursor.line = searchOrigin_.line;
            b.cursor.col = searchOrigin_.col;
            b.cursor.clampToLine(b.document);
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

    // Caso 1: Archivo pequeno (cabe entero en una pagina). RePag -> inicio,
    // AvPag -> final, sin nunca quedar fuera del documento.
    if (h >= count) {
        b.viewport.top = 0;
        b.cursor.line = (dir < 0) ? 0 : count - 1;
        b.cursor.clampToLine(b.document);
        return;
    }

    // Caso 2: Archivo mas grande que la pantalla. Conservamos la posicion
    // relativa del cursor, PERO si el viewport YA estaba pegado al borde (no
    // puede moverse mas), el cursor se imanta a ese borde (acceso rapido al
    // inicio/final). Si el viewport si se movio, se conserva la posicion
    // relativa.
    const int rel = b.cursor.line - b.viewport.top; // posicion relativa (0..h-1)
    const int maxTop = count - h;
    if (dir < 0) { // RePag (Subir)
        const bool alreadyAtTop = (b.viewport.top == 0);
        b.viewport.top = std::max(0, b.viewport.top - h);
        if (alreadyAtTop) {
            b.cursor.line = 0; // ya estaba arriba: cursor al principio
        } else {
            b.cursor.line = b.viewport.top + rel;
        }
    } else { // AvPag (Bajar)
        const bool alreadyAtBottom = (b.viewport.top == maxTop);
        b.viewport.top = std::min(maxTop, b.viewport.top + h);
        if (alreadyAtBottom) {
            b.cursor.line = count - 1; // ya estaba abajo: cursor al final
        } else {
            b.cursor.line = b.viewport.top + rel;
        }
    }

    // Clamp final de seguridad: el cursor nunca queda fuera de los limites.
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
        setActionMessage("Archivo sin nombre: usa Ctrl+K Ctrl+S para elegir ruta.", MessageKind::Warning);
        return;
    }
    if (b.document.saveToFile(b.filename)) {
        b.modified = false;
        b.savedLines = b.document.snapshot();
        b.savedIdentity = captureIdentity(b.filename);
        watchFile(b.filename);
        setActionMessage("Guardado.", MessageKind::Success);
    } else {
        setActionMessage("Error al guardar.", MessageKind::Error);
    }
}

bool Editor::deleteSelection() {
    Buffer& b = active();
    if (!hasSelection()) return false;
    auto sel = selection();
    HistoryEntry e = b.beginHistoryEntry();
    auto removed = b.document.extractRange(sel->start.line, sel->start.col,
                                           sel->end.line, sel->end.col);
    b.document.deleteRange(sel->start.line, sel->start.col,
                           sel->end.line, sel->end.col);
    e.edits.push_back({EditType::Delete, sel->start, sel->end,
                       blockToString(removed)});
    b.cursor.line = sel->start.line;
    b.cursor.col = sel->start.col;
    b.modified = true;
    clearSelection();
    b.commitHistoryEntry(std::move(e));
    return true;
}

void Editor::indentSelection(bool indent) {
    Buffer& b = active();
    if (!hasSelection()) {
        setActionMessage("Nada seleccionado.", MessageKind::Warning);
        return;
    }
    auto sel = selection();

    // Ancho de una tabulacion (en espacios). Un solo '}' / '{' mueve un
    // nivel. Si mas adelante se configura ancho de tab, esto es el lugar.
    constexpr int kIndentLen = 4;

    // La tabulacion aplica a las lineas COMPLETAS que toca la seleccion.
    // Como el rango es [start, end) con end exclusivo, la ultima linea
    // (sel->end.line) SOLO cuenta si la seleccion llega hasta dentro de
    // ella (sel->end.col > 0): si termina justo en su columna 0, esa linea
    // no quedo realmente incluida y no se tabula. Mismo criterio que usa el
    // Renderer para decidir el salto de linea seleccionado (endsAtStart) y
    // que el resto del codigo distingue a proposito. No puede darse single
    // line + end.col == 0 con una seleccion no vacia, pero el guard lo deja
    // explicito igual.
    int firstLine = sel->start.line;
    int lastLine = sel->end.line;
    if (sel->end.col == 0 && lastLine > firstLine) {
        --lastLine;
    }

    // Una sola entrada de historial cubre el rango entero, de modo que el
    // '}' / '{' se deshace en UNA sola operacion. Para no dejar entradas
    // de undo vacias (p.ej. des-indentar algo que ya no tiene margen),
    // primero miramos si ALGUNA linea del rango va a cambiar realmente.
    //
    // OJO: este criterio de "la linea va a cambiar" debe mantenerse
    // SINCRONIZADO con lo que indentLine() decide internamente (un tab
    // inicial cuenta como nivel, o hasta `kIndentLen` espacios iniciales).
    // No basta con tenerlo solo aqui explicito pero distinto: si indentLine
    // cambia el criterio de desindentado (tabs mixtos, ancho configurable,
    // ...) hay que tocar este predicado en el mismo commit. El loop es el
    // mismo que aplica los cambios abajo, salvo que este es de solo-lectura
    // y corta apenas encuentra una linea que cambie.
    bool willChange = false;
    for (int l = firstLine; l <= lastLine; ++l) {
        const std::string& s = b.document.lineAt(l);
        bool change = indent || (!s.empty() && (s[0] == '\t' || s[0] == ' '));
        if (change) { willChange = true; break; }
    }
    if (!willChange) {
        setActionMessage("Nada que tabular.", MessageKind::Info);
        return;
    }

    HistoryEntry e = b.beginHistoryEntry();
    for (int l = firstLine; l <= lastLine; ++l) {
        std::string before = b.document.lineAt(l);
        int delta = b.document.indentLine(l, indent, kIndentLen);
        if (delta == 0) continue;
        if (delta > 0) {
            e.edits.push_back({EditType::Insert, {l, 0}, {l, delta},
                               std::string(static_cast<size_t>(delta), ' ')});
        } else {
            e.edits.push_back({EditType::Delete, {l, 0}, {l, -delta},
                               before.substr(0, static_cast<size_t>(-delta))});
        }
        // Desplazar el cursor y los extremos de la seleccion sobre ESTA
        // linea por el delta que el cambio movio su comienzo (positivo al
        // indentar, negativo al desindentar). Asi la tabulacion no deja
        // cursor/seleccion apuntando a offsets viejos, sino sobre el mismo
        // texto de siempre (respetando las selecciones parciales).
        auto shift = [delta](int col) {
            // Al desindentar (< 0) nunca se pasa del inicio: col queda 0.
            return delta > 0 ? col + delta : std::max(0, col + delta);
        };
        if (b.cursor.line == l) b.cursor.col = shift(b.cursor.col);
        if (b.selection.has_value()) {
            if (b.selection->anchor.line == l) b.selection->anchor.col = shift(b.selection->anchor.col);
            if (b.selection->position.line == l) b.selection->position.col = shift(b.selection->position.col);
        }
    }
    b.modified = true;
    b.commitHistoryEntry(std::move(e));
    setActionMessage(indent ? "Tabulado." : "Tabulacion quitada.",
                     MessageKind::Success);
}

void Editor::undo() {
    coalescingTyping_ = false;
    Buffer& b = active();

    // Buffer::undo aplica las edits en reversa, restaura el estado "antes"
    // y mueve la entrada al redoStack.
    if (!b.undo()) {
        setActionMessage("Nada que deshacer.", MessageKind::Warning);
        return;
    }

    // La seleccion restaurada vuelve a estar VIGENTE. Importante: el modo
    // Seleccion solo debe activarse si el rango restaurado es realmente NO
    // vacio. Compartimos el criterio con hasSelection() (anchor != position).
    state_ = (b.selection.has_value() && b.selection->anchor != b.selection->position)
           ? State::Seleccion
           : State::Navegacion;
    setActionMessage("Deshecho.", MessageKind::Success);
}

void Editor::redo() {
    coalescingTyping_ = false;
    Buffer& b = active();

    // Buffer::redo reaplica las edits, restaura el estado "despues" y
    // devuelve la entrada al undoStack.
    if (!b.redo()) {
        setActionMessage("Nada que rehacer.", MessageKind::Warning);
        return;
    }

    state_ = (b.selection.has_value() && b.selection->anchor != b.selection->position)
           ? State::Seleccion
           : State::Navegacion;
    setActionMessage("Rehecho.", MessageKind::Success);
}
