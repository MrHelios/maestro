#pragma once

// Sondeo del estado fisico del boton izquierdo del mouse (X11).
//
// Por que existe: soltar el boton FUERA de la ventana del terminal no
// genera ningun evento SGR (modos 1000+1002+1006), asi que el Editor no
// puede enterarse por la via normal de eventos. Este sondeo es el oraculo
// que el tick de autoscroll consulta para frenar en ese caso.
//
// Semantica del fallback: sin X11 (sin DISPLAY, open fallido o sondeo
// imposible) responde "presionado" (true): nunca frena de mas, solo
// conserva la conducta anterior. Un falso "suelto" seria peor (cortaria
// scrolls legitimos) que un falso "presionado".
//
// Costo: un roundtrip X por consulta; el unico llamador es el tick de
// autoscroll (como maximo 1 cada 100ms y solo con gesto armado).
//
// OWNERSHIP + LAZY: el Display es un recurso externo con dueño explícito.
// Se abre en la PRIMERA consulta (como el viejo static lazy), no en el
// constructor: una ejecución sin gestos de mouse/autoscroll jamás toca X11.
// Se cierra en el destructor (RAII). No hay caché estática de proceso: el
// dueño (main) decide el lifetime y lo inyecta al Editor como oracle. Los
// tests inyectan una lambda y nunca construyen esto.
//
// OJO X11: este header NO incluye <X11/...> a proposito (Xlib hace
// `typedef XID Cursor` y colisiona con document/Cursor.h). Solo declara;
// la implementacion vive en platform/MouseButton.cpp.
// Solo la incluye quien la usa (main).
namespace platform {

// Fuente del estado físico del botón izquierdo. Posee su Display.
class X11MouseButtonQuery {
public:
    X11MouseButtonQuery() = default;
    ~X11MouseButtonQuery();
    X11MouseButtonQuery(const X11MouseButtonQuery&) = delete;
    X11MouseButtonQuery& operator=(const X11MouseButtonQuery&) = delete;
    X11MouseButtonQuery(X11MouseButtonQuery&&) = delete;
    X11MouseButtonQuery& operator=(X11MouseButtonQuery&&) = delete;

    // true = presionado (con fallback "presionado" sin X11, ver arriba).
    bool held() const;

private:
    // Apertura perezosa (mutable: held() es const). opened_ registra que el
    // intento ya ocurrió (exitoso o no) para no reintentar por consulta.
    void ensureDisplay() const;
    // Puntero opaco a Display (evita <X11/...> en el header).
    mutable void* display_ = nullptr;
    mutable bool opened_ = false;
};

} // namespace platform
