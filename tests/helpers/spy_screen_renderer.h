#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "rendering/ScreenRenderer.h"

// SpyScreenRenderer: implementación observacional del puerto ScreenRenderer.
// - No emite bytes, no conoce ANSI ni terminales.
// - Todos los contadores son ACUMULATIVOS y nunca se resetean: cada test
//   captura la base antes de actuar y afirma deltas después. No hay método
//   reset a propósito (un reset a mitad de secuencia borraría la señal que
//   el test quiere observar).
// - Ownership claro: el test inyecta vía injectSpy() y solo observa.
//   Sin dynamic_cast ni getters en Editor.
struct SpyScreenRenderer : public ScreenRenderer {
    // Invalidaciones totales desde la creación (acumulativo).
    int invalidateCount = 0;
    // Invalidación pendiente: invalidateCache la arma, cualquier render*
    // la consume. Bool, no contador: en el backend real invalidar es
    // idempotente (dos invalidaciones seguidas equivalen a una).
    bool pendingInvalidation = false;

    // Contadores de render por tipo (Abrir y Guardar como separados: un
    // render de SaveAs no debe contaminar la métrica del FileBrowser).
    int renderScreenDiffCount = 0;
    int renderBufferListCount = 0;
    int renderFileListCount = 0;
    int renderSaveAsFileListCount = 0;

    // Theme
    int toggleCount = 0;
    bool dark = true;

    // SyntaxCache attach/detach
    int setCacheCount = 0;
    int setCacheNullCount = 0;
    SyntaxCache* lastCache = nullptr;

    // Último snapshot mínimo por tipo
    std::string lastFilename;
    bool lastModified = false;
    // Snapshot del ÚLTIMO frame: presentación resuelta (ChromeRequest:
    // mensaje + etiqueta + acento + cursor). El spy no conoce State ni
    // expiraciones: igual que el rendering, solo ve lo ya resuelto.
    ChromeRequest lastChrome;
    std::optional<Selection> lastSelection;
    std::optional<Selection> lastSearchHighlight;
    std::optional<BracketPair> lastBracketPair;

    int lastBufferListSelected = -1;
    std::vector<std::string> lastBufferNames;
    int lastFileListSelected = -1;
    int lastFileListScroll = -1;
    std::vector<FileListItem> lastFileItems;
    std::string lastFilePath;

    void invalidateCache() override {
        ++invalidateCount;
        pendingInvalidation = true;
    }

    void setExternalSyntaxCache(SyntaxCache* c) override {
        ++setCacheCount;
        if (!c) ++setCacheNullCount;
        lastCache = c;
    }

    void toggleTheme() override {
        ++toggleCount;
        dark = !dark;
    }

    bool isDarkTheme() const override { return dark; }

    void renderScreenDiff(const Document&,
                          const Cursor&,
                          const Viewport&,
                          const std::string& filename,
                          bool modified,
                          const ChromeRequest& chrome,
                          Sink&,
                          const std::optional<Selection>& selection = std::nullopt,
                          const std::optional<Selection>& searchHighlight = std::nullopt,
                          const std::optional<BracketPair>& bracketPair = std::nullopt) override {
        ++renderScreenDiffCount;
        pendingInvalidation = false; // el render consume
        lastChrome = chrome;
        lastFilename = filename;
        lastModified = modified;
        // Asignación incondicional: un frame sin selección debe LIMPIAR el
        // snapshot, no conservar el valor del frame anterior.
        lastSelection = selection;
        lastSearchHighlight = searchHighlight;
        lastBracketPair = bracketPair;
        // Snapshot del ÚLTIMO frame: las otras clases de render quedan vacías.
        lastBufferListSelected = -1;
        lastBufferNames.clear();
        lastFileListSelected = -1;
        lastFileListScroll = -1;
        lastFileItems.clear();
        lastFilePath.clear();
    }

    void renderBufferList(const std::vector<std::string>& names,
                          int selected,
                          int,
                          int,
                          Sink&) override {
        ++renderBufferListCount;
        pendingInvalidation = false;
        lastBufferNames = names;
        lastBufferListSelected = selected;
        // Limpieza cruzada: el snapshot describe solo el último frame.
        lastChrome = ChromeRequest{};
        lastFilename.clear();
        lastModified = false;
        lastSelection.reset();
        lastSearchHighlight.reset();
        lastBracketPair.reset();
        lastFileListSelected = -1;
        lastFileListScroll = -1;
        lastFileItems.clear();
        lastFilePath.clear();
    }

    void renderFileList(const std::vector<FileListItem>& items,
                        int selected,
                        int scroll,
                        const std::string& path,
                        const MessageBarData& message,
                        int,
                        int,
                        Sink&) override {
        ++renderFileListCount;
        pendingInvalidation = false;
        lastFileItems = items;
        lastFileListSelected = selected;
        lastFileListScroll = scroll;
        lastFilePath = path;
        lastChrome.message = message;
        // Limpieza cruzada: el snapshot describe solo el último frame.
        lastChrome.estado.clear();
        lastFilename.clear();
        lastModified = false;
        lastSelection.reset();
        lastSearchHighlight.reset();
        lastBracketPair.reset();
        lastBufferListSelected = -1;
        lastBufferNames.clear();
    }

    void renderSaveAsFileList(const std::vector<FileListItem>& items,
                              int selected,
                              int scroll,
                              const std::string& path,
                              const MessageBarData& message,
                              int,
                              int,
                              Sink&) override {
        ++renderSaveAsFileListCount;
        pendingInvalidation = false;
        lastFileItems = items;
        lastFileListSelected = selected;
        lastFileListScroll = scroll;
        lastFilePath = path;
        // El input llega ya compuesto en `message` (MessageBarData):
        // se conserva para observarlo (input o mensaje activo).
        lastChrome.message = message;
        // Limpieza cruzada: el snapshot describe solo el último frame
        // (lastChrome.message queda: ES el contenido de este frame).
        lastChrome.estado.clear();
        lastFilename.clear();
        lastModified = false;
        lastSelection.reset();
        lastSearchHighlight.reset();
        lastBracketPair.reset();
        lastBufferListSelected = -1;
        lastBufferNames.clear();
    }
};

// injectSpy: punto único de inyección del spy en el Editor.
// Crea el spy con make_unique, lo transfiere al Editor y devuelve una
// referencia prestada (borrow, no owning): el Editor es el dueño, el test
// solo observa. No volver a llamar a setRenderer mientras se use la
// referencia (quedaría colgante).
inline SpyScreenRenderer& injectSpy(Editor& ed) {
    auto spy = std::make_unique<SpyScreenRenderer>();
    SpyScreenRenderer& ref = *spy;
    ed.setRenderer(std::move(spy));
    return ref;
}