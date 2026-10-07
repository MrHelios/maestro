#include "rendering/gui/GuiFont.h"

#include <cstdlib>
#include <utility>

#ifndef HAVE_SDL2_TTF

// Sin SDL_ttf: stub total (el renderer pinta fondo sólido).

GuiFont::GuiFont() = default;
GuiFont::~GuiFont() = default;

bool GuiFont::load(int pixelSize) {
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

#else

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <unordered_map>

namespace {
// Tope del caché de líneas: al superarlo se vacía ENTERO (sin LRU, FIFO ni
// distinción por uso: la entrada 513 distinta invalida las 512 anteriores).
// Deliberadamente tosco pero válido para Fase 1 (unas pocas líneas de prueba).
// TODO(Fase 2): revisar antes del render real del Frame. Con viewport +
// gutter + status + selección las entradas por frame pueden superar 512 y
// entrar en ciclos de vaciado/render que tiran el caché en cada frame.
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
using TextCache = std::unordered_map<CacheKey, SDL_Texture*, CacheKeyHash>;

void destroyCache(TextCache* c) {
    if (!c) return;
    for (auto& kv : *c) SDL_DestroyTexture(kv.second);
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
    auto it = cache->find(key);
    if (it != cache->end()) return static_cast<void*>(it->second);

    if (cache->size() >= kMaxCachedLines) {
        // Invariante: cache_ existe desde construcción hasta destrucción;
        // la evicción vacía en el lugar (sin delete/new) para no dejar
        // nunca un puntero colgando si `new` lanzara.
        // Política deliberadamente tosca (flush total, sin LRU): vale para
        // Fase 1, pero TODO(Fase 2) revisarla antes del render real del
        // Frame (ver kMaxCachedLines).
        for (auto& kv : *cache) SDL_DestroyTexture(kv.second);
        cache->clear();
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
    (*cache)[std::move(key)] = tex;
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
    // hasta destrucción; solo se vacía en el lugar. Además olvida el
    // renderer activo (ver invariante en el header).
    auto* cache = static_cast<TextCache*>(cache_);
    if (cache) {
        for (auto& kv : *cache) SDL_DestroyTexture(kv.second);
        cache->clear();
    }
    renderer_ = nullptr;
}

#endif
