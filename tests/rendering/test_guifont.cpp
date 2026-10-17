// Tests de GuiFont (Fase GUI-1): carga, métricas, caché e invariante de renderer.
//
// - Sin HAVE_SDL2_TTF: modo degradado (stub total).
// - Con HAVE_SDL2_TTF: load/unload/clearCache y cambio de renderer.
// El test de cambio de renderer usa el driver de video "dummy" de SDL2:
// hermético (sin ventana real) y con SKIP si el entorno no lo soporta.

#include "rendering/gui/GuiFont.h"

#include <cstdlib>
#include <string>
#include <string_view>

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
    // string_view tampoco crea nada (y no crashea aunque no haya NUL).
    const std::string big = "xxHELLOyy";
    CHECK(f.textTexture(nullptr, std::string_view(big.data() + 2, 5), 0xD4,
                        0xD4, 0xD4) == nullptr);
    CHECK(f.textWidth(std::string_view(big.data() + 2, 5)) == 0);
    // Tope testeable en el stub (sin recorte real, pero consistente).
    CHECK(f.maxCacheSize() == 512u);
    f.setMaxCacheSizeForTests(3);
    CHECK(f.maxCacheSize() == 3u);
    f.setMaxCacheSizeForTests(0);
    CHECK(f.maxCacheSize() == 512u);
    CHECK(f.cacheDestroys() == 0u);
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

// POLÍTICA DE FUENTES POR ORIGEN (FontOrder.h, única instancia de las
// reglas; el orden puro se prueba en test_fontorder.cpp). El entorno se
// guarda/restaura con RAII para no filtrar estado a otros tests.
namespace {
struct FontEnvGuard {
    bool hadFont = false;
    std::string fontOld;
    FontEnvGuard() {
        if (const char* f = std::getenv("MAESTRO_FONT")) {
            hadFont = true;
            fontOld = f;
        }
        // El runner de tests no emite diagnósticos: el aviso de env sigue
        // CONTANDO (envFontWarnings) pero no imprime (restaurado al salir,
        // aunque el test falle antes).
        GuiFont::setEnvWarnPrintForTests(false);
        // Aislamiento del estado global de la política (ya-avisó +
        // contador): cada test de política arranca en cero y mide SOLO
        // sus propias cargas, independientemente de qué tests corrieron
        // antes en el mismo proceso.
        GuiFont::resetEnvWarnStateForTests();
    }
    ~FontEnvGuard() {
        if (hadFont)
            setenv("MAESTRO_FONT", fontOld.c_str(), 1);
        else
            unsetenv("MAESTRO_FONT");
        GuiFont::setEnvWarnPrintForTests(true);
    }
};

const char* kDejaVuMono =
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
const char* kLiberationMono =
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf";

bool fontFileExists(const char* p) {
    FILE* fp = fopen(p, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}
}  // namespace

TEST(guifont_env_invalido_avisa_una_vez_y_falla_al_siguiente) {
    // Aislado: el guard reinicia ya-avisó + contador, así que el ==1 de
    // abajo prueba que ESTE test disparó el aviso (no depende de cargas
    // previas del proceso).
    FontEnvGuard g;
    setenv("MAESTRO_FONT", "/no/existe/maestro-test.ttf", 1);
    GuiFont f;
    // La env inválida es un AVISO (una vez) y se continúa: algún candidato
    // del sistema abre y el estado queda utilizable.
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK(f.ok());
    CHECK(!f.path().empty());
    CHECK(f.path() != std::string("/no/existe/maestro-test.ttf"));
    CHECK_EQ(f.envFontWarnings(), 1u);
    // Recarga/cambio de talla (lo que hace cada resize): NO re-avisa.
    if (f.load(18)) CHECK_EQ(f.envFontWarnings(), 1u);
    // Otro GuiFont en el mismo proceso tampoco re-avisa (por proceso).
    GuiFont f2;
    if (f2.load(16)) CHECK_EQ(f2.envFontWarnings(), 1u);
}

TEST(guifont_env_valido_precede_al_sistema) {
    FontEnvGuard g;
    // La env debe ser DISTINTA del primer candidato del sistema: si la env
    // apuntara a DejaVu (primer System), el test pasaría aunque load()
    // ignorara MAESTRO_FONT por completo. Con Liberation (segundo
    // candidato) la única forma de que path() sea la env es la precedencia.
    if (!fontFileExists(kLiberationMono))
        SKIP("falta LiberationMono en este sistema");
    setenv("MAESTRO_FONT", kLiberationMono, 1);
    GuiFont f;
    // El load con env válida NO emite avisos (solo la falla de la env).
    const unsigned long long antes = f.envFontWarnings();
    if (!f.load(16)) SKIP("TTF no abrió la env en este entorno");
    CHECK_EQ(f.path(), std::string(kLiberationMono));
    CHECK_EQ(f.envFontWarnings(), antes);
}

TEST(guifont_cli_prevalece_sobre_env_y_recarga_vuelve_a_resolver) {
    FontEnvGuard g;
    if (!fontFileExists(kDejaVuMono)) SKIP("falta DejaVuSansMono en este sistema");
    if (!fontFileExists(kLiberationMono))
        SKIP("falta LiberationMono en este sistema");
    // Env DISTINTA del primer candidato del sistema (Liberation, no
    // DejaVu): así cada tramo del test discrimina precedencia real.
    setenv("MAESTRO_FONT", kLiberationMono, 1);
    GuiFont f;
    // env simple: MAESTRO_FONT precede al sistema (sin env habría caído a
    // DejaVu, el primer System).
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK_EQ(f.path(), std::string(kLiberationMono));
    // --font (ruta explícita) SIEMPRE gana: el orden de resolución es CLI >
    // env; loadWithPath no consulta env.
    if (!f.loadWithPath(20, kDejaVuMono))
        SKIP("TTF no abrió DejaVuSansMono en este entorno");
    CHECK_EQ(f.path(), std::string(kDejaVuMono));
    CHECK(f.pixelSize() == 20);
    // Recarga normal (sin explícita): se re-resuelve y la env vuelve a
    // preceder a los candidatos del sistema.
    if (f.load(14)) CHECK_EQ(f.path(), std::string(kLiberationMono));
}

TEST(guifont_cli_invalida_error_sin_fallback) {
    FontEnvGuard g;
    if (!fontFileExists(kDejaVuMono)) SKIP("falta DejaVuSansMono en este sistema");
    if (!fontFileExists(kLiberationMono))
        SKIP("falta LiberationMono en este sistema");
    // Env válida (Liberation) DISTINTA de la fuente en uso (DejaVu, puesta
    // por CLI): si una --font inválida cayera al env, path() pasaría de
    // DejaVu a Liberation y el test lo detectaría.
    setenv("MAESTRO_FONT", kLiberationMono, 1);
    GuiFont f;
    // Estado inicial sano vía CLI explícita (no env).
    if (!f.loadWithPath(16, kDejaVuMono)) SKIP("TTF no abrió DejaVuMono en este entorno");
    CHECK(f.ok());
    CHECK_EQ(f.path(), std::string(kDejaVuMono));
    const int cw = f.cellW();
    const int ch = f.cellH();
    const unsigned long long warnings = f.envFontWarnings();
    // La explícita NO abre: false y SIN caída al env (que está sano y
    // habría "salvado" la carga si hubiera fallback). Estado intacto.
    CHECK(!f.loadWithPath(18, "/no/existe/maestro-test.ttf"));
    CHECK(f.ok());
    CHECK_EQ(f.path(), std::string(kDejaVuMono));  // NO es la env (Liberation)
    CHECK_EQ(f.cellW(), cw);
    CHECK_EQ(f.cellH(), ch);
    CHECK(f.pixelSize() == 16);  // la talla pedida tampoco se adoptó
    CHECK_EQ(f.envFontWarnings(), warnings);  // ni siquiera avisó
}

TEST(guifont_recarga_transaccional_conserva_estado) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK(f.ok());
    const std::string path = f.path();
    const int cw = f.cellW();
    const int ch = f.cellH();
    const unsigned long long antesMiss = f.cacheMisses();
    // (El miss count cambia solo si textTexture corre; acá solo el estado.)
    CHECK(!f.loadWithPath(20, "/no/existe/maestro-test.ttf"));
    CHECK(f.ok());
    CHECK_EQ(f.path(), path);       // misma fuente
    CHECK_EQ(f.cellW(), cw);        // mismas métricas
    CHECK_EQ(f.cellH(), ch);
    CHECK(f.pixelSize() == 16);     // la talla anterior sigue la activa
    CHECK_EQ(f.cacheMisses(), antesMiss);  // el caché no se tocó
    // La fuente vieja sigue REALMENTE utilizable (no solo en los getters):
    // textWidth usa la TTF_Font* viva, no un puntero colgante.
    CHECK(f.textWidth("M") > 0);
    // Talla inválida tampoco degrada.
    CHECK(!f.load(0));
    CHECK(f.ok());
    CHECK_EQ(f.path(), path);
    f.unload();
}

TEST(guifont_archivo_existente_no_es_fuente_falla) {
    // La existencia del archivo por sí sola no basta: un .txt (o cualquier
    // no-TTF) falla al abrir como fuente y NO hay fallback silencioso.
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    const std::string path = f.path();
    const int cw = f.cellW();
    const testfw::TempFile txt;
    txt.write("esto no es una fuente valida");
    CHECK(!f.loadWithPath(18, txt.path));
    CHECK(f.ok());
    CHECK_EQ(f.path(), path);
    CHECK_EQ(f.cellW(), cw);
    f.unload();
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
    // k1 fue evictada: se re-crea (miss, no hit sobre la entrada destruida)
    // y es usable. Sin desigualdad de punteros: la dirección puede
    // reutilizarse tras liberar y haría flaky al test.
    const unsigned long long missAntes = f.cacheMisses();
    const unsigned long long evAntes = f.cacheEvictions();
    void* t1b = f.textTexture(static_cast<void*>(r), "lru-prom-1", 0xD4, 0xD4, 0xD4);
    CHECK(t1b != nullptr);
    CHECK_EQ(f.cacheMisses(), missAntes + 1);
    CHECK(f.cacheEvictions() > evAntes);  // la reinserción sobre el tope evicta otra
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
    // Nunca se devuelve la textura del otro renderer: el cambio vacía el
    // caché y re-rasteriza (miss, no hit) en el renderer nuevo; el RenderCopy
    // prueba pertenencia (con una textura de rA fallaría). Sin desigualdad
    // de punteros: la dirección puede reutilizarse tras liberar.
    CHECK_EQ(f.cacheMisses(), 2u);
    CHECK_EQ(f.cacheHits(), 0u);
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

// Cambio de cara o talla re-rasteriza: load()/loadWithPath() vacían el caché
// vía unload, así la misma clave vuelve como miss (no hit sobre el ráster
// viejo) y usable. Aserción por contadores (determinista), no por punteros
// (la dirección puede reutilizarse tras liberar).
TEST(guifont_lru_cambio_fuente_o_talla_re_rasteriza) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    void* t1 = f.textTexture(static_cast<void*>(r), "lru-inv", 0xD4, 0xD4, 0xD4);
    if (!t1) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheSize(), 1u);
    CHECK_EQ(f.cacheMisses(), 1u);
    CHECK_EQ(f.cacheHits(), 0u);
    // Otra talla: invalida; la misma clave re-rasteriza (miss, no hit).
    if (!f.load(20)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK_EQ(f.cacheSize(), 0u);
    void* t2 = f.textTexture(static_cast<void*>(r), "lru-inv", 0xD4, 0xD4, 0xD4);
    if (!t2) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheSize(), 1u);
    CHECK_EQ(f.cacheMisses(), 2u);
    CHECK_EQ(f.cacheHits(), 0u);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t2), nullptr, nullptr) == 0);
    // Reapertura a la misma talla: también vacía (unload previo al abrir).
    if (!f.load(20)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK_EQ(f.cacheSize(), 0u);
    void* t3 = f.textTexture(static_cast<void*>(r), "lru-inv", 0xD4, 0xD4, 0xD4);
    if (!t3) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheMisses(), 3u);
    CHECK_EQ(f.cacheHits(), 0u);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t3), nullptr, nullptr) == 0);
    f.unload();
}

// clearCache olvida renderer y contenido: la próxima petición re-rasteriza
// (miss) en vez de devolver la textura vieja. Es la disciplina de
// detach-antes-de-destruir (ver ciclo de vida en GuiFont.h).
TEST(guifont_lru_clear_olvida_y_no_reutiliza) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    void* t1 = f.textTexture(static_cast<void*>(r), "lru-detach", 0xD4, 0xD4, 0xD4);
    if (!t1) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheMisses(), 1u);
    f.clearCache();
    CHECK_EQ(f.cacheSize(), 0u);
    void* t2 = f.textTexture(static_cast<void*>(r), "lru-detach", 0xD4, 0xD4, 0xD4);
    if (!t2) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheSize(), 1u);
    CHECK_EQ(f.cacheMisses(), 2u);
    CHECK_EQ(f.cacheHits(), 0u);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t2), nullptr, nullptr) == 0);
    f.unload();
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

// LRU determinista mínima (tope 3 vía hook de tests):
// 1. Crear A, B, C. 2. Re-acceder A (pasa a MRU). 3. Insertar D sobre el
// tope. 4. Se expulsa B (la verdadera LRU), no A. La promoción ya mueve A al
// frente, así que el back es B. Se verifica por hits/misses (determinista,
// sin comparar punteros: la dirección puede reutilizarse tras liberar) y por
// usabilidad conductual (RenderCopy sobre el renderer dueño).
TEST(guifont_lru_determinista_abc_touch_a_evicta_b) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    f.setMaxCacheSizeForTests(3);
    CHECK_EQ(f.maxCacheSize(), 3u);
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;

    void* tA = f.textTexture(static_cast<void*>(r), "lru-det-A", 0xD4, 0xD4, 0xD4);
    if (!tA) SKIP("el renderer dummy no crea texturas");
    void* tB = f.textTexture(static_cast<void*>(r), "lru-det-B", 0xD4, 0xD4, 0xD4);
    void* tC = f.textTexture(static_cast<void*>(r), "lru-det-C", 0xD4, 0xD4, 0xD4);
    CHECK(tB != nullptr);
    CHECK(tC != nullptr);
    CHECK_EQ(f.cacheSize(), 3u);
    CHECK_EQ(f.cacheEvictions(), 0u);
    CHECK_EQ(f.cacheDestroys(), 0u);

    // Re-acceso a A: hit, pasa a MRU; orden MRU->LRU: A, C, B.
    const unsigned long long hitsAntes = f.cacheHits();
    void* tA2 = f.textTexture(static_cast<void*>(r), "lru-det-A", 0xD4, 0xD4, 0xD4);
    CHECK(tA2 == tA);
    CHECK_EQ(f.cacheHits(), hitsAntes + 1);
    CHECK_EQ(f.cacheSize(), 3u);

    // Insertar D sobre el tope: una sola evicción (B), una sola destrucción.
    void* tD = f.textTexture(static_cast<void*>(r), "lru-det-D", 0xD4, 0xD4, 0xD4);
    CHECK(tD != nullptr);
    CHECK_EQ(f.cacheSize(), 3u);
    CHECK_EQ(f.cacheEvictions(), 1u);
    CHECK_EQ(f.cacheDestroys(), 1u);

    // A sobrevivió (hit, misma textura viva y usable en su renderer).
    void* tA3 = f.textTexture(static_cast<void*>(r), "lru-det-A", 0xD4, 0xD4, 0xD4);
    CHECK(tA3 == tA);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(tA3), nullptr, nullptr) == 0);

    // B fue expulsada: re-pedirla es miss (re-rasteriza, usable) y evicta otra
    // (una destrucción más, tamaño clavado en el tope).
    const unsigned long long missAntes = f.cacheMisses();
    const unsigned long long evAntes = f.cacheEvictions();
    const unsigned long long desAntes = f.cacheDestroys();
    void* tB2 = f.textTexture(static_cast<void*>(r), "lru-det-B", 0xD4, 0xD4, 0xD4);
    CHECK(tB2 != nullptr);
    CHECK_EQ(f.cacheMisses(), missAntes + 1);
    CHECK_EQ(f.cacheEvictions(), evAntes + 1);
    CHECK_EQ(f.cacheDestroys(), desAntes + 1);
    CHECK_EQ(f.cacheSize(), 3u);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(tB2), nullptr, nullptr) == 0);

    f.setMaxCacheSizeForTests(0);
    CHECK_EQ(f.maxCacheSize(), 512u);
    f.unload();
}

// Cada textura expulsada/liberada se destruye exactamente una vez:
// destroys == 1 tras la evicción de D; clearCache suma las 3 vivas (total 4);
// reconstruir + unload suma la última (total 5); los clear/unload repetidos
// no destruyen nada más (sin doble-free).
TEST(guifont_lru_destroys_exactamente_una_vez) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    f.setMaxCacheSizeForTests(3);
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;

    for (const char* k : {"lru-1vez-A", "lru-1vez-B", "lru-1vez-C"}) {
        if (!f.textTexture(static_cast<void*>(r), k, 0xD4, 0xD4, 0xD4))
            SKIP("el renderer dummy no crea texturas");
    }
    CHECK_EQ(f.cacheDestroys(), 0u);
    if (!f.textTexture(static_cast<void*>(r), "lru-1vez-A", 0xD4, 0xD4, 0xD4))
        SKIP("el renderer dummy no crea texturas");
    if (!f.textTexture(static_cast<void*>(r), "lru-1vez-D", 0xD4, 0xD4, 0xD4))
        SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheEvictions(), 1u);
    CHECK_EQ(f.cacheDestroys(), 1u);
    CHECK_EQ(f.cacheSize(), 3u);

    // clearCache libera las 3 vivas (1+3=4) y es repetible sin doble-free.
    f.clearCache();
    CHECK_EQ(f.cacheSize(), 0u);
    CHECK_EQ(f.cacheDestroys(), 4u);
    f.clearCache();
    CHECK_EQ(f.cacheDestroys(), 4u);

    // Reconstrucción tras clear: re-rasteriza (miss) y queda usable.
    void* t = f.textTexture(static_cast<void*>(r), "lru-1vez-A", 0xD4, 0xD4, 0xD4);
    if (!t) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheSize(), 1u);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t), nullptr, nullptr) == 0);

    // unload libera la viva (total 5); repetido no suma (sin doble-free).
    f.unload();
    CHECK_EQ(f.cacheSize(), 0u);
    CHECK_EQ(f.cacheDestroys(), 5u);
    f.unload();
    CHECK_EQ(f.cacheDestroys(), 5u);
    f.setMaxCacheSizeForTests(0);
}

// Destrucción de la fuente y reconstrucción posterior: unload() vacía el
// caché (size 0); tras load() la misma clave re-rasteriza como miss (no hit
// sobre el ráster viejo) y es usable en el mismo renderer.
TEST(guifont_lru_destruccion_fuente_y_reconstruccion) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;

    void* t1 = f.textTexture(static_cast<void*>(r), "lru-recon", 0xD4, 0xD4, 0xD4);
    if (!t1) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheMisses(), 1u);
    f.unload();
    CHECK(!f.ok());
    CHECK_EQ(f.cacheSize(), 0u);

    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    CHECK_EQ(f.cacheSize(), 0u);
    void* t2 = f.textTexture(static_cast<void*>(r), "lru-recon", 0xD4, 0xD4, 0xD4);
    if (!t2) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheMisses(), 2u);
    CHECK_EQ(f.cacheHits(), 0u);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(t2), nullptr, nullptr) == 0);
    f.unload();
}

// string_view no NUL-terminado: textWidth/textTexture respetan la longitud.
// `view` apunta al medio de un buffer mayor (el byte siguiente NO es NUL);
// la implementación debe medir/cachear "HELLO", no "HELLOyy...". Además la
// vista y el std::string equivalente comparten entrada (misma textura).
TEST(guifont_stringview_no_nul_respeta_longitud) {
    GuiFont f;
    if (!f.load(16)) SKIP("sin fuentes del sistema para GuiFont");
    // Prefijo recortado: si se pasara view.data() directo, TTF mediría "abX".
    const std::string buf = "abX";
    const std::string_view viewAb(buf.data(), 2);  // "ab", seguida de 'X'
    CHECK(viewAb.size() == 2u);
    CHECK(buf.data()[2] == 'X');  // garantiza que no hay NUL tras la vista
    const int wAb = f.textWidth(viewAb);
    const int wAbRef = f.textWidth("ab");
    const int wAbc = f.textWidth("abX");
    CHECK(wAb > 0);
    CHECK_EQ(wAb, wAbRef);
    CHECK(wAb != wAbc);

    // Segmento interior sin NUL al final de la vista.
    const std::string big = "xxHELLOyy";
    const std::string_view viewHello(big.data() + 2, 5);  // "HELLO"
    CHECK(big.data()[7] == 'y');  // el byte tras la vista no es NUL
    CHECK_EQ(f.textWidth(viewHello), f.textWidth("HELLO"));

    DummyVideo video;
    if (!video.ok) SKIP("SDL video no disponible en este entorno");
    DummyPair win;
    if (!win.ok) SKIP("renderer software dummy no disponible");
    SDL_Renderer* r = win.rA;
    const unsigned long long missAntes = f.cacheMisses();
    void* tView =
        f.textTexture(static_cast<void*>(r), viewHello, 0xD4, 0xD4, 0xD4);
    if (!tView) SKIP("el renderer dummy no crea texturas");
    CHECK_EQ(f.cacheMisses(), missAntes + 1);
    // Misma clave lógica: hit, mismo puntero, usable.
    void* tStr =
        f.textTexture(static_cast<void*>(r), "HELLO", 0xD4, 0xD4, 0xD4);
    CHECK(tStr == tView);
    CHECK(SDL_RenderCopy(r, static_cast<SDL_Texture*>(tStr), nullptr, nullptr) == 0);
    f.unload();
}
#endif  // HAVE_SDL2

#endif  // HAVE_SDL2_TTF
