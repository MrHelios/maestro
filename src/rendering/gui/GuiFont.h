#pragma once

#include <cstdint>
#include <string>

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
    bool load(int pixelSize = 16);
    void unload();

    bool ok() const { return font_ != nullptr; }
    // Path elegido en el último load() exitoso (vacío si no hay).
    const std::string& path() const { return path_; }
    int pixelSize() const { return pixelSize_; }

    // Métricas de celda en píxeles (válidas solo si ok()). Fallbacks 9x18
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
    int cellW_ = 9;
    int cellH_ = 18;
};
