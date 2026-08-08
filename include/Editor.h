#pragma once

#include <optional>
#include <string>
#include <vector>
#include "Document.h"
#include "Cursor.h"
#include "Selection.h"
#include "Viewport.h"
#include "Renderer.h"
#include "Terminal.h"
#include "Event.h"

// Maquina de estados interna del editor.
//   Normal:  edicion corriente. Ctrl+S entra a seleccion, Ctrl+K al prefijo.
//   Prefix:  tras Ctrl+K; el siguiente evento decide (Ctrl+S guarda,
//            Ctrl+Q sale, cualquier otra cosa lo cancela y se descarta).
//   Select:  modo seleccion (activo con Ctrl+S). Las flechas/Home/End
//            extienden la seleccion; ESC la cancela y sale; un caracter
//            o Enter la reemplaza y sale del modo.
//
// IMPORTANTE: estas son DOS cosas distintas que NO hay que confundir.
//   state_ == State::Select  == "el modo seleccion esta ACTIVO" (modo).
//   hasSelection()            == "existe un rango de texto seleccionado",
//                               es decir un rango NO vacio (anchor != position).
//
// Al entrar al modo (Ctrl+S) sin haber movido el cursor, beginSelection()
// fija anchor == position, asi que es posible tener:
//     state_ == State::Select   y   hasSelection() == false
// (modo seleccion listo pero todavia sin texto marcado). Eso es correcto
// por diseño: el modo es la capacidad de extender; hasSelection() es el
// resultado concreto. Un estado solo significa lo que anuncia: "modo"
// NO implica "texto seleccionado".
enum class State {
    Normal,
    Prefix,
    Select,
};

// Editor es el "engine": conoce el Documento, el Cursor, el Viewport,
// el modo, el archivo abierto y si hay cambios sin guardar. Traduce
// Eventos en mutaciones sobre esas piezas. No sabe nada de teclas
// crudas (eso es responsabilidad de Terminal) ni de como se dibuja
// (eso es responsabilidad de Renderer).
class Editor {
public:
    Editor();

    // Abre (o crea) el archivo indicado.
    bool openFile(const std::string& path);

    // Corre el ciclo principal:
    //   mientras siga abierto:
    //     leer evento
    //     actualizar estado
    //     renderizar
    void run();

    // --- Consultas sobre la seleccion ---
    // true solo si hay un rango NO vacio seleccionado (anchor != position).
    // No confundir con state_ == State::Select (modo activo), que puede ser
    // true aunque no haya texto marcado todavia. "Seleccion activa" (modo)
    // y "texto seleccionado" (rango) son cosas distintas.
    bool hasSelection() const;
    // Seleccion actual, si la hay (normalizada: start antes que end).
    std::optional<Normalized> selection() const;

private:
    Document document_;
    Cursor cursor_;
    Viewport viewport_;
    Renderer renderer_;
    Terminal terminal_;

// Estado de la maquina de estados (Normal/Prefix/Select).
// state_ == Select significa SOLO "modo seleccion activo". No implica
// que haya texto seleccionado: ver hasSelection() (rango no vacio).
// Los dos se pueden mismatchear (Select sin seleccion, p.ej. justo
// despues de Ctrl+S sin mover el cursor).
    State state_ = State::Normal;
    // Estado previo, guardado al entrar en Prefix para volver a el si
    // el prefijo se cancela o ejecuta (p.ej. guardar sin salir de
    // seleccion si el prefijo se abrio estando en Select).
    State priorState_ = State::Normal;
    std::string filename_;
    bool modified_ = false;
    bool running_ = true;
    std::string statusMessage_;

    // Seleccion de texto. Esta es SU casa: Document y Cursor no saben
    // nada de seleccion. Vacio cuando no hay texto seleccionado.
    // La seleccion pertenece al Editor.
    std::optional<Selection> selection_;

    // ---- Helpers de seleccion ----
    // Si no hay seleccion, la inicia poniendo el anchor en la posicion
    // actual del cursor (se llama ANTES de mover el cursor).
    void beginSelection();
    // Sincroniza el extremo de la seleccion con la posicion del cursor.
    void updateSelectionPosition();
    void clearSelection();

    // Ultimo contenido persistido (o el estado inicial si nunca se guardo).
    // modified_ = (contenido actual != savedLines_). Eso permite que undo
    // "limpie" modified_ si vuelve al estado guardado.
    std::vector<std::string> savedLines_;

    void handleEvent(const Event& event);
    void save();
    // Procesa el siguiente evento cuando el editor esta en modo Prefix
    // (tras Ctrl+K). Ctrl+S/Guardar persiste, Ctrl+Q sale; cualquier
    // otra tecla descarta el evento y cancela el prefijo.
    void handlePrefixKey(const Event& event);

    struct HistoryState {
        std::vector<std::string> lines;
        int line = 0;
        int col = 0;
        // Seleccion vigente en ese momento (si habia). Se restaura en
        // undo/redo para que una seleccion borrada/reemplazada regrese
        // a su estado original.
        std::optional<Selection> selection;
    };

    static constexpr size_t MAX_UNDO = 1000;

    std::vector<HistoryState> undoStack_;
    std::vector<HistoryState> redoStack_;

    void pushHistory();
    void undo();
    void redo();
    void applyState(const HistoryState& state);
};
