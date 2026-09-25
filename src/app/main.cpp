#include <cstdio>
#include "app/Editor.h"
#include "platform/MouseButton.h"

int main(int argc, char* argv[]) {
    Editor editor;
    // Oraculo fisico para el autoscroll: si se suelta fuera de la ventana
    // no llega evento de release; el tick consulta X11 y frena igual.
    editor.setMouseButtonHeldOracle([] { return platform::leftMouseButtonHeld(); });

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

    editor.run();
    return 0;
}
