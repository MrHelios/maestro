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
// El Display se abre una sola vez por proceso (static) y no se cierra:
// vive lo que vive la app, igual que el clipboard X11.
//
// OJO X11: este header NO incluye <X11/...> a proposito (Xlib hace
// `typedef XID Cursor` y colisiona con document/Cursor.h). Solo declara;
// la implementacion vive en platform/MouseButton.cpp, que no incluye el
// engine. Solo la incluye quien la usa (main).
namespace platform {

bool leftMouseButtonHeld();

} // namespace platform
