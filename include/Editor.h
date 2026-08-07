#pragma once

#include <string>
#include <vector>
#include "Document.h"
#include "Cursor.h"
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
    };

    static constexpr size_t MAX_UNDO = 1000;

    std::vector<HistoryState> undoStack_;
    std::vector<HistoryState> redoStack_;

    void pushHistory();
    void undo();
    void redo();
    void applyState(const HistoryState& state);
};
