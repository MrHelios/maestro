#include "rendering/gui/GuiFont.h"

#include <cstdlib>
#include <string_view>
#include <utility>

#include "rendering/gui/FontOrder.h"

#ifdef HAVE_SDL2_TTF
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#endif

#ifndef HAVE_SDL2_TTF

// Sin SDL_ttf: stub total (el renderer pinta fondo sólido).

GuiFont::GuiFont() = default;
GuiFont::~GuiFont() = default;

bool GuiFont::load(int pixelSize) {
    if (pixelSize <= 0) return false;
    pixelSize_ = pixelSize;
    return false;
}

bool GuiFont::loadWithPath(int pixelSize, const std::string&) {
    if (pixelSize <= 0) return false;
    pixelSize_ = pixelSize;
    return false;
}

bool GuiFont::openWithMetrics(int, const std::optional<std::string>&) {
    return false;
}
void GuiFont::unload() {
    font_ = nullptr;
    path_.clear();
}
int GuiFont::cellW() const { return cellW_; }
int GuiFont::cellH() const { return cellH_; }
void* GuiFont::textTexture(void*, std::string_view, uint8_t, uint8_t, uint8_t,
                           uint8_t) {
    return nullptr;
}
int GuiFont::textWidth(std::string_view) const { return 0; }
void GuiFont::clearCache() {}
size_t GuiFont::cacheSize() const { return 0; }
size_t GuiFont::maxCacheSize() const { return testMaxCache_ ? testMaxCache_ : 512; }
void GuiFont::setMaxCacheSizeForTests(size_t n) { testMaxCache_ = n; }
unsigned long long GuiFont::cacheHits() const { return 0; }
unsigned long long GuiFont::cacheMisses() const { return 0; }
unsigned long long GuiFont::cacheEvictions() const { return 0; }
unsigned long long GuiFont::cacheDestroys() const { return 0; }
unsigned long long GuiFont::envFontWarnings() const { return 0; }
void GuiFont::setEnvWarnPrintForTests(bool) {}
void GuiFont::resetEnvWarnStateForTests() {}

#else

#include <cstdio>
#include <list>
#include <unordered_map>

namespace {
// Tope del caché de texturas con política LRU acotada: al superar el límite
// se destruye ÚNICAMENTE la entrada menos recientemente usada (una por
// inserción), nunca flush total. El límite se mantiene en 512 a propósito:
// no se aumenta a ciegas como sustituto de la evicción.
constexpr size_t kMaxCachedLines = 512;

struct CacheKey {
    std::string text;
    uint32_t rgba = 0;
    bool operator==(const CacheKey& o) const {
        return rgba == o.rgba && text == o.text;
    }
};
struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const noexcept {
        size_t h = std::hash<std::string>{}(k.text);
        h ^= std::hash<uint32_t>{}(k.rgba) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
// Caché LRU: map clave -> {textura, posición en lista} + lista de claves en
// orden MRU->LRU (front = más reciente). En hit se promociona con splice O(1);
// al insertar sobre el límite se evicta solo el back. Toda textura viva está
// exactamente una vez en map y su clave una vez en la lista (invariante).
struct TextCache {
    struct Entry {
        SDL_Texture* tex = nullptr;
        std::list<CacheKey>::iterator lruIt;
    };
    std::unordered_map<CacheKey, Entry, CacheKeyHash> map;
    std::list<CacheKey> lru;
    unsigned long long hits = 0;
    unsigned long long misses = 0;
    unsigned long long evictions = 0;
    // Total de SDL_DestroyTexture ejecutadas por este caché (evicción +
    // clearCache/unload/dtor). Cada textura viva se destruye exactamente una
    // vez al salir de ambas estructuras; monotónico, solo observabilidad.
    unsigned long long destroys = 0;
};

void destroyTextureCounted(TextCache* c, SDL_Texture* t) {
    SDL_DestroyTexture(t);
    if (c) ++c->destroys;
}

void destroyCache(TextCache* c) {
    if (!c) return;
    for (auto& kv : c->map) {
        SDL_DestroyTexture(kv.second.tex);
        ++c->destroys;
    }
    delete c;
}

// Métricas de celda de una fuente ABIERTA (helper del swap transaccional:
// se mide la candidata ANTES de sustituir la actual). cellW = avance de 'M'
// (solo representa la celda si la fuente es monoespaciada); cellH = alto
// de línea, con respaldo en la talla pedida si TTF no lo reporta.
void measureCell(TTF_Font* f, int pixelSize, int& cellW, int& cellH) {
    int advance = 0;
    int minx = 0, maxx = 0, miny = 0, maxy = 0;
    if (TTF_GlyphMetrics(f, 'M', &minx, &maxx, &miny, &maxy, &advance) == 0 &&
        advance > 0) {
        cellW = advance;
    } else {
        int w = 0, h = 0;
        if (TTF_SizeUTF8(f, "M", &w, &h) == 0 && w > 0) cellW = w;
    }
    cellH = TTF_FontHeight(f);
    if (cellH <= 0) cellH = pixelSize;
}

// Refcount de TTF_Init/TTF_Quit entre todas las GuiFont vivas.
int g_tffInitCount = 0;

// Avisos por MAESTRO_FONT inválida en ESTE proceso: máximo 1 (política de
// FontOrder.h; una recarga/resize no vuelve a avisar). Observabilidad para
// tests vía GuiFont::envFontWarnings().
unsigned long long g_envFontWarnings = 0;
// Ya avisó en este proceso (política una-vez; los tests la reinician con
// resetEnvWarnStateForTests para poder medir su propio disparo aislado).
bool g_envWarned = false;

// Canal silenciado solo para tests (GuiFont::setEnvWarnPrintForTests): el
// runner no debe imprimir diagnósticos; el contador sigue contando.
bool g_envWarnPrintEnabled = true;

// Aviso único: la env explícita no se pudo abrir, se continúa con el
// siguiente candidato (nunca error terminal: la env es hint, no override).
// UNA sola vez por proceso: una recarga/resize con la misma env inválida
// no repite el aviso (observado con g_envFontWarnings, 0..1).
void warnEnvFontOnce(const std::string& path) {
    if (g_envWarned) return;
    g_envWarned = true;
    ++g_envFontWarnings;
    if (!g_envWarnPrintEnabled) return;
    fprintf(stderr,
            "maestro: aviso: no se pudo abrir MAESTRO_FONT '%s'; "
            "continuando con el siguiente candidato de fuente\n",
            path.c_str());
}

// Heurística monoespaciada P2 (aviso, nunca rechazo): compara el avance de
// 'M' (ancha) con el de 'i' (angosta en proporcionales). Si ambos se miden
// y difieren, la fuente probablemente no es monoespaciada y la grilla de
// celdas (medida con 'M') no valdrá para todos los glifos. Se avisa UNA sola
// vez por proceso por stderr y se sigue (no se rechazan fuentes válidas por
// una heurística demasiado estricta).
void maybeWarnNonMonospaceOnce(TTF_Font* f, const std::string& path) {
    static bool warned = false;
    if (warned || !f) return;
    int advM = 0, advI = 0;
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    if (TTF_GlyphMetrics(f, 'M', &x0, &x1, &y0, &y1, &advM) != 0 || advM <= 0)
        return;
    if (TTF_GlyphMetrics(f, 'i', &x0, &x1, &y0, &y1, &advI) != 0 || advI <= 0)
        return;
    if (advM == advI) return;
    warned = true;
    std::string where = path.empty() ? "<desconocida>" : path;
    fprintf(stderr,
            "maestro: aviso: la fuente '%s' no parece monoespaciada "
            "(avance 'M'=%d vs 'i'=%d); la grilla se mide con 'M' y puede "
            "desalinearse\n",
            where.c_str(), advM, advI);
}
}  // namespace

GuiFont::GuiFont() : cache_(new TextCache()) {}
GuiFont::~GuiFont() {
    unload();
    destroyCache(static_cast<TextCache*>(cache_));
    cache_ = nullptr;
}

bool GuiFont::load(int pixelSize) {
    // Tamaño inválido: openWithMetrics lo rechaza sin tocar el estado.
    return openWithMetrics(pixelSize, std::nullopt);
}

bool GuiFont::loadWithPath(int pixelSize, const std::string& path) {
    return openWithMetrics(pixelSize, std::optional<std::string>(path));
}

bool GuiFont::openWithMetrics(int pixelSize,
                              const std::optional<std::string>& explicitPath) {
    if (pixelSize <= 0) return false;
    // Ruta explícita vacía: error sin fallback (ni siquiera candidatos).
    if (explicitPath.has_value() && explicitPath->empty()) return false;
    // Refcount de TTF_Init: +1 por el intento (se descuenta si el intento
    // completo falla o al cerrar la fuente que reemplaza). Un reload
    // exitoso queda balanceado (+1 nuevo, -1 viejo).
    const bool needInit = (g_tffInitCount == 0);
    if (needInit) {
        if (TTF_Init() != 0) return false;
    }
    ++g_tffInitCount;

    const auto order = resolveFontOrder(explicitPath);
    for (const FontCandidate& cand : order) {
        // Sin ::explicitPath el orden contiene exactamente un candidato
        // Cli; su fallo termina el intento SIN fallback (política:
        // una --font que no abre es error terminal, ver FontOrder.h).
        TTF_Font* f = TTF_OpenFont(cand.path.c_str(), pixelSize);
        if (!f) {
            if (cand.origin == FontOrigin::Env) warnEnvFontOnce(cand.path);
            // Defensa en origen: la Cli no prueba más candidatos (la
            // resolución ya devolvió solo la explícita, policy P1).
            if (cand.origin == FontOrigin::Cli) break;
            continue;
        }
        int cw = 0, ch = 0;
        measureCell(f, pixelSize, cw, ch);
        if (cw <= 0) {
            // Métricas inutilizables: la candidata no se adopta (y el
            // estado actual no se toca: transaccional).
            TTF_CloseFont(f);
            if (cand.origin == FontOrigin::Env) warnEnvFontOnce(cand.path);
            if (cand.origin == FontOrigin::Cli) break;
            continue;
        }
        // COMMIT del swap: nuevo estado primero, cierre del viejo después.
        TTF_Font* old = static_cast<TTF_Font*>(font_);
        font_ = static_cast<void*>(f);
        path_ = cand.path;
        pixelSize_ = pixelSize;
        cellW_ = cw;
        cellH_ = ch;
        // El ráster cambió (aunque sea solo de talla): el caché de
        // texturas del viejo raster ya no vale.
        clearCache();
        if (old) {
            TTF_CloseFont(old);
            if (--g_tffInitCount == 0) TTF_Quit();
        }
        // P2: aviso único si la fuente no parece monoespaciada (nunca
        // rechazo).
        maybeWarnNonMonospaceOnce(f, path_);
        return true;
    }

    // Nadie abrió: se deshace el TTF_Init de arriba y el estado actual
    // quedó intacto (ni unload ni clearCache: transaccional).
    if (--g_tffInitCount == 0) TTF_Quit();
    return false;
}

void GuiFont::unload() {
    if (font_) {
        TTF_CloseFont(static_cast<TTF_Font*>(font_));
        font_ = nullptr;
        if (--g_tffInitCount == 0) TTF_Quit();
    }
    path_.clear();
    clearCache();
}

int GuiFont::cellW() const { return cellW_; }
int GuiFont::cellH() const { return cellH_; }

void* GuiFont::textTexture(void* sdlRenderer, std::string_view text,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!font_ || !sdlRenderer || text.empty()) return nullptr;
    auto* cache = static_cast<TextCache*>(cache_);
    if (!cache) return nullptr;
    if (sdlRenderer != renderer_) {
        // El caché solo vale para un renderer activo: ante otro distinto se
        // vacía antes de buscar/crear nada (nunca se devuelve una textura
        // creada para otro SDL_Renderer, aunque el caller olvide clearCache).
        clearCache();
        renderer_ = sdlRenderer;
    }
    // Copia NUL-terminada: la vista puede no terminar en NUL en data()+size()
    // y SDL_ttf exige C-string. La copia es además la clave del caché.
    const size_t cap = testMaxCache_ ? testMaxCache_ : kMaxCachedLines;
    CacheKey key{std::string(text),
                 static_cast<uint32_t>(r | (g << 8) | (b << 16) | (a << 24))};
    auto it = cache->map.find(key);
    if (it != cache->map.end()) {
        // Hit: promociona a MRU y devuelve la textura viva. Nunca se devuelve
        // una textura destruida: la evicción borra del map y de la lista a la
        // vez, así que todo lo que sigue en el map está vivo.
        cache->lru.splice(cache->lru.begin(), cache->lru, it->second.lruIt);
        ++cache->hits;
        return static_cast<void*>(it->second.tex);
    }
    ++cache->misses;

    // Render PRIMERO, evicción DESPUÉS: si TTF_RenderUTF8_Blended o
    // SDL_CreateTextureFromSurface fallan, el caché conserva sus entradas
    // válidas (sin pérdida innecesaria de la LRU ni hueco bajo el tope).
    SDL_Color c{r, g, b, a};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(static_cast<TTF_Font*>(font_),
                                              key.text.c_str(), c);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(
        static_cast<SDL_Renderer*>(sdlRenderer), surf);
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;
    if (cache->map.size() >= cap && !cache->lru.empty()) {
        // Evicción LRU de UNA sola entrada (la menos usada): se destruye su
        // textura y se borra del map y de la lista. El resto del caché sigue
        // válido (sin flush total ni ciclos de vaciado/render).
        const CacheKey& victim = cache->lru.back();
        auto vit = cache->map.find(victim);
        if (vit != cache->map.end()) {
            destroyTextureCounted(cache, vit->second.tex);
            cache->map.erase(vit);
            ++cache->evictions;
        }
        cache->lru.pop_back();
    }
    // La superficie blended trae fondo transparente: se fija BLENDMODE_BLEND
    // explícito (defensa en profundidad: los SDL2 modernos ya lo activan
    // solo ante superficies con alfa, pero no todos los backends/versiones
    // lo garantizan; sin blend el RenderCopy pegaría cajas negras opacas).
    // Se fija una vez al crear (el caché conserva el modo por textura).
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    // Publica como MRU: la clave vive en la lista y el map guarda su iterador.
    cache->lru.push_front(key);
    TextCache::Entry e;
    e.tex = tex;
    e.lruIt = cache->lru.begin();
    cache->map.emplace(std::move(key), e);
    return static_cast<void*>(tex);
}

int GuiFont::textWidth(std::string_view text) const {
    if (!font_ || text.empty()) return 0;
    // Copia NUL-terminada: nunca se pasa view.data() directo a SDL_ttf
    // (la vista puede no terminar en NUL en data()+size()).
    const std::string owned(text);
    int w = 0, h = 0;
    if (TTF_SizeUTF8(static_cast<TTF_Font*>(font_), owned.c_str(), &w, &h) != 0)
        return 0;
    return w;
}

void GuiFont::clearCache() {
    // Invariante (HAVE_SDL2_TTF): cache_ siempre existe desde construcción
    // hasta destrucción; solo se vacía en el lugar (una sola liberación por
    // textura). Además olvida el renderer activo (ver invariante en el header).
    auto* cache = static_cast<TextCache*>(cache_);
    if (cache) {
        for (auto& kv : cache->map) destroyTextureCounted(cache, kv.second.tex);
        cache->map.clear();
        cache->lru.clear();
    }
    renderer_ = nullptr;
}

size_t GuiFont::cacheSize() const {
    const auto* cache = static_cast<const TextCache*>(cache_);
    return cache ? cache->map.size() : 0;
}

size_t GuiFont::maxCacheSize() const {
    return testMaxCache_ ? testMaxCache_ : kMaxCachedLines;
}

void GuiFont::setMaxCacheSizeForTests(size_t n) {
    testMaxCache_ = n;
    // Si el nuevo tope deja entradas sobrantes, evicta LRU hasta encajar
    // (una destrucción por textura, contando evicciones+destrucciones).
    // n == 0 restaura el default 512 y también recorta si se excede.
    auto* cache = static_cast<TextCache*>(cache_);
    if (!cache) return;
    const size_t cap = n != 0 ? n : kMaxCachedLines;
    while (cache->map.size() > cap && !cache->lru.empty()) {
        const CacheKey& victim = cache->lru.back();
        auto vit = cache->map.find(victim);
        if (vit != cache->map.end()) {
            destroyTextureCounted(cache, vit->second.tex);
            cache->map.erase(vit);
            ++cache->evictions;
        }
        cache->lru.pop_back();
    }
}

unsigned long long GuiFont::cacheHits() const {
    const auto* cache = static_cast<const TextCache*>(cache_);
    return cache ? cache->hits : 0;
}

unsigned long long GuiFont::cacheMisses() const {
    const auto* cache = static_cast<const TextCache*>(cache_);
    return cache ? cache->misses : 0;
}

unsigned long long GuiFont::cacheEvictions() const {
    const auto* cache = static_cast<const TextCache*>(cache_);
    return cache ? cache->evictions : 0;
}

unsigned long long GuiFont::cacheDestroys() const {
    const auto* cache = static_cast<const TextCache*>(cache_);
    return cache ? cache->destroys : 0;
}

unsigned long long GuiFont::envFontWarnings() const {
    return g_envFontWarnings;
}

void GuiFont::setEnvWarnPrintForTests(bool enabled) {
    g_envWarnPrintEnabled = enabled;
}

void GuiFont::resetEnvWarnStateForTests() {
    g_envWarned = false;
    g_envFontWarnings = 0;
}

#endif
