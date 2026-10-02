#include <cstdio>
#include <memory>
#include "app/Editor.h"
#include "platform/MouseButton.h"
#include "platform/tty/TtyRunLoop.h"
#include "rendering/tty/TtyRenderer.h"
#include "rendering/tty/TtySink.h"

// main es el composition root: acá se cablean los backends concretos
// (TtyRenderer, TtySink, X11MouseButtonQuery, TtyRunLoop) con el engine
// (Editor), que solo conoce puertos neutros (ScreenRenderer, Sink, oracle)
// y la fachada handleEvent/resize/renderFrame/tick. El futuro GUI construirá
// los suyos sin tocar src/app/ (solo este archivo).
int main(int argc, char* argv[]) {
    // Dueño explícito del recurso X11 (sin static de proceso). Vive lo que
    // vive el editor, que lo consulta vía oracle en el tick de autoscroll.
    platform::X11MouseButtonQuery mouseQuery;
    // Backend de escritura real. Vive lo que vive el editor.
    TtySink ttySink;
    // Backend de presentación real + clipboard/watcher reales (vía el ctor
    // explícito de renderer). Único sitio productivo que nombra
    // rendering/tty/ para el renderer.
    Editor editor(std::make_unique<TtyRenderer>());
    editor.setSink(ttySink);
    // Oraculo fisico para el autoscroll: si se suelta fuera de la ventana
    // no llega evento de release; el tick consulta X11 y frena igual.
    // Único cableado productivo: no hay fallback global oculto.
    editor.setMouseButtonPressedQuery([&] { return mouseQuery.held(); });

    // Sin argumentos: arranca con el buffer vacío "SinNombre" que ya
    // crea BufferManager en su constructor. Con un path: lo abre (o crea
    // en memoria si no existe).
    if (argc >= 2) {
        if (Editor::isDirectory(argv[1])) {
            std::fprintf(stderr,
                         "Error: '%s' es una carpeta. Solo se pueden abrir archivos.\n",
                         argv[1]);
            return 1;
        }
        editor.loadIntoActiveBuffer(argv[1]);
    }

    // El loop TTY vive fuera del Editor: mide el tamaño inicial
    // vía resize() y corre el ciclo. El Editor solo recibe eventos.
    TtyRunLoop loop(editor);
    loop.run();
    return 0;
}
