#pragma once
#include "platform/clipboard/SystemClipboard.h"
#include <string>
#include <optional>

// Clipboard GUI vía SDL (SDL_SetClipboardText / SDL_GetClipboardText).
//
// Reemplaza a X11Clipboard en el modo --gui: SDL habla con el clipboard
// nativo (X11/Wayland/Windows/macOS según backend SDL) sin Display propio
// ni protocolo INCR. Por eso es síncrono:
//   fd() == -1, hasPending() == false, processEvents() == no-op.
//
// Fallback de memoria: si SDL2 no está compilado (sin HAVE_SDL2) o el
// subsistema de video no está inicializado (tests headless, Editor creado
// antes de SDL_Init en main), copy/paste operan sobre memoria del proceso
// (igual que NullClipboard). En cuanto SDL video está activo, SDL es la
// fuente de verdad y la memoria queda como respaldo.
//
// ownsClipboard(): con SDL listo, true si el contenido actual del
// clipboard SDL coincide con la última copia PUBLICADA con éxito. Es
// coincidencia de CONTENIDO, no propiedad real: si otra aplicación copia
// exactamente los mismos bytes, devuelve true (SDL no ofrece un
// identificador portable de propietario como el owner X11). Tras un copy
// externo con texto distinto -> false (semántica source-of-truth como
// FakeClipboard/X11). Un copy() fallido NO actualiza el testigo, así que
// nunca puede dar true por coincidencia con contenido externo.
// Sin SDL listo, true si hay respaldo local (modo memoria).
// Coste: cada consulta con SDL listo aloca y lee el texto completo
// (SDL_GetClipboardText + SDL_free). Aceptable porque nada en el Editor
// lo llama en caminos calientes (verificado: sin usos en src/app/; solo
// flujos explícitos de clipboard y tests).
class SdlClipboard : public SystemClipboard {
public:
    bool copy(const std::string& text) override;
    std::optional<std::string> paste() override;
    bool ownsClipboard() const override;
    void processEvents() override {}
    int fd() const override { return -1; }
    bool hasPending() const override { return false; }

private:
#ifdef HAVE_SDL2
    // ¿Hay subsistema de video listo para usar el clipboard SDL?
    static bool sdlReady();
#endif
    // Respaldo local: se actualiza en CADA copy(). Solo es fuente de
    // verdad cuando SDL no está listo (modo memoria).
    std::string localText_;
    bool localHas_ = false;
    // Testigo de la última copia PUBLICADA en SDL con éxito. Solo se
    // actualiza cuando SDL_SetClipboardText() devuelve 0; un intento
    // fallido no lo toca. Solo se consulta con SDL listo.
    std::string publishedText_;
    bool publishedHas_ = false;
};
