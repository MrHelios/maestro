#pragma once

#include <cstdint>
#include <string>

#include "platform/WindowSize.h"

// ---------------------------------------------------------------------------
// GuiFont (Fase GUI-1): fuente monoespaciada vía SDL_ttf + caché de texturas.
//
// El header es libre de SDL a propósito (punteros opacos): compila con y sin
// HAVE_SDL2_TTF. Sin TTF, load() es no-op y ok() es false (modo degradado).
//
// Responsabilidades:
//   - load(): abre el primer candidato disponible (MAESTRO_FONT o lista del
//     sistema) y mide la celda (ancho del avance de 'M', alto de línea).
//     Requiere fuente monoespaciada: MAESTRO_FONT debe apuntar a una
//     fuente monoespaciada, igual que los candidatos del sistema.
//   - textTexture(): renderiza una línea UTF-8 a SDL_Texture* cacheada por
//     (texto, color). Las texturas dependen del renderer: clearCache() se
//     llama al cambiar/recrear el SDL_Renderer (la fuente CPU se conserva).
//
// Invariante de renderer: un GuiFont solo puede utilizarse con un único
// SDL_Renderer activo a la vez. GuiFont recuerda el renderer dueño de las
// texturas cacheadas y vacía el caché automáticamente si textTexture()
// recibe otro distinto (además, cambiarlo requiere clearCache() explícito
// desde el caller, p. ej. GuiRenderer::setSdlRenderer).
// ---------------------------------------------------------------------------
class GuiFont {
public:
    GuiFont();
    ~GuiFont();

    GuiFont(const GuiFont&) = delete;
    GuiFont& operator=(const GuiFont&) = delete;

    // Abre la fuente a `pixelSize` px. Devuelve true si quedó utilizable.
    // Re-llamar con otro tamaño reabre (cierra la anterior).
    // `pixelSize <= 0` se rechaza con false sin alterar el estado actual.
    // La fuente debe ser monoespaciada (MAESTRO_FONT también): la celda
    // se mide con el avance de 'M' y solo vale para todas si lo es.
    // UNIDADES HiDPI (contrato visual-estable): `pixelSize` son píxeles del
    // DRAWABLE, no puntos de ventana. El loop lo escala con
    // guiScaledFontPixels (base 16 × drawable/ventana) antes de llamar: a 2×
    // pide 32 y la celda resultante (~18×36) mantiene la grilla estable. Se
    // escala la talla en vez de usar TTF_OpenFontDPI a propósito: es
    // equivalente en raster y funciona con cualquier SDL_ttf (sin exigir
    // >=2.0.18).
    bool load(int pixelSize = 16);
    // Variante que usa una ruta explícita (p. ej. --font). Si falla, no
    // intenta fallbacks del sistema.
    bool loadWithPath(int pixelSize, const std::string& path);
    void unload();

    bool ok() const { return font_ != nullptr; }
    // Path elegido en el último load() exitoso (vacío si no hay).
    const std::string& path() const { return path_; }
    int pixelSize() const { return pixelSize_; }

    // Métricas de celda en píxeles (válidas solo si ok()). Fallback canónico
    // si se consultan sin fuente (el renderer los usa igual para resize).
    // cellW() es el avance de 'M': solo representa la celda si la fuente
    // es monoespaciada (requisito de load(), ver arriba).
    int cellW() const;
    int cellH() const;

    // Línea UTF-8 -> SDL_Texture* cacheada (owned por el caché, no liberar).
    // Devuelve nullptr si no hay fuente/renderer o falla el render.
    // `sdlRenderer` es SDL_Renderer* opaco. El color va en la clave del caché;
    // el renderer NO: si `sdlRenderer` difiere del renderer activo, el caché
    // se vacía primero para no devolver jamás una textura de otro renderer.
    // El caché está acotado (flush total al llenarse, sin LRU): vale para
    // Fase 1, revisar antes del render real del Frame (ver GuiFont.cpp).
    //
    // DIMENSIONES (contrato que sostiene al pintado sin recorte): la
    // superficie SDL_ttf blended mide la caja de AVANCE del texto (igual
    // que TTF_SizeUTF8), no la caja de tinta. Con fuente monoespaciada
    // (requisito de load()) un glifo de 1 columna produce textura de
    // cw×ch exactos y uno ancho de 2×cw×ch: el caller dibuja con
    // dst = tw×th de SDL_QueryTexture dentro de la celda sin invadir
    // vecinas ni desalinearse en Y. Congelado en
    // guifont_textura_1col/ancha_mide_*_exacta (la parte ancha se skipea
    // sin fuente de avance doble).
    void* textTexture(void* sdlRenderer, const std::string& text,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF);

    // Ancho en píxeles de `text` con la fuente actual (0 si no hay fuente).
    int textWidth(const std::string& text) const;

    // Libera todas las texturas (cambio de renderer o de tema si se quiere
    // forzar; el color ya va en la clave así que el toggle no lo exige).
    // También olvida el renderer activo: la próxima textTexture() adoptará
    // el que reciba.
    void clearCache();

private:
    void* font_ = nullptr;     // TTF_Font* opaco (solo el .cpp conoce SDL_ttf)
    void* cache_ = nullptr;    // caché (texto,color)->SDL_Texture*, opaco
    void* renderer_ = nullptr;  // SDL_Renderer* opaco dueño del caché (o
                                // nullptr si el caché está vacío/desconocido)
    std::string path_;
    int pixelSize_ = 16;
    // Fallback canónico (ver platform::kFallbackCellW/H en WindowSize.h, P3):
    // no duplicar literales.
    int cellW_ = platform::kFallbackCellW;
    int cellH_ = platform::kFallbackCellH;
};
