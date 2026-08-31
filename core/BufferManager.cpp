#include "core/BufferManager.h"
#include "core/Layout.h"

#include <algorithm>

BufferManager::BufferManager() {
    // Invariante 1 y 2 (v0.6.3): siempre existe al menos un buffer y hay
    // exactamente uno activo. El constructor arranca con un unico buffer
    // sin nombre y lo deja activo. unnamedCounter_ arranca en 1 porque el
    // buffer inicial ya consumio "SinNombre".
    buffers_.emplace_back();
    activeBuffer_ = 0;
}

Buffer& BufferManager::active() {
    return buffers_[static_cast<size_t>(activeBuffer_)];
}

const Buffer& BufferManager::active() const {
    return buffers_[static_cast<size_t>(activeBuffer_)];
}

int BufferManager::count() const {
    return static_cast<int>(buffers_.size());
}

Buffer& BufferManager::at(int idx) {
    return buffers_[static_cast<size_t>(idx)];
}

const Buffer& BufferManager::at(int idx) const {
    return buffers_[static_cast<size_t>(idx)];
}

int BufferManager::activeIndex() const {
    return activeBuffer_;
}

int BufferManager::push(Buffer buffer) {
    buffers_.push_back(std::move(buffer));
    activeBuffer_ = static_cast<int>(buffers_.size()) - 1;
    return activeBuffer_;
}

std::string BufferManager::nextUnnamedName() {
    if (unnamedCounter_ == 0) return "SinNombre";
    return "SinNombre" + std::to_string(unnamedCounter_);
}

int BufferManager::createBuffer(int rows, int cols) {
    Buffer nuevo;
    nuevo.unnamedName = nextUnnamedName();
    unnamedCounter_++;
    // El viewport del buffer nuevo debe tener las dimensiones reales de la
    // terminal (run() solo las fijo al arrancar para los buffers ya
    // existentes). Sin esto, un buffer creado a mitad de sesion con el
    // Viewport por defecto no redibuja toda la pantalla y queda resto
    // del buffer anterior.
    fitViewport(nuevo, rows, cols);
    // El buffer nuevo se convierte inmediatamente en el buffer activo
    // (v0.6.3, invariante 13).
    return push(std::move(nuevo));
}

CloseResult BufferManager::closeActive(int rows, int cols) {
    Buffer& b = active();

    // Invariante 10 (v0.6.3): un buffer modificado no se puede cerrar.
    // Hay que guardar los cambios (Ctrl+K s) o restaurarlo (undo hasta el
    // ultimo estado guardado). No se ofrece confirmacion: se bloquea.
    if (b.modified) {
        return CloseResult::ModifiedBlocked;
    }

    // Invariante 14 (v0.6.3): el ultimo buffer nunca se elimina. En lugar
    // de dejarlo con buffers_ vacio, se convierte en un buffer vacio sin
    // nombre (nuevo nombre generico, documento de una linea vacia,
    // modified = false) y seguimos en el.
    if (buffers_.size() == 1) {
        b.document = Document();
        b.cursor = Cursor();
        fitViewport(b, rows, cols);
        b.filename.clear();
        b.unnamedName = nextUnnamedName();
        unnamedCounter_++;
        b.syncSavedState();
        b.selection.reset();
        b.selectAllActive = false;
        b.selectAllPrevious.reset();
        b.savedIdentity = Buffer::FileIdentity{};
        b.undoStack.clear();
        b.redoStack.clear();
        return CloseResult::ResetLast;
    }

    // Varios buffers: se elimina el activo y el buffer que HEREDA la ranura
    // queda activo: el que ahora ocupa la misma posicion, o el ultimo si se
    // cerro el ultimo (misma ranura, clamp al final). No se abre el selector
    // al cerrar (eso es Ctrl+K t, browse explicito): cerrar es un acto
    // rapido, no un dialogo. Invariante 17.
    buffers_.erase(buffers_.begin() + activeBuffer_);
    activeBuffer_ = std::min(activeBuffer_, static_cast<int>(buffers_.size()) - 1);
    return CloseResult::Removed;
}

bool BufferManager::activate(int idx) {
    activeBuffer_ = idx;
    // Devuelve si el buffer activado tiene seleccion NO vacia, para que el
    // Editor reconcilie el modo global (Seleccion vs Navegacion).
    const Buffer& b = active();
    return b.selection.has_value() && b.selection->anchor != b.selection->position;
}

void BufferManager::fitViewport(Buffer& b, int rows, int cols) {
    // La unica fuente de la geometria del frame es computeLayout()
    // (ui/Layout.h): aqui se copia la altura del CONTENIDO (la terminal
    // menos el chrome) al viewport del buffer.
    const Layout layout = computeLayout(rows, cols);
    b.viewport.height = layout.content.height;
    b.viewport.width = cols;
}

std::vector<std::string> BufferManager::names() const {
    std::vector<std::string> names;
    names.reserve(buffers_.size());
    for (const Buffer& b : buffers_) {
        names.push_back(b.modified ? b.displayName() + " *"
                                   : b.displayName());
    }
    return names;
}
