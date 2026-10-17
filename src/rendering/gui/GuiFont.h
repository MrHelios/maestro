#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "platform/WindowSize.h"

// ---------------------------------------------------------------------------
// GuiFont (Fase GUI-1): fuente monoespaciada vía SDL_ttf + caché de texturas.
//
// El header es libre de SDL a propósito (punteros opacos): compila con y sin
// HAVE_SDL2_TTF. Sin TTF, load() es no-op y ok() es false (modo degradado).
//
// Responsabilidades:
//   - load(): resuelve el orden de candidatos con resolveFontOrder
//     (rendering/gui/FontOrder.h: --font CLI > MAESTRO_FONT > candidatos
//     del sistema > ~/.fonts), abre el primero que funcione y mide la
//     celda (ancho del avance de 'M', alto de línea). Requiere fuente
//     monoespaciada: MAESTRO_FONT y los candidatos del sistema deben
//     apuntar a fuentes monoespaciadas.
//     POLÍTICA POR ORIGEN: MAESTRO_FONT inválida emite UN aviso por
//     proceso (envFontWarnings) y continúa con el siguiente candidato;
//     ninguna fuente utilizable devuelve false (error terminal).
//   - loadWithPath(): abre EXCLUSIVAMENTE la ruta explícita (--font): si
//     falla, devuelve false SIN fallback a env ni candidatos. La política
//     de error (exit != 0) la aplica el caller (GuiRunLoop con
//     fontOpenDiagnostic).
//   - textTexture(): renderiza una línea UTF-8 a SDL_Texture* cacheada por
//     (texto, color). Las texturas dependen del renderer: clearCache() se
//     llama al cambiar/recrear el SDL_Renderer (la fuente CPU se conserva).
//
// Invariante de renderer: un GuiFont solo puede utilizarse con un único
// SDL_Renderer activo a la vez. GuiFont recuerda el renderer dueño de las
// texturas cacheadas y vacía el caché automáticamente si textTexture()
// recibe otro distinto (además, cambiarlo requiere clearCache() explícito
// desde el caller, p. ej. GuiRenderer::setSdlRenderer).
//
// CICLO DE VIDA DE TEXTURAS (contrato compartido: la optimización de
// navegación lo reutiliza tal cual, sin duplicarlo):
//   - Propiedad: cada textura viva está exactamente en una entrada del map
//     (y su clave una vez en la lista LRU). Se destruye UNA sola vez, en el
//     mismo paso que sale de ambas estructuras: evicción (una por inserción
//     sobre el tope), clearCache/unload o destructor. Tras salir, nadie la
//     referencia: el map ya no la contiene y los callers solo toman
//     prestado el puntero para el RenderCopy inmediato (nunca lo guardan
//     entre frames).
//   - Invalidación: todo lo que cambia el ráster vacía el caché.
//     load()/loadWithPath() con RECARGA EXITOSA (cambio de talla o ruta)
//     vía el clearCache del swap transaccional; una recarga FALLIDA no
//     toca el estado ni vacía el caché (la fuente anterior sigue viva).
//     Cambio de renderer vía clearCache (explícito del caller + automático
//     en textTexture()).
//     La clave (texto, rgba con alfa) es completa PORQUE no existe otra vía
//     que altere el resultado: sin mutación de estilo/hinting sin recarga y
//     con blend fijado al crear.
//   - Obligación del caller: desacoplar (setSdlRenderer(nullptr)/clearCache)
//     ANTES de destruir el SDL_Renderer. La detección de cambio es por
//     identidad de puntero: un renderer nuevo en la misma dirección pasaría
//     por el viejo y devolvería texturas muertas.
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
    // guiScaledFontPixels desde la base del CLI (--font-size, píxeles
    // lógicos; base 16 × drawable/ventana) antes de llamar, tanto al abrir
    // como en cada resize: a 2× pide 32 y la celda resultante (~18×36)
    // mantiene la grilla estable. Se
    // escala la talla en vez de usar TTF_OpenFontDPI a propósito: es
    // equivalente en raster y funciona con cualquier SDL_ttf (sin exigir
    // >=2.0.18).
    bool load(int pixelSize = 16);
    // Variante que usa una ruta explícita (p. ej. --font). Si falla, no
    // intenta fallbacks del sistema y NO altera el estado actual (ver
    // transaccionalidad más abajo).
    bool loadWithPath(int pixelSize, const std::string& path);
    void unload();

    // TRANSACCIONALIDAD (contrato, probado en test_guifont.cpp): open /
    // measure / swap. Tanto load() como loadWithPath() abren la nueva
    // fuente y miden su celda ANTES de tocar el estado actual; solo si todo
    // tiene éxito se sustituye (y ahí sí se vacía el caché, porque el
    // ráster cambió). Una recarga fallida (talla nueva en una ruta que ya
    // no abre, archivo existente que no es una fuente, etc.) devuelve false
    // con la fuente anterior, sus métricas y su caché intactos: los
    // callers de resize (ensureFontPixels) pueden ignorar el retorno sin
    // degradar la grilla. `pixelSize <= 0` se rechaza sin tocar nada.

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
    // El caché está acotado con política LRU (evicción de una sola entrada
    // al superar el tope, con promoción en cada hit).
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
    //
    // SEGURIDAD string_view (contrato): `text` es una vista de longitud
    // explícita y PUEDE no estar terminada en NUL en `data()+size()` (p.
    // ej. un segmento recortado de una línea mayor). SDL_ttf solo acepta
    // C-strings NUL-terminados, así que la implementación COPIA la vista a
    // un std::string NUL-terminado antes de llamar a TTF_RenderUTF8_Blended
    // y usa esa copia como clave del caché. Nunca se pasa `view.data()`
    // directamente a SDL_ttf. Con NULes embebidos TTF mide hasta el primero
    // (limitación heredada de la API C; seguro, sin lecturas OOB).
    void* textTexture(void* sdlRenderer, std::string_view text,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF);

    // Ancho en píxeles de `text` con la fuente actual (0 si no hay fuente).
    // Acepta string_view con la misma disciplina que textTexture: se copia
    // a un NUL-terminado antes de TTF_SizeUTF8 (nunca view.data() directo).
    int textWidth(std::string_view text) const;

    // Libera todas las texturas (cambio de renderer o de tema si se quiere
    // forzar; el color ya va en la clave así que el toggle no lo exige).
    // También olvida el renderer activo: la próxima textTexture() adoptará
    // el que reciba.
    void clearCache();

    // Observabilidad del caché (solo lectura, para tests/métricas):
    //   cacheSize()     -> entradas vivas (<= maxCacheSize()).
    //   cacheHits()     -> aciertos acumulados de textTexture().
    //   cacheMisses()   -> misses acumulados (intentos de raster).
    //   cacheEvictions()-> evicciones LRU acumuladas (una por inserción
    //                      sobre el límite, nunca flush total).
    //   cacheDestroys() -> llamadas acumuladas a SDL_DestroyTexture por
    //                      este caché (evicción + clearCache/unload/dtor).
    //                      Cada textura viva se destruye EXACTAMENTE una vez
    //                      al salir del map+lista; monotónico y solo tests.
    // Sin TTF siempre 0. No alteran el pintado.
    size_t cacheSize() const;
    size_t maxCacheSize() const;
    // Solo tests: fija el tope del caché LRU (0 = restaura el default 512).
    // Si el caché actual excede el nuevo tope, evicta LRU hasta encajar
    // (destruyendo una vez cada textura, contando evicciones+destrucciones).
    // Permite secuencias deterministas mínimas (A,B,C + re-touch A + D)
    // sin rellenar 512 entradas. No usar en producción.
    void setMaxCacheSizeForTests(size_t n);
    unsigned long long cacheHits() const;
    unsigned long long cacheMisses() const;
    unsigned long long cacheEvictions() const;
    unsigned long long cacheDestroys() const;

    // Avisos por MAESTRO_FONT inválida acumulados en el proceso (0..1:
    // el aviso es UNA vez por proceso aunque la recarga se repita). Solo
    // observabilidad para tests; con el stub sin TTF siempre 0.
    unsigned long long envFontWarnings() const;

    // Solo tests: silencia el fprintf del aviso de MAESTRO_FONT (el
    // contador envFontWarnings sigue contando igual). Default true: el
    // runner de tests no debe emitir diagnósticos por stderr. Los tests
    // que disparan la política la silencian/restauran con RAII.
    static void setEnvWarnPrintForTests(bool enabled);

    // Solo tests: reinicia el estado global de la política de aviso
    // (bandera "ya avisó" + contador a 0) para que un test de política
    // mida SOLO su propio disparo y no dependa de qué tests corrieron
    // antes en el mismo proceso. La semántica de producción (una vez por
    // proceso) no cambia: ningún camino productivo llama a esto.
    static void resetEnvWarnStateForTests();

private:
    // Reemplazo transaccional (implementado en el .cpp sobre SDL_ttf):
    // explicitPath vacío = resolución con FontOrder (load); con valor =
    // ruta única Cli sin fallback (loadWithPath).
    bool openWithMetrics(int pixelSize,
                         const std::optional<std::string>& explicitPath);
    void* font_ = nullptr;     // TTF_Font* opaco (solo el .cpp conoce SDL_ttf)
    void* cache_ = nullptr;    // caché (texto,color)->SDL_Texture*, opaco
    void* renderer_ = nullptr;  // SDL_Renderer* opaco dueño del caché (o
                                // nullptr si el caché está vacío/desconocido)
    std::string path_;
    int pixelSize_ = 16;
    // Override del tope LRU solo para tests (0 = default kMaxCachedLines).
    size_t testMaxCache_ = 0;
    // Fallback canónico (ver platform::kFallbackCellW/H en WindowSize.h, P3):
    // no duplicar literales.
    int cellW_ = platform::kFallbackCellW;
    int cellH_ = platform::kFallbackCellH;
};
