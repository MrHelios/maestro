// Tests de GuiFont (Fase GUI-1): carga, métricas, caché e invariante de renderer.
//
// - Sin HAVE_SDL2_TTF: modo degradado (stub total).
// - Con HAVE_SDL2_TTF: load/unload/clearCache y cambio de renderer.
// El test de cambio de renderer usa el driver de video "dummy" de SDL2:
// hermético (sin ventana real) y con SKIP si el entorno no lo soporta.

#include "rendering/gui/GuiFont.h"

#include <cstdlib>
#include <string>

#include "test_framework.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

#ifndef HAVE_SDL2_TTF

TEST(guifont_sin_ttf_modo_degradado) {
    GuiFont f;
    CHECK(!f.load(16));
    CHECK(!f.ok());
    CHECK(f.cellW() == 9);
    CHECK(f.cellH() == 18);
    CHECK(f.textTexture(nullptr, "abc", 0xD4, 0xD4, 0xD4) == nullptr);
    f.clearCache();  // no crash
    f.clearCache();  // repetible
    f.unload();
    CHECK(!f.ok());
    CHECK(f.path().empty());
    CHECK(f.textWidth("abc") == 0);
}

#else  // HAVE_SDL2_TTF

TEST(guifont_load_invalido_no_altera_estado) {
    GuiFont f;
    CHECK(!f.load(0));
    CHECK(!f.load(-10));
    CHECK(!f.ok());
    CHECK(f.path().empty());
    CHECK(f.pixelSize() == 16);  // intacto (valor por defecto)
}

TEST(guifont_load_exitoso_metricas) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK(f.ok());
    CHECK(f.cellW() > 0);
    CHECK(f.cellH() > 0);
    CHECK(!f.path().empty());
}

TEST(guifont_clearCache_repetido_no_falla) {
    GuiFont f;
    f.clearCache();  // sin fuente: no crash
    f.clearCache();  // repetible
    if (f.load(16)) {
        f.clearCache();  // con fuente (caché vacío): no crash
        f.clearCache();
    }
}

TEST(guifont_unload_libera_fuente) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK(f.ok());
    f.unload();
    CHECK(!f.ok());
    CHECK(f.path().empty());
    f.unload();  // repetible, no crash
    CHECK(!f.ok());
}

#ifdef HAVE_SDL2
namespace {

// Video SDL hermético para tests: driver "dummy" (sin ventana real).
// Restaura SDL_VIDEODRIVER previo al salir.
struct DummyVideo {
    bool ok = false;
    std::string prevDriver;
    bool hadPrev = false;
    DummyVideo() {
        if (const char* p = std::getenv("SDL_VIDEODRIVER")) {
            prevDriver = p;
            hadPrev = true;
        }
        ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        ok = (SDL_Init(SDL_INIT_VIDEO) == 0);
    }
    ~DummyVideo() {
        if (ok) SDL_Quit();
        if (hadPrev)
            ::setenv("SDL_VIDEODRIVER", prevDriver.c_str(), 1);
        else
            ::unsetenv("SDL_VIDEODRIVER");
    }
    DummyVideo(const DummyVideo&) = delete;
    DummyVideo& operator=(const DummyVideo&) = delete;
};

// Pareja de ventana+renderer software sobre video dummy (no-owned respecto
// a GuiFont: las texturas se destruyen con unload() antes que esto salga).
struct DummyPair {
    SDL_Window* wA = nullptr;
    SDL_Window* wB = nullptr;
    SDL_Renderer* rA = nullptr;
    SDL_Renderer* rB = nullptr;
    bool ok = false;
    DummyPair() {
        wA = SDL_CreateWindow("maestro-test-a", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
        wB = SDL_CreateWindow("maestro-test-b", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
        if (!wA || !wB) return;
        rA = SDL_CreateRenderer(wA, -1, SDL_RENDERER_SOFTWARE);
        rB = SDL_CreateRenderer(wB, -1, SDL_RENDERER_SOFTWARE);
        ok = (rA != nullptr && rB != nullptr);
    }
    ~DummyPair() {
        if (rA) SDL_DestroyRenderer(rA);
        if (rB) SDL_DestroyRenderer(rB);
        if (wA) SDL_DestroyWindow(wA);
        if (wB) SDL_DestroyWindow(wB);
    }
    DummyPair(const DummyPair&) = delete;
    DummyPair& operator=(const DummyPair&) = delete;
};

}  // namespace

TEST(guifont_cambio_renderer_invalida_cache) {
    // renderer A -> textTexture("abc") -> renderer B -> textTexture("abc"):
    // la segunda llamada nunca debe devolver la textura creada para A.
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* rA = win.rA;
    SDL_Renderer* rB = win.rB;

    void* tA1 = f.textTexture(static_cast<void*>(rA), "abc", 0xD4, 0xD4, 0xD4);
    if (!tA1) SKIP("el renderer dummy no crea texturas");
    void* tA2 = f.textTexture(static_cast<void*>(rA), "abc", 0xD4, 0xD4, 0xD4);
    CHECK(tA1 == tA2);  // hit: mismo renderer, misma clave
    // Prueba conductual de pertenencia: SDL rechaza texturas creadas para
    // otro renderer, así que RenderCopy==0 demuestra que tA1 es de rA.
    if (SDL_RenderCopy(rA, static_cast<SDL_Texture*>(tA1), nullptr, nullptr) != 0)
        SKIP("el renderer dummy no soporta RenderCopy (chequeo conductual)");

    // Cambio de renderer: el caché debe invalidarse. Sin invalidación, tB
    // sería tA1 (de rA) y el RenderCopy sobre rB fallaría.
    void* tB = f.textTexture(static_cast<void*>(rB), "abc", 0xD4, 0xD4, 0xD4);
    CHECK(tB != nullptr);
    CHECK(SDL_RenderCopy(rB, static_cast<SDL_Texture*>(tB), nullptr, nullptr) == 0);
    void* tB2 = f.textTexture(static_cast<void*>(rB), "abc", 0xD4, 0xD4, 0xD4);
    CHECK(tB == tB2);  // el nuevo renderer ya tiene su propio caché

    // Y de vuelta a A: también debe fallar la búsqueda (el paso por B lo
    // vació) y la textura resultante pertenecer a rA.
    void* tA3 = f.textTexture(static_cast<void*>(rA), "abc", 0xD4, 0xD4, 0xD4);
    CHECK(tA3 != nullptr);
    CHECK(SDL_RenderCopy(rA, static_cast<SDL_Texture*>(tA3), nullptr, nullptr) == 0);

    // Destruye las texturas ANTES que los renderers (los RAII de arriba
    // salen después, en orden inverso).
    f.unload();
    CHECK(!f.ok());
}

// Dimensiones de textura == grilla de celdas: contrato que sostiene al
// cursor Block (y a paintText). Ambos dibujan la textura tal cual con
// dst = tw×th de SDL_QueryTexture, sin recorte: si una textura de 1
// columna no midiera exactamente cw×ch invadiría celdas vecinas o se
// desalinearía en Y. Se congela para ASCII (siempre) y para un carácter
// ancho real (si la fuente lo avanza doble; si no, se skipea esa parte).
TEST(guifont_textura_1col_mide_celda_exacta) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    const int cw = f.cellW(), ch = f.cellH();
    // Avance == celda (requisito monospace del producto, ver GuiFont.h).
    CHECK_EQ(f.textWidth("h"), cw);
    for (const char* s : {"h", "M", "~", "{", "b"}) {
        void* t = f.textTexture(static_cast<void*>(r), s, 0xD4, 0xD4, 0xD4);
        if (!t) SKIP("el renderer dummy no crea texturas");
        int tw = 0, th = 0;
        SDL_QueryTexture(static_cast<SDL_Texture*>(t), nullptr, nullptr, &tw,
                         &th);
        CHECK_EQ(tw, cw);
        CHECK_EQ(th, ch);
    }
    f.unload();
}

TEST(guifont_textura_ancha_mide_doble_celda_exacta) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    const int cw = f.cellW(), ch = f.cellH();
    const char* kWide[] = {
        u8"\u6F22",  // 漢 CJK
        u8"\u1100",  // ᄀ jamo hangul
        u8"\u2B1B",  // ⬛
        u8"\u2B50",  // ⭐
        u8"\u3002",  // 。
        u8"\uFF21",  // Ａ fullwidth
    };
    std::string wide;
    for (const char* cand : kWide) {
        if (f.textWidth(cand) == 2 * cw) {
            wide = cand;
            break;
        }
    }
    if (wide.empty()) SKIP("fuente sin avance doble para anchos");
    void* t = f.textTexture(static_cast<void*>(r), wide, 0xD4, 0xD4, 0xD4);
    if (!t) SKIP("el renderer dummy no crea texturas");
    int tw = 0, th = 0;
    SDL_QueryTexture(static_cast<SDL_Texture*>(t), nullptr, nullptr, &tw, &th);
    CHECK_EQ(tw, 2 * cw);
    CHECK_EQ(th, ch);
    f.unload();
}
#endif  // HAVE_SDL2

#ifdef HAVE_SDL2
// LRU acotada: hit devuelve la misma textura viva y cuenta recencia.
TEST(guifont_lru_hit_misma_textura_y_contadores) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    void* t1 = f.textTexture(static_cast<void*>(r), "lru-hit", 0xD4, 0xD4, 0xD4);
    if (!t1) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheSize(), 1u);
    void* t2 = f.textTexture(static_cast<void*>(r), "lru-hit", 0xD4, 0xD4, 0xD4);
    CHECK(t1 == t2);
    CHECK(f.cacheHits() >= 1);
    CHECK(f.cacheMisses() >= 1);
    CHECK_EQ(f.cacheSize(), 1u);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t2), nullptr, nullptr) == 0);
    f.unload();
}

// LRU acotada: superar el tope evicta de a una (sin flush total).
// Con flush-total, tras 600 distintas el tamaño sería 600-512=88;
// con LRU debe quedar clavado en el tope y las recientes deben seguir en hit.
TEST(guifont_lru_eviccion_acotada_sin_flush) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    const size_t max = f.maxCacheSize();
    CHECK(max == 512u);
    for (size_t i = 0; i < max + 80; ++i) {
        void* t = f.textTexture(static_cast<void*>(r),
                                "lru-fill-" + std::to_string(i), 0xD4, 0xD4, 0xD4);
        if (!t) SKIP("el renderer dummy no crea texturas");
    }
    CHECK_EQ(f.cacheSize(), max);
    CHECK(f.cacheEvictions() >= 80);
    // La última insertada sigue viva (hit, mismo puntero, usable).
    void* last1 = f.textTexture(static_cast<void*>(r), "lru-fill-591", 0xD4, 0xD4, 0xD4);
    void* last2 = f.textTexture(static_cast<void*>(r), "lru-fill-591", 0xD4, 0xD4, 0xD4);
    CHECK(last1 == last2);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(last2), nullptr, nullptr) == 0);
    f.unload();
}

// Promoción LRU: el hit sube a MRU y sobrevive a la siguiente evicción,
// mientras la verdadera LRU cae. Verifica además que no se devuelve una
// textura ya destruida (la re-creada es usable y distinta).
TEST(guifont_lru_promocion_sobrevive_eviccion) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    const size_t max = f.maxCacheSize();
    void* t0 = f.textTexture(static_cast<void*>(r), "lru-prom-0", 0xD4, 0xD4, 0xD4);
    if (!t0) SKIP("el renderer dummy no crea texturas");
    void* t1 = f.textTexture(static_cast<void*>(r), "lru-prom-1", 0xD4, 0xD4, 0xD4);
    if (!t1) SKIP("el renderer dummy no crea texturas");
    for (size_t i = 2; i < max; ++i) {
        void* t = f.textTexture(static_cast<void*>(r),
                                "lru-prom-" + std::to_string(i), 0xD4, 0xD4, 0xD4);
        if (!t) SKIP("el renderer dummy no crea texturas");
    }
    CHECK_EQ(f.cacheSize(), max);
    // Promociona k0 a MRU; k1 queda como LRU.
    CHECK(f.textTexture(static_cast<void*>(r), "lru-prom-0", 0xD4, 0xD4, 0xD4) == t0);
    // Una inserción nueva debe evictar k1, no k0.
    void* tn = f.textTexture(static_cast<void*>(r), "lru-prom-nueva", 0xD4, 0xD4, 0xD4);
    CHECK(tn != nullptr);
    CHECK_EQ(f.cacheSize(), max);
    // k0 sigue viva (mismo puntero, usable): no fue evictada.
    void* t0b = f.textTexture(static_cast<void*>(r), "lru-prom-0", 0xD4, 0xD4, 0xD4);
    CHECK(t0b == t0);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t0b), nullptr, nullptr) == 0);
    // k1 fue evictada: se re-crea (puntero distinto al destruido) y es usable.
    // No se toca el puntero viejo t1 (destruido): solo se compara desigualdad.
    void* t1b = f.textTexture(static_cast<void*>(r), "lru-prom-1", 0xD4, 0xD4, 0xD4);
    CHECK(t1b != nullptr);
    CHECK(t1b != t1);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t1b), nullptr, nullptr) == 0);
    f.unload();
}

// Cambio de renderer y clear: liberan una sola vez y nunca devuelven texturas viejas.
TEST(guifont_lru_renderer_distinto_y_clear) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    void* tA = f.textTexture(static_cast<void*>(win.rA), "lru-ctx", 0xD4, 0xD4, 0xD4);
    if (!tA) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheSize(), 1u);
    void* tB = f.textTexture(static_cast<void*>(win.rB), "lru-ctx", 0xD4, 0xD4, 0xD4);
    CHECK(tB != nullptr);
    CHECK(tB != tA);  // nunca se devuelve la textura del otro renderer
    CHECK_EQ(f.cacheSize(), 1u);
    CHECK(SDL_RenderCopy(win.rB, static_cast<SDL_Texture*>(tB), nullptr, nullptr) == 0);
    f.clearCache();
    CHECK_EQ(f.cacheSize(), 0u);
    f.clearCache();  // repetible, sin doble-free
    void* tB2 = f.textTexture(static_cast<void*>(win.rB), "lru-ctx", 0xD4, 0xD4, 0xD4);
    CHECK(tB2 != nullptr);
    CHECK(SDL_RenderCopy(win.rB, static_cast<SDL_Texture*>(tB2), nullptr, nullptr) == 0);
    f.unload();
    CHECK_EQ(f.cacheSize(), 0u);
}

// Destructor con entradas vivas: no debe hacer doble-free ni crashear.
// Si hubiera doble liberación, ASan/lo vería; acá al menos no cuelga ni aborta.
TEST(guifont_lru_destructor_con_entradas_vivas_no_crashea) {
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    {
        GuiFont f;
        if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
        for (int i = 0; i < 16; ++i) {
            void* t = f.textTexture(static_cast<void*>(win.rA),
                                    "lru-dtor-" + std::to_string(i), 0xD4, 0xD4, 0xD4);
            if (!t) SKIP("el renderer dummy no crea texturas");
        }
        CHECK_EQ(f.cacheSize(), 16u);
        // Sale de scope sin unload()/clearCache(): el destructor libera una vez.
    }
}
#endif  // HAVE_SDL2

#endif  // HAVE_SDL2_TTF
