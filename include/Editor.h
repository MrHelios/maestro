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

// Modo del editor. En v0.1 solo existe el modo de edicion normal,
// pero la enum ya deja lugar para "Comando" (guardar-como, buscar, etc)
// en versiones futuras.
enum class Mode {
    Editing,
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
    // false si no hay texto seleccionado (anchor == position).
    bool hasSelection() const;
    // Seleccion actual, si la hay (normalizada: start antes que end).
    std::optional<Normalized> selection() const;

private:
    Document document_;
    Cursor cursor_;
    Viewport viewport_;
    Renderer renderer_;
    Terminal terminal_;

Mode mode_ = Mode::Editing;
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
