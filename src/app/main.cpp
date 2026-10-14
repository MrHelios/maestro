#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "app/CliArgs.h"
#include "app/Editor.h"
#include "filesystem/FileWatcherFactory.h"
#include "platform/MouseButton.h"
#include "platform/clipboard/ClipboardFactory.h"
#include "platform/gui/GuiOptions.h"
#include "platform/gui/GuiRunLoop.h"
#include "platform/tty/TtyRunLoop.h"
#include "rendering/gui/GuiRenderer.h"
#include "rendering/tty/TtyRenderer.h"
#include "rendering/tty/TtySink.h"

namespace {
void printHelp(const char* prog) {
    std::printf("Uso: %s [--gui] [opciones-gui] [archivo]\n", prog);
    std::printf("\n");
    std::printf("  sin flags           modo terminal (TTY)\n");
    std::printf("  --gui               modo ventana SDL2 (mismo binario)\n");
    std::printf("  -h, --help          esta ayuda\n");
    std::printf("\n");
    std::printf("Opciones GUI (requieren --gui):\n");
    std::printf("  --font <ruta>       fuente TTF monoespaciada (override MAESTRO_FONT)\n");
    std::printf("  --font-size <pt>    tamaño base en puntos (default 16, escala HiDPI)\n");
    std::printf("  --theme dark|light  tema inicial (default dark)\n");
    std::printf("  --no-hidpi          desactiva SDL_WINDOW_ALLOW_HIGHDPI\n");
    std::printf("  --geometry WxH      tamaño inicial ventana en puntos (ej. 1200x800)\n");
}

}  // namespace

// main es el composition root: acá se cablean los backends concretos
// (TtyRenderer/TtySink/X11MouseButtonQuery/TtyRunLoop o
// GuiRenderer/GuiRunLoop) con el engine (Editor), que solo conoce puertos
// neutros (ScreenRenderer, Sink, oracle) y la fachada
// handleEvent/resize/renderFrame/tick. Un solo binario, dos modos:
// TTY por defecto, GUI con --gui.
int main(int argc, char* argv[]) {
    Args args = parseArgs(argc, argv);
    const char* prog = argc > 0 ? argv[0] : "maestro";
    if (args.help) {
        printHelp(prog);
        return 0;
    }
    if (args.bad) {
        std::fprintf(stderr, "Uso: %s [--gui] [archivo]\n", prog);
        return 1;
    }

    auto openFile = [&](Editor& editor) -> int {
        if (args.file.empty()) return 0;
        if (Editor::isDirectory(args.file)) {
            std::fprintf(stderr,
                          "Error: '%s' es una carpeta. Solo se pueden abrir archivos.\n",
                          args.file.c_str());
            return 1;
        }
        editor.loadIntoActiveBuffer(args.file);
        return 0;
    };

    if (args.gui) {
        // Rama GUI: backend SDL2 + loop de ventana. Sin tocar src/app/
        // más allá de este archivo (el Editor solo ve ScreenRenderer).
        // Clipboard SDL (reemplaza X11: SDL_Get/SetClipboardText, síncrono
        // sin INCR); watcher inotify reusado tal cual (vía makeFileWatcher,
        // el GuiRunLoop lo bombea con pollEvents, sin fd).
        auto guiRenderer = std::make_unique<GuiRenderer>(args.guiOptions);
        GuiRenderer* raw = guiRenderer.get();
        Editor editor(std::move(guiRenderer), makeSdlClipboard(), makeFileWatcher());
        if (int rc = openFile(editor)) return rc;
        GuiRunLoop loop(editor, args.guiOptions);
        loop.setGuiRenderer(raw);
        return loop.run();
    }

    // Rama TTY (comportamiento histórico intacto).
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

    if (int rc = openFile(editor)) return rc;

    // El loop TTY vive fuera del Editor: mide el tamaño inicial
    // vía resize() y corre el ciclo. El Editor solo recibe eventos.
    TtyRunLoop loop(editor);
    loop.run();
    return 0;
}
