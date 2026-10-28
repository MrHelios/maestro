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

namespace {

inline bool bracketPairCoversCursor(const BracketPair& p, Position cur) {
    // cubre desde el open inclusive hasta el close inclusive
    return !(cur < p.open) && !(p.close < cur);
}
inline Edit makeIndentEditForLine(int line, int delta, const std::string& before) {
    if (delta > 0) return {EditType::Insert, {line, 0}, {line, delta}, std::string(static_cast<size_t>(delta), ' ')};
    return {EditType::Delete, {line, 0}, {line, -delta}, before.substr(0, static_cast<size_t>(-delta))};
}
}

Editor::Editor() : Editor(makeSystemClipboard(), makeFileWatcher()) {}

Editor::Editor(std::unique_ptr<ScreenRenderer> renderer)
    : Editor(std::move(renderer), makeSystemClipboard(), makeFileWatcher()) {}

Editor::Editor(std::unique_ptr<SystemClipboard> clipboard)
    : Editor(std::move(clipboard), makeFileWatcher()) {}

Editor::Editor(std::unique_ptr<SystemClipboard> clipboard, std::unique_ptr<FileWatcher> watcher)
    : Editor(std::make_unique<NullScreenRenderer>(), std::move(clipboard), std::move(watcher)) {}

Editor::Editor(std::unique_ptr<ScreenRenderer> renderer,
               std::unique_ptr<SystemClipboard> clipboard,
               std::unique_ptr<FileWatcher> watcher)
    : renderer_(std::move(renderer)),
      clipboard_(std::move(clipboard)), watcher_(std::move(watcher)) {
    // Sin fallback silencioso: el único Null intencional lo crea el ctor
    // (clipboard, watcher) con un NullScreenRenderer explícito. Pasar
    // nullptr acá degrada X11/inotify sin aviso, así que es assert.
    assert(renderer_ != nullptr && "Editor: renderer nulo (usar NullScreenRenderer explícito si es intencional)");
    assert(clipboard_ != nullptr && "Editor: clipboard nulo (usar makeNullClipboard() explícito si es intencional)");
    assert(watcher_ != nullptr && "Editor: watcher nulo (usar makeNullFileWatcher() explícito si es intencional)");
    setStatusMessage(kHelpEmpty);
    registerCommands();
    // Garantía del fallback 24x80: el Buffer recién creado trae el default
    // del struct Viewport, que no coincide con el contenido (filas - chrome).
    // Sincronizar acá deja una geometría coherente aunque el dueño nunca
    // llame a resize() (render sin resize previo no se rompe); resize()
    // aplica después el tamaño real y mantiene el invariante.
    for (int i = 0; i < buffers.count(); ++i) syncViewportSize(buffers.at(i));
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

void Editor::setStatusMessage(const std::string& msg, MessageKind kind,
                               std::optional<int> cursor,
                               std::optional<int> boldPrefix) {
    statusMessage_ = Message{msg, kind, std::nullopt, cursor, boldPrefix};
}

void Editor::setActionMessage(const std::string& msg, MessageKind kind) {
    statusMessage_ = Message{msg, kind,
                             std::chrono::steady_clock::now() + kActionMessageTimeout};
}

void Editor::clearExpiredActionMessage(
    std::chrono::steady_clock::time_point now) {
    // Un mensaje de accion expira por si solo pasados los 5 segundos. Los
    // mensajes persistentes NO se tocan (persistent() es true).
    if (statusMessage_.persistent()) return;
    if (statusMessage_.expired(now)) {
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
        HistoryEntry e = b.beginHistoryEntry();
        if (hasSelection()) {
            auto sel = selection();
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
        invalidateWrapIndex();
        resetPreferredVisualColumn();
        updateModified(b);
        clearSelection();
        state_ = State::Navegacion;
        setActionMessage("Pegado.", MessageKind::Success);
        b.commitHistoryEntry(std::move(e));
    });
    commands_.registerCommand("navegacion.palabra.atras", [this] {
        active().cursor.moveToPreviousWord(active().document);
        resetPreferredVisualColumn();
    });
    commands_.registerCommand("navegacion.palabra.adelante", [this] {
        active().cursor.moveToNextWord(active().document);
        resetPreferredVisualColumn();
    });
    commands_.registerCommand("seleccion.total", [this] {
        Buffer& b = active();
        b.selectAllPrevious = b.selection;
        b.selection = selectAllSelection();
        b.selectAllActive = true;
        state_ = State::Seleccion;
        setStatusMessage(kHelpEmpty);
    });
    commands_.registerCommand("navegacion.ir_a_fila", [this] {
        startGoToLine();
    });
    commands_.registerCommand("archivo.renombrar", [this] {
        // Via GUI (sin Prefix) no hay priorState_ guardado: capturarlo
        // como hace buffer.guardar para poder volver al contexto.
        if (state_ != State::Prefix) priorState_ = state_;
        startRename();
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
        if (hadSelection) {
            auto sel = selection();
            auto block = b.document.extractRange(sel->start.line, sel->start.col,
                                                 sel->end.line, sel->end.col);
            if (!clipboard_->copy(blockToString(block))) {
                setActionMessage("Error al copiar al portapapeles.", MessageKind::Error);
                return;
            }
        }
        clearSelection();
        b.selectAllPrevious.reset();
        b.selectAllActive = false;
        state_ = State::Navegacion;
        setActionMessage(hadSelection ? "Copiado." : "Nada seleccionado.");
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
            invalidateWrapIndex();
            resetPreferredVisualColumn();
            updateModified(b);
            b.commitHistoryEntry(std::move(e));
        }
        clearSelection();
        b.selectAllPrevious.reset();
        b.selectAllActive = false;
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

    // Navegacion: '}' indenta la linea actual; '{' desindenta la linea actual.
    // No requieren seleccion; operan sobre la linea donde esta el cursor.
    commands_.registerCommand("navegacion.indentar", [this] {
        indentCurrentLine(true);
    });
    commands_.registerCommand("navegacion.desindentar", [this] {
        indentCurrentLine(false);
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
            // La lista esta en orden MRU: el cursor arranca en el buffer
            // anterior (posicion 1), no en el activo (posicion 0).
            bufferSelectorIndex_ = buffers.previousMruPosition();
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
    commands_.registerCommand("buffer.anterior", [this] {
        switchToPreviousBuffer();
    });
    commands_.registerCommand("navegacion.buscar", [this] {
        startSearch();
    });
    commands_.registerCommand("theme.toggle", [this] {
        toggleTheme();
    });
    commands_.registerCommand("bracket.jump", [this] {
        Buffer& b = active();
        Position cur{b.cursor.line, b.cursor.col};
    
        SyntaxLanguage lang = languageFromFilename(b.filename);
        if (lang == SyntaxLanguage::None) lang = SyntaxLanguage::Cpp;
    
        std::optional<Position> target;
    
        // 1. ¿pair cache válido cubre el cursor?
        if (bracketPair_ && bracketPairCoversCursor(*bracketPair_, cur)) {
            target = (nextBracketJump_ == BracketJumpTarget::Open)
                ? bracketPair_->open
                : bracketPair_->close;
        } else {
            // 2. No hay pair válido (o no cubre el cursor, típico en viewport-only).
            // Calcular el par completo bajo demanda usando búsqueda incremental.
            // Esto es O(distancia) pero solo se paga al comando explícito Ctrl+K m.
            auto src = makeBracketSpanSource(b, lang);
            auto pair = findMatchingBracketFrom(b.document, cur, lang, src);
            
            if (!pair) {
                bracketPair_.reset();
                nextBracketJump_ = BracketJumpTarget::Open;
                setActionMessage("Sin bracket.", MessageKind::Warning);
                state_ = priorState_;
                return;
            }
            
            target = (nextBracketJump_ == BracketJumpTarget::Open)
                ? pair->open
                : pair->close;
        }
    
        b.cursor.line = target->line;
        b.cursor.col = target->col;
        b.cursor.clampToLine(b.document);
        resetPreferredVisualColumn();
    
        centerViewportOnCursor();
        clearSelection();
    
        nextBracketJump_ = (nextBracketJump_ == BracketJumpTarget::Open)
            ? BracketJumpTarget::Close
            : BracketJumpTarget::Open;
    
        bracketJumpPendingPreserve_ = true;
        refreshBracketAfterJump();

        state_ = priorState_;
        setActionMessage("Bracket.", MessageKind::Info);
    });

    // --- Cobertura completa (decisión 3): intenciones sin parámetros ---
    // Cada comando representa la INTENCIÓN de aplicación y es dueño de la
    // semántica completa por modo (regla InputEvent/CommandMap): el handling
    // (handleNavegacion/Interaccion/SeleccionEvent) solo resuelve
    // InputEvent -> nombre, sin envolver la acción. Así el camino TTY y el
    // botón GUI (executeCommand) producen exactamente lo mismo:
    //   Navegacion/Interaccion -> mover pelado;
    //   Seleccion (sin 'a')    -> begin + mover + update;
    //   Seleccion total ('a')  -> salto a extremo (flechas) o no-op;
    //   modales                -> no-op, como el teclado.
    // Eventos con payload (InsertChar.text, Mouse.coords, Resize) NO son
    // comandos (Handler es void()): viajan por handleEvent.
    commands_.registerCommand("cursor.mover.izquierda", [this] {
        Buffer& b = active();
        if (state_ == State::Seleccion && b.selectAllActive) {
            jumpSelectAllEdge(false);  // BOF, como flecha en seleccion total
            return;
        }
        if (state_ == State::Seleccion) {
            beginSelection();
            b.cursor.moveLeft(b.document);
            updateSelectionPosition();
            // Movimiento horizontal: reiniciar columna visual preferida
            resetPreferredVisualColumn();
            return;
        }
        if (state_ == State::Navegacion || state_ == State::Interaccion) {
            b.cursor.moveLeft(b.document);
            resetPreferredVisualColumn();
        }
    });
    commands_.registerCommand("cursor.mover.derecha", [this] {
        Buffer& b = active();
        if (state_ == State::Seleccion && b.selectAllActive) {
            jumpSelectAllEdge(true);  // EOF, como flecha en seleccion total
            return;
        }
        if (state_ == State::Seleccion) {
            beginSelection();
            b.cursor.moveRight(b.document);
            updateSelectionPosition();
            resetPreferredVisualColumn();
            return;
        }
        if (state_ == State::Navegacion || state_ == State::Interaccion) {
            b.cursor.moveRight(b.document);
            resetPreferredVisualColumn();
        }
    });
    commands_.registerCommand("cursor.mover.arriba", [this] {
        Buffer& b = active();
        if (state_ == State::Seleccion && b.selectAllActive) {
            jumpSelectAllEdge(false);
            return;
        }
        if (isWrapEnabledFor(b)) {
            const softwrap::Index& idx = wrapFor(this, b);
            int visRow = idx.visualRowOf({b.cursor.line, b.cursor.col});
            int totalRows = idx.totalRows();
            if (visRow == 0 && totalRows > 0) {
                // Ya en la primera fila visual: no hacer nada
            } else {
                int targetVisRow = std::max(0, visRow - 1);
                if (targetVisRow < totalRows) {
                    // Conservar la preferencia ORIGINAL durante toda la
                    // secuencia vertical: positionAt puede devolver una
                    // columna menor (fila corta) sin destruir la preferencia.
                    int prefCol = preferredVisualCol_.value_or(
                        visualColumnOf(idx, b.document, {b.cursor.line, b.cursor.col}));
                    preferredVisualCol_ = prefCol;
                    Position np = idx.positionAt(b.document, targetVisRow, prefCol);
                    if (state_ == State::Seleccion) {
                        beginSelection();
                        b.cursor.line = np.line;
                        b.cursor.col = np.col;
                        updateSelectionPosition();
                    } else if (state_ == State::Navegacion || state_ == State::Interaccion) {
                        b.cursor.line = np.line;
                        b.cursor.col = np.col;
                    }
                }
            }
        } else {
            if (state_ == State::Seleccion) {
                beginSelection();
                b.cursor.moveUp(b.document);
                updateSelectionPosition();
                return;
            }
            if (state_ == State::Navegacion || state_ == State::Interaccion) {
                b.cursor.moveUp(b.document);
            }
        }
    });
    commands_.registerCommand("cursor.mover.abajo", [this] {
        Buffer& b = active();
        if (state_ == State::Seleccion && b.selectAllActive) {
            jumpSelectAllEdge(true);
            return;
        }
        if (isWrapEnabledFor(b)) {
            const softwrap::Index& idx = wrapFor(this, b);
            int visRow = idx.visualRowOf({b.cursor.line, b.cursor.col});
            int totalRows = idx.totalRows();
            if (visRow >= totalRows - 1 && totalRows > 0) {
                // Ya en la última fila visual: no hacer nada
            } else {
                int targetVisRow = std::min(visRow + 1, totalRows - 1);
                // Conservar la preferencia ORIGINAL durante toda la
                // secuencia vertical (ver movimiento hacia arriba).
                int prefCol = preferredVisualCol_.value_or(
                    visualColumnOf(idx, b.document, {b.cursor.line, b.cursor.col}));
                preferredVisualCol_ = prefCol;
                Position np = idx.positionAt(b.document, targetVisRow, prefCol);
                if (state_ == State::Seleccion) {
                    beginSelection();
                    b.cursor.line = np.line;
                    b.cursor.col = np.col;
                    updateSelectionPosition();
                } else if (state_ == State::Navegacion || state_ == State::Interaccion) {
                    b.cursor.line = np.line;
                    b.cursor.col = np.col;
                }
            }
        } else {
            if (state_ == State::Seleccion) {
                beginSelection();
                b.cursor.moveDown(b.document);
                updateSelectionPosition();
                return;
            }
            if (state_ == State::Navegacion || state_ == State::Interaccion) {
                b.cursor.moveDown(b.document);
            }
        }
    });
    commands_.registerCommand("cursor.mover.inicio", [this] {
        Buffer& b = active();
        if (state_ == State::Seleccion && b.selectAllActive) return;
        if (isWrapEnabledFor(b)) {
            const softwrap::Index& idx = wrapFor(this, b);
            int vr = idx.visualRowOf({b.cursor.line, b.cursor.col});
            if (state_ == State::Seleccion) {
                beginSelection();
                Position np = idx.positionAt(b.document, vr, 0);
                b.cursor.line = np.line;
                b.cursor.col = np.col;
                updateSelectionPosition();
                resetPreferredVisualColumn();
            } else if (state_ == State::Navegacion || state_ == State::Interaccion) {
                Position np = idx.positionAt(b.document, vr, 0);
                b.cursor.line = np.line;
                b.cursor.col = np.col;
                resetPreferredVisualColumn();
            }
        } else {
            if (state_ == State::Seleccion) {
                beginSelection();
                b.cursor.moveHome();
                updateSelectionPosition();
                return;
            }
            if (state_ == State::Navegacion || state_ == State::Interaccion) {
                b.cursor.moveHome();
            }
        }
    });
    commands_.registerCommand("cursor.mover.fin", [this] {
        Buffer& b = active();
        if (state_ == State::Seleccion && b.selectAllActive) return;
        if (isWrapEnabledFor(b)) {
            const softwrap::Index& idx = wrapFor(this, b);
            int vr = idx.visualRowOf({b.cursor.line, b.cursor.col});
            if (state_ == State::Seleccion) {
                beginSelection();
                const softwrap::VisualRow& row = idx.rowAt(vr);
                int width = row.columnEnd - row.columnBegin;
                Position np = idx.positionAt(b.document, vr, width);
                b.cursor.line = np.line;
                b.cursor.col = np.col;
                updateSelectionPosition();
                resetPreferredVisualColumn();
            } else if (state_ == State::Navegacion || state_ == State::Interaccion) {
                const softwrap::VisualRow& row = idx.rowAt(vr);
                int width = row.columnEnd - row.columnBegin;
                Position np = idx.positionAt(b.document, vr, width);
                b.cursor.line = np.line;
                b.cursor.col = np.col;
                resetPreferredVisualColumn();
            }
        } else {
            if (state_ == State::Seleccion) {
                beginSelection();
                b.cursor.moveEnd(b.document);
                updateSelectionPosition();
                return;
            }
            if (state_ == State::Navegacion || state_ == State::Interaccion) {
                b.cursor.moveEnd(b.document);
            }
        }
    });
    commands_.registerCommand("cursor.pagina.arriba", [this] {
        if (state_ == State::Seleccion && active().selectAllActive) return;  // no-op, como el teclado
        if (state_ == State::Seleccion) {
            beginSelection();
            applyPage(-1);
            return;
        }
        if (state_ == State::Navegacion || state_ == State::Interaccion) {
            applyPage(-1);
        }
    });
    commands_.registerCommand("cursor.pagina.abajo", [this] {
        if (state_ == State::Seleccion && active().selectAllActive) return;  // no-op, como el teclado
        if (state_ == State::Seleccion) {
            beginSelection();
            applyPage(+1);
            return;
        }
        if (state_ == State::Navegacion || state_ == State::Interaccion) {
            applyPage(+1);
        }
    });
    commands_.registerCommand("vista.scroll.arriba", [this] {
        if (state_ == State::Seleccion && active().selectAllActive) return;  // no-op, como el teclado
        if (state_ == State::Navegacion || state_ == State::Interaccion ||
            state_ == State::Seleccion) {
            applyScroll(-3);
        }
    });
    commands_.registerCommand("vista.scroll.abajo", [this] {
        if (state_ == State::Seleccion && active().selectAllActive) return;  // no-op, como el teclado
        if (state_ == State::Navegacion || state_ == State::Interaccion ||
            state_ == State::Seleccion) {
            applyScroll(3);
        }
    });
    commands_.registerCommand("edicion.deshacer", [this] {
        undo();
    });
    commands_.registerCommand("edicion.rehacer", [this] {
        redo();
    });
    // Ctrl+K s / botón GUI "Guardar": con nombre guarda directo (y sale
    // del prefijo si venía de él); sin nombre abre el explorador SaveAs
    // (fijando priorState_ si no venía del prefijo, para poder volver).
    commands_.registerCommand("buffer.guardar", [this] {
        if (active().filename.empty()) {
            if (state_ != State::Prefix && state_ != State::SaveAsFileBrowser)
                priorState_ = state_;
            startSaveAs();
            return;
        }
        save();
        if (state_ == State::Prefix) state_ = priorState_;
    });
    // Ctrl+K Ctrl+S / botón GUI "Guardar como": siempre abre el explorador.
    commands_.registerCommand("buffer.guardar.como", [this] {
        if (state_ != State::Prefix && state_ != State::SaveAsFileBrowser)
            priorState_ = state_;
        startSaveAs();
    });
    // Ctrl+K q / botón GUI "Salir": bloquea si hay modificados.
    // Ctrl+K Ctrl+Q / botón GUI "Salir sin guardar": salida inmediata.
    // Ambos delegan en requestQuit (fuente única del chequeo), que además
    // reporta el resultado al loop GUI (ver Editor.h).
    commands_.registerCommand("app.salir", [this] {
        requestQuit(false);
    });
    commands_.registerCommand("app.salir.forzado", [this] {
        requestQuit(true);
    });
}

void Editor::executeCommand(const std::string& name) {
    commands_.execute(name);
}

bool Editor::hasCommand(const std::string& name) const {
    return commands_.has(name);
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

bool Editor::loadIntoActiveBuffer(const std::string& path) {
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
        // Error real (permisos, E/S): no se trata como archivo nuevo y
        // Document::loadFromFile garantiza no tocar el documento en este
        // camino (ver Document.cpp).
        b.filename = oldFilename;
        setActionMessage((result == LoadResult::PermissionDenied)
                                  ? "Sin permisos de lectura: " + path
                                  : "No se pudo leer: " + path,
                         MessageKind::Error);
        return false;
    }
    b.syncSavedState();
    b.savedIdentity = (result == LoadResult::Success) ? captureIdentity(b.filename) : Buffer::FileIdentity{};
    b.cursor.line = 0;
    b.cursor.col = 0;
    invalidateWrapIndex();
    resetPreferredVisualColumn();
    b.selection.reset();
    b.selectAllActive = false;
    b.selectAllPrevious.reset();
    if (oldFilename != b.filename) unwatchFile(oldFilename);
    watchFile(b.filename);
    state_ = State::Navegacion;
    setStatusMessage("");
    if (result == LoadResult::NotFound) {
        setActionMessage("Archivo nuevo: " + path, MessageKind::Success);
    }
    return result == LoadResult::Success;
}

void Editor::resize(const platform::WindowSize& size) {
    if (size.rows <= 0 || size.cols <= 0) return;
    currentRows_ = size.rows;
    currentCols_ = size.cols;
    // Métricas GUI (HiDPI): solo valores conocidos (>=0). Un negativo es
    // "desconocido" igual que 0 (nunca se guarda tal cual: contaminaría el
    // escalado puntos<->drawable que asume dimensiones válidas o 0).
    currentCellW_ = size.cellW < 0 ? 0 : size.cellW;
    currentCellH_ = size.cellH < 0 ? 0 : size.cellH;
    currentPixelW_ = size.pixelW < 0 ? 0 : size.pixelW;
    currentPixelH_ = size.pixelH < 0 ? 0 : size.pixelH;
    for (int i = 0; i < buffers.count(); ++i) {
        syncViewportSize(buffers.at(i));
        buffers.at(i).cursor.clampToLine(buffers.at(i).document);
    }
    // Sin invalidateCache: el diff del backend detecta el cambio de
    // geometría solo y rebuilda; un resize al mismo tamaño conserva el
    // fast path.
}

void Editor::resize(int rows, int cols) {
    resize(platform::WindowSize{rows, cols, 0, 0, 0, 0});
}

void Editor::syncViewportSize(Buffer& b) {
    BufferManager::fitViewport(b, currentRows_, currentCols_);
}

void Editor::createBuffer() {
    invalidateWrapIndex();
    resetPreferredVisualColumn();
    const int rows = currentRows_, cols = currentCols_;
    // El nuevo buffer se vuelve activo y pasa al frente de la MRU
    // automaticamente en BufferManager::createBuffer.
    const int idx = buffers.createBuffer(rows, cols);
    renderer_->invalidateCache();
    setActionMessage("Buffer nuevo: " + buffers.at(idx).unnamedName, MessageKind::Success);
}

void Editor::closeActiveBuffer() {
    invalidateWrapIndex();
    resetPreferredVisualColumn();
    Buffer& cur = active();
    std::string oldPath = cur.filename;
    const int rows = currentRows_, cols = currentCols_;
    auto cr = buffers.closeActive(rows, cols);
    if (cr != CloseResult::ModifiedBlocked) unwatchFile(oldPath);
    switch (cr) {
        case CloseResult::ModifiedBlocked:
            setActionMessage("Buffer modificado: guarda con Ctrl+K s o restaura.", MessageKind::Warning);
            state_ = priorState_;
            break;
        case CloseResult::ResetLast:
            setActionMessage("Buffer reiniciado: " + active().unnamedName);
            state_ = State::Navegacion;
            renderer_->invalidateCache();
            break;
        case CloseResult::Removed: {
            // closeActive() ya dejo activo el frente de la MRU (el buffer
            // anterior). Reconciliamos el modo con su seleccion.
            const bool hasSelection = buffers.activateMruPosition(0);
            state_ = hasSelection ? State::Seleccion : State::Navegacion;
            renderer_->invalidateCache();
            setActionMessage("Buffer cerrado. Activo: " + active().displayName());
            break;
        }
    }
}

void Editor::activateBuffer(int idx) {
    invalidateWrapIndex();
    resetPreferredVisualColumn();
    const bool hasSelection = buffers.activateIndex(idx);
    state_ = hasSelection ? State::Seleccion : State::Navegacion;
    setStatusMessage("");
    renderer_->invalidateCache();
}

void Editor::activateBufferAtMruPosition(int pos) {
    invalidateWrapIndex();
    resetPreferredVisualColumn();
    const bool hasSelection = buffers.activateMruPosition(pos);
    state_ = hasSelection ? State::Seleccion : State::Navegacion;
    setStatusMessage("");
    renderer_->invalidateCache();
}

void Editor::switchToPreviousBuffer() {
    if (buffers.count() <= 1) {
        setActionMessage("No hay buffer anterior.", MessageKind::Warning);
        state_ = priorState_;  // Restaurar estado previo al entrar en Prefix
        return;
    }

    // La MRU ya expresa el toggle: activar la posicion 1 promueve ese
    // buffer al frente y deja el actual como nuevo "anterior".
    activateBufferAtMruPosition(buffers.previousMruPosition());
    setActionMessage("Buffer anterior: " + active().displayName());
}

std::vector<std::string> Editor::bufferNames() const {
    return buffers.mruNames();
}

void Editor::tick(std::chrono::steady_clock::time_point now) {
    clearExpiredActionMessage(now);
    tickMouseAutoscroll(now);
}

void Editor::clearExpiredMessages(
    std::chrono::steady_clock::time_point now) {
    clearExpiredActionMessage(now);
}

bool Editor::hasClipboardPending() const {
    return clipboard_ && clipboard_->hasPending();
}

void Editor::processClipboardEvents() {
    if (clipboard_) clipboard_->processEvents();
}

int Editor::clipboardFd() const {
    return clipboard_ ? clipboard_->fd() : -1;
}

int Editor::watcherFd() const {
    return watcher_ ? watcher_->fd() : -1;
}

void Editor::pollWatcherEvents() {
    if (!watcher_) return;
    watcher_->pollEvents([this](const FileChangeEvent& ev) {
        handleFileChange(ev);
    });
}

void Editor::pollExternalEvents() {
    processClipboardEvents();
    pollWatcherEvents();
}

bool Editor::requestQuit(bool force) {
    if (!force) {
        bool anyModified = false;
        for (int i = 0; i < buffers.count(); ++i) {
            if (buffers.at(i).modified) { anyModified = true; break; }
        }
        if (anyModified) {
            if (state_ == State::Prefix) state_ = priorState_;
            setActionMessage("Hay archivos sin guardar", MessageKind::Warning);
            return false;
        }
    }
    running_ = false;
    return true;
}

Editor::CursorViewSnapshot Editor::cursorViewSnapshot() const {
    const Buffer& b = active();
    return {b.id, b.cursor.line, b.cursor.col, b.viewport.top, state_};
}

bool Editor::consumeSuspendRequest() {
    if (!suspendRequested_) return false;
    suspendRequested_ = false;
    return true;
}

static int msUntil(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point target) {
    const auto remaining = target - now;
    const long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining)
            .count();
    return ms <= 0 ? 0 : static_cast<int>(std::min<long>(ms, INT_MAX));
}

int Editor::nextTimeoutMs(
    std::chrono::steady_clock::time_point now) const {
    int waitMs = -1;
    if (hasClipboardPending()) waitMs = 20;
    if (statusMessage_.expiry) {
        int msgMs = msUntil(now, *statusMessage_.expiry);
        if (waitMs < 0) waitMs = msgMs;
        else waitMs = std::min(waitMs, msgMs);
    }
    if (mouseAutoscrollActive()) {
        const auto target =
            mouseAutoscrollLastStep_ + kMouseAutoscrollInterval;
        int tickMs = msUntil(now, target);
        if (waitMs < 0) waitMs = tickMs;
        else waitMs = std::min(waitMs, tickMs);
    }
    return waitMs;
}

void Editor::invalidateScreen() {
    renderer_->invalidateCache();
}

void Editor::renderFrame() {
    Buffer& b = active();
    // Transición modal: invalida el diff al entrar Y al salir (una sola vez
    // por transición, no por frame). Adentro del modal el cache ya está
    // invalidado y ningún camino lo revalida; al salir, el primer frame del
    // editor es rebuild total.
    const bool isModal =
        (state_ == State::BufferSelector || state_ == State::FileBrowser || state_ == State::SaveAsFileBrowser);
    if (isModal != wasModal_) {
        renderer_->invalidateCache();
        wasModal_ = isModal;
    }
    if (state_ == State::BufferSelector) {
        // Pantalla del selector: se dibuja la lista de buffers con la
        // barra MULTIBUFFER al final, manteniendo el aspecto del editor.
        renderer_->renderBufferList(bufferNames(), bufferSelectorIndex_,
                                  b.viewport.width, b.viewport.height, sink());
    } else if (state_ == State::FileBrowser) {
        fileBrowser.clampScroll(b.viewport.height);
        // DEUDA (Alcance 1): el vector adaptado se reconstruye por frame
        // (O(n) copias + 1 alloc). Aceptable fuera del camino critico y
        // consistente con bufferNames(); a futuro cachearlo en
        // FileBrowser::reload() o pasar una vista no propietaria al Renderer.
        std::vector<FileListItem> items;
        items.reserve(fileBrowser.entries_.size());
        for (const FileBrowserEntry& e : fileBrowser.entries_) {
            items.push_back(FileListItem{e.name, e.isDirectory});
        }
        renderer_->renderFileList(items,
                                fileBrowser.index_, fileBrowser.scroll_,
                                fileBrowser.path_, toMessageBar(statusMessage_),
                                b.viewport.width, b.viewport.height, sink());
    } else if (state_ == State::SaveAsFileBrowser) {
        fileBrowser.clampScroll(b.viewport.height);
        std::vector<FileListItem> items;
        items.reserve(fileBrowser.entries_.size());
        for (const FileBrowserEntry& e : fileBrowser.entries_) {
            items.push_back(FileListItem{e.name, e.isDirectory});
        }
        // El input vive en el MessageBar (debajo del StatusBar), como
        // el resto de los prompts modales ("Find: ..."): si hay un mensaje
        // activo se muestra el; si no, la linea de input con el nombre.
        // El cursor va al final del nombre, antes del sufijo.
        Message shown = statusMessage_;
        if (shown.text.empty()) {
            shown = Message{std::string(kHelpNombrar) + saveAsFileName_ +
                                kHelpSaveAsSuffix,
                            MessageKind::Prompt, std::nullopt,
                            promptCursor(kHelpNombrar, saveAsFileName_),
                            promptBoldPrefix(kHelpNombrar)};
        }
        renderer_->renderSaveAsFileList(items,
                                        fileBrowser.index_, fileBrowser.scroll_,
                                        fileBrowser.path_, toMessageBar(shown),
                                        b.viewport.width, b.viewport.height, sink());
    } else {
        // Sincroniza lenguaje del cache con el buffer activo antes de bracket/render
        {
            // Idioma real del archivo (None => sin highlight): este caché es
            // del renderer. Los brackets usan bracketCache_ propio (siempre
            // Cpp); forzar Cpp acá reintroduciría el thrash de idioma con
            // Renderer::updateSyntaxLanguage (invalidateAll por frame).
            SyntaxLanguage lang = languageFromFilename(b.filename);

            if (b.syntaxCache.language() != lang) b.syntaxCache.setLanguage(lang);

            renderer_->setExternalSyntaxCache(&b.syntaxCache);
        }

        bool wrap = isWrapEnabledFor(b);
        if (wrap) {
            // En wrap: forzar left=0 y clamp top a filas visuales
            b.viewport.left = 0;
            const softwrap::Index& idx = wrapFor(this, b);
            int totalVisualRows = idx.totalRows();
            int contentH = b.viewport.height;
            if (contentH > 0) {
                int maxTop = std::max(0, totalVisualRows - contentH);
                if (b.viewport.top > maxTop) b.viewport.top = maxTop;
            }
        }

        // IMPORTANTE: scroll antes que bracket highlight.
        // Si no, tras un page/jump el cursor puede quedar fuera del rango de scan.
        // Scroll manual (scrollbar): si el viewport se desplazó manualmente y el
        // cursor NO ha cambiado de posición, NO seguir al cursor.
        bool preserveManualViewport = false;
        if (b.manualScroll) {
            const bool cursorUnchanged =
                b.cursor.line == b.manualScrollCursor.line &&
                b.cursor.col  == b.manualScrollCursor.col;

            if (cursorUnchanged) {
                preserveManualViewport = true;
            } else {
                // El cursor se movió: volver a seguirlo en este mismo render.
                b.manualScroll = false;
            }
        }

        if (!preserveManualViewport) {
            // Aplicar la lógica existente de suppressScrollToCursor_
            // y, cuando corresponda, desplazar hacia el cursor.
            if (suppressScrollToCursor_) {
                suppressScrollToCursor_ = false;
            } else {
                if (wrap) {
                    const softwrap::Index& idx = wrapFor(this, b);
                    int vr = idx.visualRowOf({b.cursor.line, b.cursor.col});
                    // scrollToCursor visual
                    int contentH = b.viewport.height;
                    if (contentH > 0) {
                        if (vr < b.viewport.top) {
                            b.viewport.top = vr;
                        } else if (vr >= b.viewport.top + contentH) {
                            b.viewport.top = vr - contentH + 1;
                        }
                        int maxTop = std::max(0, idx.totalRows() - contentH);
                        if (b.viewport.top > maxTop) b.viewport.top = maxTop;
                    }
                } else {
                    b.viewport.scrollToCursor(
                        b.cursor,
                        b.document,
                        textWidthFor(b.viewport, b.document.lineCount())
                    );
                }
            }
        }

        // Bracket highlight: viewport-acotado.
        if (bracketJumpPendingPreserve_) {
            refreshBracketAfterJump();
            bracketJumpPendingPreserve_ = false;
        } else {
            updateBracketHighlight();
        }

        std::optional<BracketPair> toRender =
            (state_ == State::Seleccion) ? std::nullopt : bracketPair_;

        if (wrap) {
            const softwrap::Index& idx = wrapFor(this, b);
            renderer_->setWrapIndex(&idx);
        } else {
            renderer_->setWrapIndex(nullptr);
        }

        renderer_->renderScreenDiff(
            b.document,
            b.cursor,
            b.viewport,
            b.filename,
            b.modified,
            makeChromeRequest(statusMessage_, state_),
            sink(),
            b.selection,
            searchHighlight_,
            toRender
        );

        renderer_->setExternalSyntaxCache(nullptr);
    }
}

std::optional<Position> Editor::resolveMouseDragPosition(CellPos cell) {
    Buffer& b = active();
    const Layout layout =
        computeLayout(b.viewport.height + kChromeRows, b.viewport.width);
    const int h = layout.content.height;
    const int totalW = layout.content.width;
    if (h <= 0 || totalW <= 0) return std::nullopt;

    const int relRow = cell.row - layout.content.row;
    const int relCol = cell.col - layout.content.col;
    const int gutterW = gutterWidth(b.document.lineCount(), totalW);

    bool wrap = isWrapEnabledFor(b);
    const softwrap::Index* idx = nullptr;
    if (wrap) {
        idx = &wrapFor(this, b);
    }
    int totalVisualRows = wrap ? idx->totalRows() : b.document.lineCount();
    int textW = wrap ? 0 : totalW - gutterW;  // wrap no usa scroll horizontal
    int maxTop = std::max(0, totalVisualRows - h);

    // Intencion de autoscroll temporal (solo vertical, solo gesto de mouse:
    // unico llamador con gesto armado, mas el tick que re-ejecuta fuera).
    // Semantica ASIMETRICA, y hacia arriba deliberadamente distinta de
    // "estrictamente fuera":
    // - relRow == 0 ES contenido (primera fila visible), no fuera. Pero el
    //   fuera real hacia arriba (relRow < 0) no existe: la terminal reporta
    //   filas 0-based y el contenido arranca en la fila 0, asi que jamas
    //   llega relRow < 0. DECISION: tratar la primera fila como intencion
    //   de scroll-up, como sustituto del fuera inalcanzable. Consecuencia
    //   conocida y aceptada: un drag que termina justo en la primera fila
    //   scrollea (y arma el tick); con este reporte no hay forma de
    //   distinguir "seleccionar la primera fila" de "pedir scroll arriba".
    // - Hacia abajo el fuera real SI llega (chrome inferior: relRow >= h), asi
    //   que no hay sustituto: la ultima fila (h-1) es seleccion exacta,
    //   no intencion de scroll.
    // El resto del interior apaga de inmediato. Al armar (None -> intencion)
    // se fija lastStep = ahora: el primer paso lo da el tick tras el
    // intervalo, sin salto inmediato.
    {
        MouseAutoscrollDirection want = MouseAutoscrollDirection::None;
        if (relRow <= 0)
            want = MouseAutoscrollDirection::Up;
        else if (relRow >= h)
            want = MouseAutoscrollDirection::Down;
        if (want != MouseAutoscrollDirection::None) {
            if (mouseAutoscrollDirection_ == MouseAutoscrollDirection::None)
                mouseAutoscrollLastStep_ = std::chrono::steady_clock::now();
            mouseAutoscrollCell_ = cell;
        }
        mouseAutoscrollDirection_ = want;
    }

    // Fila `~` dentro del content (mas alla del EOF pero sin salir del
    // viewport): sin scroll, se extiende hasta EOF.
    const bool insideRows = relRow >= 0 && relRow < h;
    if (insideRows) {
        int visualRow = b.viewport.top + relRow;
        if (visualRow >= totalVisualRows) {
            // Más allá del EOF: el extremo de selección debe ser el EOF
            // lógico (después del último carácter), no la última celda que
            // devolvería positionAt() para un cursor/End.
            if (wrap) {
                return dragEofPosition(b.document);
            } else {
                return dragEdgePosition(relRow, relCol, b.viewport, b.document, h, totalW);
            }
        }
    }

    // Fuera del contenido en vertical y ya en el limite: scroll imposible,
    // pero la intencion de seleccionar hacia el borde sigue siendo valida.
    // Se devuelve la posicion clampada al borde en lugar de caer en screenToCursor()
    // (nullopt). Solo activo durante seleccion por mouse: esta funcion solo
    // se llama desde handleMouseDrag con gesto armado (press previo valido).
    if (relRow < 0 && b.viewport.top <= 0) {
        if (wrap && idx) {
            return idx->positionAt(b.document, 0, std::max(0, relCol - gutterW));
        }
        return dragEdgePosition(relRow, relCol, b.viewport, b.document, h, totalW);
    }
    if (relRow >= h && b.viewport.top >= maxTop) {
        if (wrap && idx) {
            const int lastRow = totalVisualRows - 1;
            const int visualCol = std::max(0, relCol - gutterW);
            const softwrap::VisualRow& row = idx->rowAt(lastRow);
            const int rowWidth = row.columnEnd - row.columnBegin;
            if (visualCol >= rowWidth) return dragEofPosition(b.document);
            return idx->positionAt(b.document, lastRow, visualCol);
        }
        return dragEdgePosition(relRow, relCol, b.viewport, b.document, h, totalW);
    }

    // Autoscroll por bordes (1 linea / 1 celda por evento). Asimetrico en
    // vertical como el armado de arriba: la primera fila (relRow == 0, que
    // ES contenido) scrollea por la misma DECISION sustituta —el fuera
    // hacia arriba no es reportable—; la ultima fila es exclusiva (el
    // fuera real es el chrome inferior, relRow >= h). En wrap no hay
    // scroll horizontal.
    bool scrolled = false;
    if (relRow <= 0 && b.viewport.top > 0) {
        b.viewport.top--;
        scrolled = true;
    } else if (relRow >= h && b.viewport.top < maxTop) {
        b.viewport.top++;
        scrolled = true;
    }
    if (!wrap) {
        if (relCol <= gutterW && b.viewport.left > 0) {
            b.viewport.left--;
            if (b.viewport.left < 0) b.viewport.left = 0;
            scrolled = true;
        } else if (relCol >= totalW - 1 && textW > 0) {
            // Guard contra deriva infinita en lineas cortas: solo scroll a la
            // derecha si la linea borde tiene contenido oculto mas alla del
            // viewport (ancho visual > left + textW).
            int probeLine = b.viewport.top +
                            std::min(std::max(relRow, 0), h - 1);
            if (probeLine < 0) probeLine = 0;
            if (probeLine >= b.document.lineCount()) probeLine = b.document.lineCount() - 1;
            const std::string& ls = b.document.lineAt(probeLine);
            const int visWidth = utf8::columnOf(ls, static_cast<int>(ls.size()));
            if (visWidth > b.viewport.left + textW) {
                b.viewport.left++;
                scrolled = true;
            }
        }
    }

    if (!scrolled) {
        // Sin scroll: mapeo normal (puede ser nullopt fuera del area de
        // contenido, en el chrome inferior, que
        // aqui ya se trato como borde inferior si habia scroll posible;
        // si no habia scroll posible se ignora el evento).
        if (wrap && idx) {
            return screenToCursorWrap(*idx, cell, layout, b.viewport, b.document, gutterW);
        }
        return screenToCursor(cell, layout, b.viewport, b.document);
    }

    // Tras scrollear, la posicion es el borde visible (mismo helper que
    // el camino tilde: una sola copia de la logica de columnas).
    if (wrap && idx) {
        int visualRow = std::clamp(b.viewport.top + relRow, 0, totalVisualRows - 1);
        int visualCol = std::max(0, relCol - gutterW);
        return idx->positionAt(b.document, visualRow, visualCol);
    }
    return dragEdgePosition(relRow, relCol, b.viewport, b.document, h,
                            totalW);
}

void Editor::applyMouseDragPosition(const Position& pos) {
    Buffer& b = active();
    if (!mouseDragStarted_) {
        // Primer drag efectivo: entra a Seleccion o reinicia el rango.
        if (pressState_ != State::Seleccion) {
            state_ = State::Seleccion;
            b.selectAllActive = false;
        } else {
            b.selection.reset();
            b.selectAllActive = false;
        }
        b.selection = Selection{dragAnchor_.value(), pos};
        b.cursor.line = pos.line;
        b.cursor.col = pos.col;
        b.cursor.clampToLine(b.document);
        resetPreferredVisualColumn();
        // La posicion ya viene normalizada por byteForColumn+alignStart;
        // se re-sincroniza por seguridad tras el clamp.
        b.selection->position = {b.cursor.line, b.cursor.col};
        mouseDragStarted_ = true;
        return;
    }
    b.cursor.line = pos.line;
    b.cursor.col = pos.col;
    b.cursor.clampToLine(b.document);
    resetPreferredVisualColumn();
    if (b.selection.has_value()) {
        b.selection->position = {b.cursor.line, b.cursor.col};
    } else {
        b.selection = Selection{dragAnchor_.value(),
                                {b.cursor.line, b.cursor.col}};
    }
}

bool Editor::mouseAutoscrollActive() const {
    if (mouseAutoscrollDirection_ == MouseAutoscrollDirection::None)
        return false;
    if (!mouseGestureActive_ || !mouseDragStarted_) return false;
    if (state_ != State::Navegacion && state_ != State::Interaccion &&
        state_ != State::Seleccion)
        return false;
    if (!active().selection.has_value()) return false;
    return true;
}

bool Editor::tickMouseAutoscroll(
    std::chrono::steady_clock::time_point now) {
    if (!mouseAutoscrollActive()) return false;
    if (!mouseButtonPressedQuery_()) {
        // Suelta fuera de la ventana: el release nunca llega como evento y
        // solo el oraculo fisico lo detecta. Misma salida que un release
        // entregado (gesto cerrado, sin paso).
        mouseGestureActive_ = false;
        mouseDragStarted_ = false;
        mouseAutoscrollDirection_ = MouseAutoscrollDirection::None;
        return false;
    }
    if (now - mouseAutoscrollLastStep_ < kMouseAutoscrollInterval)
        return false;
    // Un paso: re-ejecuta el ultimo drag fuera (resolve scrollea como
    // maximo 1 linea, o sostiene el borde si ya esta en el limite). Sin
    // deuda acumulada: lastStep = ahora, no += intervalo.
    auto pos = resolveMouseDragPosition(mouseAutoscrollCell_);
    mouseAutoscrollLastStep_ = now;
    if (!pos.has_value()) return false;
    applyMouseDragPosition(*pos);
    return true;
}

std::optional<Selection> Editor::selectAllSelection() const {
    const Buffer& b = active();
    int last = b.document.lineCount() - 1;
    Position end{last, b.document.lineLength(last)};
    return Selection{{0, 0}, end};
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
    if (isWrapEnabledFor(b)) {
        const softwrap::Index& idx = wrapFor(this, b);
        int totalRows = idx.totalRows();
        int h = b.viewport.height;
        if (h <= 0) return;
        int prefVisualCol = preferredVisualCol_.value_or(
            visualColumnOf(idx, b.document, {b.cursor.line, b.cursor.col}));
        // Página es navegación vertical: conserva la preferencia entre pasos.
        preferredVisualCol_ = prefVisualCol;

        Position target;
        if (h >= totalRows) {
            b.viewport.top = 0;
            int targetRow = (dir < 0) ? 0 : totalRows - 1;
            target = idx.positionAt(b.document, targetRow, prefVisualCol);
        } else {
            int vr = idx.visualRowOf({b.cursor.line, b.cursor.col});
            int rel = vr - b.viewport.top;
            int maxTop = totalRows - h;
            if (dir < 0) {
                bool alreadyAtTop = (b.viewport.top == 0);
                b.viewport.top = std::max(0, b.viewport.top - h);
                target = idx.positionAt(b.document,
                    alreadyAtTop ? 0 : b.viewport.top + rel, prefVisualCol);
            } else {
                bool alreadyAtBottom = (b.viewport.top == maxTop);
                b.viewport.top = std::min(maxTop, b.viewport.top + h);
                target = idx.positionAt(b.document,
                    alreadyAtBottom ? totalRows - 1 : b.viewport.top + rel,
                    prefVisualCol);
            }
        }
        b.cursor.line = target.line;
        b.cursor.col = target.col;
        if (state_ == State::Seleccion) updateSelectionPosition();
        return;
    }
    const int count = b.document.lineCount(); // >= 1: siempre hay una linea
    const int h = b.viewport.height;

    // Caso 1: Archivo pequeno (cabe entero en una pagina). RePag -> inicio,
    // AvPag -> final, sin nunca quedar fuera del documento.
    if (h >= count) {
        b.viewport.top = 0;
        b.cursor.line = (dir < 0) ? 0 : count - 1;
        b.cursor.clampToLine(b.document);
        if (state_ == State::Seleccion) updateSelectionPosition();
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
    if (state_ == State::Seleccion) updateSelectionPosition();
}

void Editor::applyScroll(int delta) {
    Buffer& b = active();
    if (isWrapEnabledFor(b)) {
        const softwrap::Index& idx = wrapFor(this, b);
        int totalRows = idx.totalRows();
        int h = b.viewport.height;
        if (h <= 0) return;
        int maxTop = std::max(0, totalRows - h);
        int oldTop = b.viewport.top;
        b.viewport.top = std::min(std::max(b.viewport.top + delta, 0), maxTop);
        if (b.viewport.top != oldTop) suppressScrollToCursor_ = true;
        return;
    }
    const int count = b.document.lineCount();
    const int h = b.viewport.height;
    if (h <= 0) return;
    if (h >= count) {
        b.viewport.top = 0;
        return;
    }
    const int maxTop = count - h;
    const int oldTop = b.viewport.top;
    b.viewport.top = std::min(std::max(b.viewport.top + delta, 0), maxTop);
    if (b.viewport.top != oldTop) suppressScrollToCursor_ = true;
}

void Editor::jumpSelectAllEdge(bool toEnd) {
    Buffer& b = active();
    if (toEnd) {
        int last = b.document.lineCount() - 1;
        b.cursor.line = last;
        b.cursor.col = b.document.lineLength(last);
    } else {
        b.cursor.line = 0;
        b.cursor.col = 0;
    }
    b.selection = Selection{{b.cursor.line, b.cursor.col},
                            {b.cursor.line, b.cursor.col}};
    resetPreferredVisualColumn();
    b.selectAllActive = false;
    b.selectAllPrevious.reset();
    setStatusMessage("SELECCION");
}

void Editor::clearSelection() {
    Buffer& b = active();
    b.selection.reset();
}

void Editor::save() {
    Buffer& b = active();
    // v0.9: save() solo se invoca para buffers CON nombre (handlePrefixKey
    // desvia los sin nombre al explorador SaveAs). El guard sigue aqui como
    // invariante defensivo: un buffer sin nombre no tiene a donde guardar.
    if (b.filename.empty()) {
        setActionMessage("Archivo sin nombre: usa Ctrl+K Ctrl+S para elegir ruta.", MessageKind::Warning);
        return;
    }
    if (b.document.saveToFile(b.filename)) {
        b.syncSavedState();
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
    resetPreferredVisualColumn();
    updateModified(b);
    for (int l = sel->start.line; l <= sel->end.line; ++l) {
        markWrapLineDirty(this, l);
    }
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

    bool willChange = false;
    for (int l = firstLine; l <= lastLine; ++l) {
        if (b.document.previewIndentDelta(l, indent, kIndentLen) != 0) { willChange = true; break; }
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
        e.edits.push_back(makeIndentEditForLine(l, delta, before));
        if (b.cursor.line == l) b.cursor.col = shiftColumn(b.cursor.col, delta);
        if (b.selection.has_value()) {
            if (b.selection->anchor.line == l) b.selection->anchor.col = shiftColumn(b.selection->anchor.col, delta);
            if (b.selection->position.line == l) b.selection->position.col = shiftColumn(b.selection->position.col, delta);
        }
    }
    invalidateWrapIndex();
    resetPreferredVisualColumn();
    updateModified(b);
    b.commitHistoryEntry(std::move(e));
    setActionMessage(indent ? "Tabulado." : "Tabulacion quitada.",
                     MessageKind::Success);
}

void Editor::indentCurrentLine(bool indent) {
    Buffer& b = active();
    int line = b.cursor.line;
    if (b.document.previewIndentDelta(line, indent, kIndentLen) == 0) {
        setActionMessage("Nada que tabular.", MessageKind::Info);
        return;
    }

    std::string before = b.document.lineAt(line);
    int delta = b.document.indentLine(line, indent, kIndentLen);
    if (delta == 0) {
        setActionMessage("Nada que tabular.", MessageKind::Info);
        return;
    }

    HistoryEntry e = b.beginHistoryEntry();
    e.edits.push_back(makeIndentEditForLine(line, delta, before));
    b.cursor.col = shiftColumn(b.cursor.col, delta);

    invalidateWrapIndex();
    resetPreferredVisualColumn();
    updateModified(b);
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
    invalidateWrapIndex();
    resetPreferredVisualColumn();
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

    invalidateWrapIndex();
    resetPreferredVisualColumn();
    state_ = (b.selection.has_value() && b.selection->anchor != b.selection->position)
            ? State::Seleccion
            : State::Navegacion;
    setActionMessage("Rehecho.", MessageKind::Success);
}

void Editor::toggleTheme() {
    renderer_->toggleTheme();
    state_ = priorState_;
    setActionMessage(renderer_->isDarkTheme() ? "Tema oscuro" : "Tema claro", MessageKind::Info);
}

void Editor::startGoToLine() {
    goToLineQuery_.clear();
    state_ = State::IrAFila;
    setStatusMessage(std::string(kHelpIrAFila), MessageKind::Prompt,
                     promptCursor(kHelpIrAFila, goToLineQuery_),
                     promptBoldPrefix(kHelpIrAFila));
}

BracketSpanSource Editor::makeBracketSpanSource(Buffer& buf, SyntaxLanguage lang) {
    // El path de brackets usa caché propio (siempre Cpp, incluso si el archivo
    // es None): comparte documento pero no idioma con Buffer::syntaxCache.
    // syncDocument() invalida solo ante cambio de documento/buffer.
    auto& cache = bracketCache_;
    if (cache.language() != lang) cache.setLanguage(lang);
    const Document* doc = &buf.document;
    BracketSpanSource src;
    src.ensure = [&cache, doc](int line) {
        if (line >= 0) cache.ensureValid(*doc, line + 1);
    };
    src.spans = [&cache](int line) -> const std::vector<SyntaxSpan>& {
        return cache.spansFor(line);
    };
    return src;
}

// Fuente viewport-only sobre SyntaxCache: reutiliza los spans con estado
// sintáctico correcto (before_[line]) en vez de resaltar cada línea desde
// SyntaxState{}. El acotado al viewport lo impone findMatchingBracketBounded
// vía firstLine/lastLineExclusive, no la fuente.
BracketSpanSource Editor::makeViewportSpanSource(Buffer& buf, SyntaxLanguage lang, int firstLine, int lastLine) {
    (void)firstLine;
    (void)lastLine;
    return makeBracketSpanSource(buf, lang);
}

void Editor::updateBracketHighlight() {
    Buffer& b = active();
    Position cur{b.cursor.line, b.cursor.col};

    bool wrap = isWrapEnabledFor(b);
    int top, bottom;
    if (wrap) {
        const softwrap::Index& idx = wrapFor(this, b);
        int vtop = b.viewport.top;
        int height = b.viewport.height;
        int vbottom = std::min(vtop + height, idx.totalRows());
        // Convertir filas visuales a rango de líneas lógicas
        int firstLine = idx.rowAt(vtop).docLine;
        int lastLine = idx.rowAt(std::max(0, vbottom - 1)).docLine;
        top = std::max(0, firstLine);
        bottom = std::min(b.document.lineCount(), lastLine + 1);
        // Para cache, usar viewport top visual
        if (cur == lastBracketCursor_ &&
            b.document.version() == lastBracketVersion_ &&
            b.document.instanceId() == lastBracketInstanceId_ &&
            b.viewport.top == lastBracketViewportTop_ &&
            vbottom == lastBracketViewportBottom_) {
            return;
        }
    } else {
        top = std::max(0, b.viewport.top);
        int height = std::max(0, b.viewport.height);
        bottom = std::min(b.document.lineCount(), top + height);

        if (cur == lastBracketCursor_ &&
            b.document.version() == lastBracketVersion_ &&
            b.document.instanceId() == lastBracketInstanceId_ &&
            top == lastBracketViewportTop_ &&
            bottom == lastBracketViewportBottom_) {
            return;
        }
    }

    SyntaxLanguage lang = languageFromFilename(b.filename);
    if (lang == SyntaxLanguage::None) lang = SyntaxLanguage::Cpp;

    // firstLine/lastLine salen directamente del rango visible
    auto source = makeViewportSpanSource(b, lang, top, bottom);
    auto p = findMatchingBracketBounded(b.document, cur, lang, source, top, bottom);

    if (!p) {
        bracketPair_.reset();
        nextBracketJump_ = BracketJumpTarget::Open;
    } else {
        bool samePair = bracketPair_ && *bracketPair_ == *p;
        bool cursorMoved = (lastBracketCursor_.line != cur.line ||
                            lastBracketCursor_.col != cur.col);
        if (!bracketPair_ || !samePair) {
            bracketPair_ = p;
            nextBracketJump_ = BracketJumpTarget::Open;
        } else if (cursorMoved) {
            nextBracketJump_ = BracketJumpTarget::Open;
            bracketPair_ = p;
        } else {
            bracketPair_ = p;
        }
    }

    lastBracketCursor_ = cur;
    lastBracketVersion_ = b.document.version();
    lastBracketInstanceId_ = b.document.instanceId();
    if (wrap) {
        const softwrap::Index& idx = wrapFor(this, b);
        lastBracketViewportTop_ = b.viewport.top;
        lastBracketViewportBottom_ = std::min(b.viewport.top + b.viewport.height, idx.totalRows());
    } else {
        lastBracketViewportTop_ = top;
        lastBracketViewportBottom_ = bottom;
    }
}

void Editor::refreshBracketAfterJump() {
    Buffer& b = active();
    Position cur{b.cursor.line, b.cursor.col};

    bool wrap = isWrapEnabledFor(b);
    int top, bottom;
    if (wrap) {
        const softwrap::Index& idx = wrapFor(this, b);
        int vtop = b.viewport.top;
        int height = b.viewport.height;
        int vbottom = std::min(vtop + height, idx.totalRows());
        int firstLine = idx.rowAt(vtop).docLine;
        int lastLine = idx.rowAt(std::max(0, vbottom - 1)).docLine;
        top = std::max(0, firstLine);
        bottom = std::min(b.document.lineCount(), lastLine + 1);

        // Fast path O(1): incluye top y bottom para detectar resizes
        if (cur == lastBracketCursor_ &&
            b.document.version() == lastBracketVersion_ &&
            b.document.instanceId() == lastBracketInstanceId_ &&
            b.viewport.top == lastBracketViewportTop_ &&
            vbottom == lastBracketViewportBottom_) {
            return;
        }
    } else {
        top = std::max(0, b.viewport.top);
        int height = std::max(0, b.viewport.height);
        bottom = std::min(b.document.lineCount(), top + height);

        // Fast path O(1): incluye top y bottom para detectar resizes
        if (cur == lastBracketCursor_ &&
            b.document.version() == lastBracketVersion_ &&
            b.document.instanceId() == lastBracketInstanceId_ &&
            top == lastBracketViewportTop_ &&
            bottom == lastBracketViewportBottom_) {
            return;
        }
    }

    SyntaxLanguage lang = languageFromFilename(b.filename);
    if (lang == SyntaxLanguage::None) lang = SyntaxLanguage::Cpp;

    // Fuente viewport-only sobre SyntaxCache (estado entre líneas correcto).
    auto src = makeViewportSpanSource(b, lang, top, bottom);
    auto p = findMatchingBracketBounded(b.document, cur, lang, src, top, bottom);

    if (!p) {
        bracketPair_.reset();
        // nextBracketJump_ se conserva después de jump
    } else {
        bracketPair_ = p;
    }

    lastBracketCursor_ = cur;
    lastBracketVersion_ = b.document.version();
    lastBracketInstanceId_ = b.document.instanceId();
    if (wrap) {
        const softwrap::Index& idx = wrapFor(this, b);
        lastBracketViewportTop_ = b.viewport.top;
        lastBracketViewportBottom_ = std::min(b.viewport.top + b.viewport.height, idx.totalRows());
    } else {
        lastBracketViewportTop_ = top;
        lastBracketViewportBottom_ = bottom;
    }
}

Editor::ScrollInfo Editor::scrollInfo() {
    Buffer& b = active();

    int total = b.document.lineCount();

    if (isWrapEnabledFor(b)) {
        const softwrap::Index& idx = wrapFor(this, b);
        total = idx.totalRows();
    }

    return ScrollInfo{
        b.viewport.top,
        b.viewport.height,
        total
    };
}

void Editor::setViewportTop(int top) {
    Buffer& b = active();
    const ScrollInfo info = scrollInfo();

    // Scroll manual: el viewport se desacopla del cursor hasta que este se mueva.
    b.manualScroll = true;
    b.manualScrollCursor = {b.cursor.line, b.cursor.col};

    if (info.visible <= 0) {
        b.viewport.top = 0;
        return;
    }

    const int maxTop = std::max(0, info.total - info.visible);
    b.viewport.top = std::clamp(top, 0, maxTop);

    // NOTA: no se mueve el cursor. El scrollbar solo cambia el viewport.
    // manualScroll evita que scrollToCursor devuelva el viewport
    // a la posición del cursor en renders posteriores.
    if (state_ == State::Seleccion) updateSelectionPosition();
}


