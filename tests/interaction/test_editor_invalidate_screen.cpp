// Invalidación total de pantalla (vuelta de SIGCONT).
//
// Contrato: tras invalidateScreen(), el próximo renderFrame emite el
// frame completo aunque nada haya cambiado (el diff parte de cero).
// Sin invalidar, el segundo render sale (casi) vacío por el fast path.

#include <string>

#include "test_framework.h"

#include "rendering/Sink.h"
#include "rendering/tty/TtyRenderer.h"
#include "helpers/test_tty_renderer.h"
#include "app/Editor.h"

namespace {

class CaptureSink : public Sink {
public:
    bool writeStdout(const std::string& s) override {
        out += s;
        return true;
    }
    std::string out;
};

}  // namespace

TEST(editor_invalidate_screen_repaints_full_frame) {
    Editor ed;
    // Bytes ANSI: el default del Editor es Null neutro, se inyecta el real.
    ed.setRenderer(makeTtyTestRenderer());
    CaptureSink sink;
    ed.setSink(sink);

    ed.renderFrame();
    const size_t full = sink.out.size();
    CHECK(full > 1000);  // frame 24x80 completo, no vacío

    // Sin cambios: el diff no emite casi nada (fast path).
    sink.out.clear();
    ed.renderFrame();
    const size_t diff = sink.out.size();
    CHECK(diff < full);

    // Tras invalidar: frame completo otra vez, idéntico al primero.
    ed.invalidateScreen();
    sink.out.clear();
    ed.renderFrame();
    CHECK_EQ(sink.out.size(), full);
}
