#include "rendering/gui/GuiFont.h"

#include <cstdlib>
#include <utility>

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
void GuiFont::unload() {
    font_ = nullptr;
    path_.clear();
}
int GuiFont::cellW() const { return cellW_; }
int GuiFont::cellH() const { return cellH_; }
void* GuiFont::textTexture(void*, const std::string&, uint8_t, uint8_t, uint8_t,
                           uint8_t) {
    return nullptr;
}
int GuiFont::textWidth(const std::string&) const { return 0; }
void GuiFont::clearCache() {}
size_t GuiFont::cacheSize() const { return 0; }
size_t GuiFont::maxCacheSize() const { return 512; }
unsigned long long GuiFont::cacheHits() const { return 0; }
unsigned long long GuiFont::cacheMisses() const { return 0; }
unsigned long long GuiFont::cacheEvictions() const { return 0; }

#else

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
};

void destroyCache(TextCache* c) {
    if (!c) return;
    for (auto& kv : c->map) SDL_DestroyTexture(kv.second.tex);
    delete c;
}

// Candidatos del sistema (orden de preferencia). MAESTRO_FONT los precede.
const char* kFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
};

// Refcount de TTF_Init/TTF_Quit entre todas las GuiFont vivas.
int g_tffInitCount = 0;
}  // namespace

GuiFont::GuiFont() : cache_(new TextCache()) {}
GuiFont::~GuiFont() {
    unload();
    destroyCache(static_cast<TextCache*>(cache_));
    cache_ = nullptr;
}

bool GuiFont::load(int pixelSize) {
    // Tamaño inválido: se rechaza sin tocar el estado actual (no se
    // descarga la fuente en uso ni se delega a SDL_ttf).
    if (pixelSize <= 0) return false;
    unload();
    pixelSize_ = pixelSize;

    if (g_tffInitCount == 0 && TTF_Init() != 0) return false;
    ++g_tffInitCount;

    auto tryOpen = [&](const char* p) -> TTF_Font* {
        if (!p || !*p) return nullptr;
        TTF_Font* f = TTF_OpenFont(p, pixelSize_);
        if (f) path_ = p;
        return f;
    };

    TTF_Font* f = nullptr;
    if (const char* env = std::getenv("MAESTRO_FONT")) f = tryOpen(env);
    for (const char* c : kFontCandidates) {
        if (f) break;
        f = tryOpen(c);
    }
    // ~/.local/share/fonts (JetBrainsMono y cía, nombre exacto variable):
    // se prueban los habituales si lo anterior falló.
    if (!f) {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            const std::string h = home;
            const char* extra[] = {
                "/.local/share/fonts/JetBrainsMono-Regular.ttf",
                "/.fonts/JetBrainsMono-Regular.ttf",
            };
            for (const char* e : extra) {
                if (f) break;
                f = tryOpen((h + e).c_str());
            }
        }
    }
    if (!f) {
        // Sin fuente: se deshace el TTF_Init de arriba.
        if (--g_tffInitCount == 0) TTF_Quit();
        return false;
    }
    font_ = static_cast<void*>(f);

    // Celda: avance horizontal de 'M' (vale para todos los glifos solo
    // si la fuente es monoespaciada, requisito documentado en el header) y
    // alto de línea de la fuente.
    int advance = 0;
    int minx = 0, maxx = 0, miny = 0, maxy = 0;
    if (TTF_GlyphMetrics(f, 'M', &minx, &maxx, &miny, &maxy, &advance) == 0 &&
        advance > 0) {
        cellW_ = advance;
    } else {
        int w = 0, h = 0;
        if (TTF_SizeUTF8(f, "M", &w, &h) == 0 && w > 0) cellW_ = w;
    }
    cellH_ = TTF_FontHeight(f);
    if (cellH_ <= 0) cellH_ = pixelSize_;
    return true;
}

bool GuiFont::loadWithPath(int pixelSize, const std::string& path) {
    if (pixelSize <= 0) return false;
    unload();
    pixelSize_ = pixelSize;

    if (g_tffInitCount == 0 && TTF_Init() != 0) return false;
    ++g_tffInitCount;

    TTF_Font* f = TTF_OpenFont(path.c_str(), pixelSize_);
    if (!f) {
        if (--g_tffInitCount == 0) TTF_Quit();
        return false;
    }
    font_ = static_cast<void*>(f);
    path_ = path;

    int advance = 0;
    int minx = 0, maxx = 0, miny = 0, maxy = 0;
    if (TTF_GlyphMetrics(f, 'M', &minx, &maxx, &miny, &maxy, &advance) == 0 &&
        advance > 0) {
        cellW_ = advance;
    } else {
        int w = 0, h = 0;
        if (TTF_SizeUTF8(f, "M", &w, &h) == 0 && w > 0) cellW_ = w;
    }
    cellH_ = TTF_FontHeight(f);
    if (cellH_ <= 0) cellH_ = pixelSize_;
    return true;
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

void* GuiFont::textTexture(void* sdlRenderer, const std::string& text,
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
    CacheKey key{text, static_cast<uint32_t>(r | (g << 8) | (b << 16) | (a << 24))};
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

    if (cache->map.size() >= kMaxCachedLines && !cache->lru.empty()) {
        // Evicción LRU de UNA sola entrada (la menos usada): se destruye su
        // textura y se borra del map y de la lista. El resto del caché sigue
        // válido (sin flush total ni ciclos de vaciado/render).
        const CacheKey& victim = cache->lru.back();
        auto vit = cache->map.find(victim);
        if (vit != cache->map.end()) {
            SDL_DestroyTexture(vit->second.tex);
            cache->map.erase(vit);
            ++cache->evictions;
        }
        cache->lru.pop_back();
    }
    SDL_Color c{r, g, b, a};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(static_cast<TTF_Font*>(font_),
                                              text.c_str(), c);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(
        static_cast<SDL_Renderer*>(sdlRenderer), surf);
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;
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

int GuiFont::textWidth(const std::string& text) const {
    if (!font_ || text.empty()) return 0;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(static_cast<TTF_Font*>(font_), text.c_str(), &w, &h) != 0)
        return 0;
    return w;
}

void GuiFont::clearCache() {
    // Invariante (HAVE_SDL2_TTF): cache_ siempre existe desde construcción
    // hasta destrucción; solo se vacía en el lugar (una sola liberación por
    // textura). Además olvida el renderer activo (ver invariante en el header).
    auto* cache = static_cast<TextCache*>(cache_);
    if (cache) {
        for (auto& kv : cache->map) SDL_DestroyTexture(kv.second.tex);
        cache->map.clear();
        cache->lru.clear();
    }
    renderer_ = nullptr;
}

size_t GuiFont::cacheSize() const {
    const auto* cache = static_cast<const TextCache*>(cache_);
    return cache ? cache->map.size() : 0;
}

size_t GuiFont::maxCacheSize() const { return kMaxCachedLines; }

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

#endif
