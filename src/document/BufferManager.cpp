#include "document/BufferManager.h"
#include "layout/Layout.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>

BufferManager::BufferManager() {
    // Invariante 1 y 2 (v0.6.3): siempre existe al menos un buffer y hay
    // exactamente uno activo. El constructor arranca con un unico buffer
    // sin nombre y lo deja activo. unnamedCounter_ arranca en 1 porque el
    // buffer inicial ya consumio "SinNombre".
    buffers_.emplace_back();
    buffers_.back().id = nextBufferId_++;
    activeBuffer_ = 0;
    mru_.push_back(buffers_.back().id);
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
    assert(idx >= 0 && idx < count());
    return buffers_[static_cast<size_t>(idx)];
}

const Buffer& BufferManager::at(int idx) const {
    assert(idx >= 0 && idx < count());
    return buffers_[static_cast<size_t>(idx)];
}

int BufferManager::activeIndex() const {
    return activeBuffer_;
}

int BufferManager::indexOfId(int id) const {
    for (int i = 0; i < count(); ++i) {
        if (buffers_[static_cast<size_t>(i)].id == id) return i;
    }
    return -1;
}

void BufferManager::touchMru(int id) {
    removeFromMru(id);
    mru_.insert(mru_.begin(), id);
}

void BufferManager::removeFromMru(int id) {
    mru_.erase(std::remove(mru_.begin(), mru_.end(), id), mru_.end());
}

void BufferManager::resetMru(int id) {
    mru_.clear();
    mru_.push_back(id);
}

bool BufferManager::activateId(int id) {
    const int idx = indexOfId(id);
    assert(idx >= 0);
    // Activar el buffer ya activo no reordena nada (contrato de la MRU).
    if (idx != activeBuffer_ || mru_.empty() || mru_.front() != id) {
        activeBuffer_ = idx;
        touchMru(id);
    }
    const Buffer& b = active();
    return b.selection.has_value() && b.selection->anchor != b.selection->position;
}

int BufferManager::push(Buffer&& buffer) {
    buffer.id = nextBufferId_++;
    buffers_.push_back(std::move(buffer));
    const int id = buffers_.back().id;
    activateId(id);
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
        // El mismo buffer reiniciado conserva su identidad: la MRU queda
        // con ese unico ID (sin IDs obsoletos).
        resetMru(b.id);
        return CloseResult::ResetLast;
    }

    // Varios buffers: se elimina el activo de la coleccion y de la MRU.
    // El nuevo activo es el frente restante, es decir el buffer anterior
    // (posicion 1 de la MRU previa). No se abre el selector al cerrar
    // (eso es Ctrl+K t, browse explicito). Invariante 17.
    const int closedId = b.id;
    buffers_.erase(buffers_.begin() + activeBuffer_);
    removeFromMru(closedId);
    assert(!mru_.empty());
    activateId(mru_.front());
    return CloseResult::Removed;
}

bool BufferManager::activateIndex(int idx) {
    assert(idx >= 0 && idx < count());
    return activateId(buffers_[static_cast<size_t>(idx)].id);
}

bool BufferManager::activateMruPosition(int pos) {
    assert(pos >= 0 && pos < static_cast<int>(mru_.size()));
    return activateId(mru_[static_cast<size_t>(pos)]);
}

int BufferManager::mruPositionOf(int id) const {
    for (int i = 0; i < static_cast<int>(mru_.size()); ++i) {
        if (mru_[static_cast<size_t>(i)] == id) return i;
    }
    return -1;
}

int BufferManager::previousMruPosition() const {
    return mru_.size() > 1 ? 1 : 0;
}

void BufferManager::fitViewport(Buffer& b, int rows, int cols) {
    // La unica fuente de la geometria del frame es computeLayout()
    // (ui/Layout.h): aqui se copia la altura del CONTENIDO (la terminal
    // menos el chrome) al viewport del buffer.
    const Layout layout = computeLayout(rows, cols);
    b.viewport.height = layout.content.height;
    b.viewport.width = cols;
}

std::vector<std::string> BufferManager::mruNames() const {
    // Indice id -> posicion fisica construido una sola vez: recorrer la
    // MRU y buscar cada ID con indexOfId() seria O(n^2).
    std::unordered_map<int, int> indexById;
    indexById.reserve(buffers_.size());
    for (int i = 0; i < count(); ++i) {
        indexById.emplace(buffers_[static_cast<size_t>(i)].id, i);
    }

    std::vector<std::string> names;
    names.reserve(mru_.size());
    for (int id : mru_) {
        const auto it = indexById.find(id);
        assert(it != indexById.end());
        const Buffer& b = buffers_[static_cast<size_t>(it->second)];
        names.push_back(b.modified ? b.displayName() + " *"
                                   : b.displayName());
    }
    return names;
}
