#pragma once

// ---------------------------------------------------------------------------
// SmallVec<T, N>: vector con capacidad INLINE para N elementos.
//
// Existe por UN motivo: HistoryEntry guarda sus edits, y el caso dominante
// es UNA sola edit por entrada (cada tecla). Con std::vector eso cuesta una
// allocation de heap por tecla; aca el primer elemento vive dentro de la
// propia HistoryEntry y solo las operaciones multi-edit (reemplazo de
// seleccion, indentacion, pegado sobre seleccion, ...) piden memoria.
//
// API minima: SOLO lo que el sistema de historial usa (push_back, empty,
// size, iteracion normal y reversa). No es un reemplazo general de vector:
// si se necesita algo mas (insert, erase, resize), la respuesta correcta es
// usar std::vector, no engordar esta clase.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <iterator>
#include <new>
#include <utility>

template <typename T, std::size_t N>
class SmallVec {
    static_assert(N >= 1, "SmallVec requiere capacidad inline >= 1");

public:
    SmallVec() = default;

    SmallVec(const SmallVec& other) {
        size_ = other.size_;
        cap_ = other.cap_;
        if (other.heap_) heap_ = alloc(cap_);
        for (std::size_t i = 0; i < size_; ++i) {
            new (data() + i) T(other.data()[i]);
        }
    }

    SmallVec(SmallVec&& other) noexcept {
        size_ = other.size_;
        if (other.heap_) {
            heap_ = other.heap_;
            cap_ = other.cap_;
            other.heap_ = nullptr;
            other.size_ = 0;
            other.cap_ = N;
        } else {
            for (std::size_t i = 0; i < size_; ++i) {
                new (inlineData() + i) T(std::move(other.inlineData()[i]));
                other.inlineData()[i].~T();
            }
            other.size_ = 0;
        }
    }

    SmallVec& operator=(const SmallVec& other) {
        if (this == &other) return *this;
        clearAndFree();
        size_ = other.size_;
        cap_ = other.cap_;
        if (other.heap_) heap_ = alloc(cap_);
        for (std::size_t i = 0; i < size_; ++i) {
            new (data() + i) T(other.data()[i]);
        }
        return *this;
    }

    SmallVec& operator=(SmallVec&& other) noexcept {
        if (this == &other) return *this;
        clearAndFree();
        size_ = other.size_;
        if (other.heap_) {
            heap_ = other.heap_;
            cap_ = other.cap_;
            other.heap_ = nullptr;
            other.size_ = 0;
            other.cap_ = N;
        } else {
            for (std::size_t i = 0; i < size_; ++i) {
                new (inlineData() + i) T(std::move(other.inlineData()[i]));
                other.inlineData()[i].~T();
            }
            other.size_ = 0;
        }
        return *this;
    }

    ~SmallVec() {
        destroyAll();
        freeHeap();
    }

    void push_back(const T& value) {
        if (size_ == cap_) grow();
        new (data() + size_) T(value);
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ == cap_) grow();
        new (data() + size_) T(std::move(value));
        ++size_;
    }

    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }

    T* begin() { return data(); }
    T* end() { return data() + size_; }
    const T* begin() const { return data(); }
    const T* end() const { return data() + size_; }

    using reverse_iterator = std::reverse_iterator<T*>;
    using const_reverse_iterator = std::reverse_iterator<const T*>;

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

private:
    T* inlineData() { return reinterpret_cast<T*>(inlineBuf_); }
    const T* inlineData() const { return reinterpret_cast<const T*>(inlineBuf_); }
    T* data() { return heap_ ? heap_ : inlineData(); }
    const T* data() const { return heap_ ? heap_ : inlineData(); }

    static T* alloc(std::size_t n) {
        return static_cast<T*>(::operator new(sizeof(T) * n));
    }

    void grow() {
        const std::size_t newCap = cap_ * 2;
        T* fresh = alloc(newCap);
        for (std::size_t i = 0; i < size_; ++i) {
            new (fresh + i) T(std::move(data()[i]));
            data()[i].~T();
        }
        freeHeap();
        heap_ = fresh;
        cap_ = newCap;
    }

    void destroyAll() {
        for (std::size_t i = 0; i < size_; ++i) data()[i].~T();
        size_ = 0;
    }

    void freeHeap() {
        if (heap_) {
            ::operator delete(heap_);
            heap_ = nullptr;
        }
        cap_ = N;
    }

    void clearAndFree() {
        destroyAll();
        freeHeap();
    }

    alignas(T) unsigned char inlineBuf_[sizeof(T) * N];
    T* heap_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cap_ = N;
};
