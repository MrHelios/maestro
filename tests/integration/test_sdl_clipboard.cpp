// Integración SDL real de SdlClipboard (camino SDL, no memoria).
//
// Los tests de tests/unit/test_sdlclipboard.cpp corren headless: sin video
// SDL, sdlReady() es false y todo se resuelve en memoria. Pasan aunque
// SDL_SetClipboardText / SDL_GetClipboardText estén rotos. Este archivo
// cubre el camino real: inicializa video SDL explícitamente, copia con
// SdlClipboard y lee de vuelta DIRECTO desde SDL (independiente de la
// memoria del wrapper), incluyendo UTF-8, multilínea y sobrescritura
// externa.
//
// Entorno compatible: cualquier backend de video SDL con clipboard real
// (x11, wayland, ...). Sin video (CI headless sin X) o con drivers
// virtuales ("dummy", "offscreen": SDL responde con un buffer interno,
// indistinguible de la memoria que ya cubren los unit tests) o con un
// backend sin clipboard (el sondeo de capacidad falla), el test hace SKIP.
// Se corre con:
//   make test-one FILTER='sdlclipboard_integration*'
// Bajo X: DISPLAY=:0 make test-one FILTER='sdlclipboard_integration*'
#include <string>

#include "test_framework.h"
#include "platform/clipboard/SdlClipboard.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>

namespace {

// RAII del subsistema de video SDL, autocontenido e independiente del
// orden de ejecución del resto de los tests:
// - Si video YA estaba iniciado (otro test, app embebida), no lo cierra.
// - Si lo inicia este scope, lo cierra al salir (restaura SDL_WasInit).
// - Guarda el clipboard previo y lo restaura best-effort ANTES de cerrar
//   video (tras el Quit ya no hay owner: X11 no persiste sin manager).
//   No contamina el clipboard del usuario ni de otros tests.
class SdlVideoScope {
public:
    SdlVideoScope() {
        alreadyUp_ = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
        if (!alreadyUp_) {
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return;
            started_ = true;
        }
        // Descarta drivers virtuales: su clipboard es un buffer interno de
        // SDL, indistinguible de la memoria que ya cubren los unit tests
        // (verificado: "offscreen" responde al roundtrip sin backend real).
        const char* driver = SDL_GetCurrentVideoDriver();
        if (!driver || std::string(driver) == "dummy" ||
            std::string(driver) == "offscreen") {
            cleanup();
            dummy_ = true;
            return;
        }
        // Guarda el clipboard previo para restaurarlo al salir.
        if (SDL_HasClipboardText() == SDL_TRUE) {
            char* p = SDL_GetClipboardText();
            if (p) {
                saved_ = p;
                hasSaved_ = true;
                SDL_free(p);
            }
        }
        ready_ = true;
    }

    ~SdlVideoScope() { cleanup(); }

    SdlVideoScope(const SdlVideoScope&) = delete;
    SdlVideoScope& operator=(const SdlVideoScope&) = delete;

    bool ready() const { return ready_; }
    bool dummy() const { return dummy_; }

private:
    void cleanup() {
        if (ready_ || started_) {
            if (hasSaved_) SDL_SetClipboardText(saved_.c_str());
            if (started_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        ready_ = false;
        started_ = false;
    }

    bool alreadyUp_ = false;
    bool started_ = false;
    bool ready_ = false;
    bool dummy_ = false;
    bool hasSaved_ = false;
    std::string saved_;
};

// Lee directo desde SDL (fuente de verdad del backend, sin pasar por la
// memoria de SdlClipboard). Vacío == clipboard real vacío, no fallback.
static std::string readSdlDirect(bool& ok) {
    ok = false;
    if (SDL_HasClipboardText() == SDL_FALSE) {
        ok = true;
        return "";
    }
    char* p = SDL_GetClipboardText();
    if (!p) return "";
    std::string out(p);
    SDL_free(p);
    ok = true;
    return out;
}

}  // namespace
#endif

TEST(sdlclipboard_integration_real_sdl_roundtrip) {
#ifndef HAVE_SDL2
    SKIP("sin HAVE_SDL2: no hay camino SDL que probar (ver unit, memoria)");
#else
    SdlVideoScope video;
    if (video.dummy())
        SKIP("driver SDL virtual (dummy/offscreen): buffer interno, no backend real");
    if (!video.ready())
        SKIP("sin video SDL (headless sin X): camino memoria cubierto en unit");

    // Garantía de que este test ejercita el camino SDL real de
    // SdlClipboard (sdlReady() true), no el fallback de memoria.
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0);

    // Sondeo de capacidad del backend, vía SDL directo: escribe y relee.
    // Un backend sin clipboard (set falla o readback difiere) -> SKIP, no
    // FAIL: no hay nada real que verificar en este entorno. Los drivers
    // virtuales ya se excluyeron arriba (falso positivo del sondeo).
    const std::string probe = "__maestro_clip_probe__";
    if (SDL_SetClipboardText(probe.c_str()) != 0)
        SKIP("backend SDL sin clipboard (SDL_SetClipboardText falla)");
    bool ok = false;
    if (readSdlDirect(ok) != probe || !ok)
        SKIP("backend SDL sin clipboard (readback difiere)");

    SdlClipboard cb;
    CHECK(cb.copy("hola SDL"));
    CHECK(cb.ownsClipboard());

    // Lectura posterior DIRECTO desde SDL: prueba que la copia se publicó
    // en el backend del sistema, independiente de la memoria del wrapper.
    CHECK_EQ(readSdlDirect(ok), "hola SDL");
    CHECK(ok);
    auto p = cb.paste();
    CHECK(p.has_value());
    if (p) CHECK_EQ(*p, "hola SDL");

    // UTF-8 + multilínea por el camino real, en ambas direcciones.
    const std::string rich =
        std::string("caf\xC3\xA9 \xF0\x9F\x98\x80\nl\xc3\xadnea 2\nl\xc3\xadnea 3");
    CHECK(cb.copy(rich));
    CHECK_EQ(readSdlDirect(ok), rich);
    CHECK(ok);
    auto pr = cb.paste();
    CHECK(pr.has_value());
    if (pr) CHECK_EQ(*pr, rich);

    // Testigo por instancia: otra instancia sin copy() propio no es dueña
    // aunque el backend tenga texto (sin falso positivo cruzado).
    SdlClipboard other;
    CHECK(!other.ownsClipboard());

    // Sobrescritura externa REAL (vía SDL directo, como otra app): el
    // wrapper la detecta (owns false) y paste la refleja (source-of-truth).
    CHECK_EQ(SDL_SetClipboardText("external-app"), 0);
    CHECK(!cb.ownsClipboard());
    CHECK_EQ(readSdlDirect(ok), "external-app");
    CHECK(ok);
    auto pe = cb.paste();
    CHECK(pe.has_value());
    if (pe) CHECK_EQ(*pe, "external-app");
#endif
}
