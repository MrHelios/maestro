#include "test_framework.h"
#define private public
#include "app/Editor.h"
#undef private
#include "rendering/ScreenRenderer.h"
#include "rendering/NullScreenRenderer.h"
#include "rendering/Sink.h"
#include "helpers/FakeClipboard.h"
#include "filesystem/NullFileWatcher.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstdio>

using testfw::TempFile;

// NullScreenRenderer es el default del Editor(clipboard, watcher).
// No pinta (render* descartan), no cachea (invalidate no-op), tema opaco
// interno (arranca oscuro), sin sintaxis externa.
TEST(null_screen_renderer_noop_render) {
    NullScreenRenderer nsr;
    // Sink que cuenta: NullSink descarta sin observar, así que para probar
    // "no emite" hace falta contar llamadas y bytes (no existe en Sink.h).
    struct CountingSink : public Sink {
        int calls = 0;
        size_t bytes = 0;
        bool writeStdout(const std::string& s) override {
            ++calls;
            bytes += s.size();
            return true;
        }
    } sink;

    // renderScreenDiff no emite nada
    Document doc; doc.restore({"x"});
    Cursor cur; Viewport vp; vp.top=0; vp.height=1; vp.width=10;
    Message msg; State st = State::Navegacion;
    nsr.renderScreenDiff(doc, cur, vp, "t", false, msg, st, sink,
                         std::nullopt, std::nullopt, std::nullopt);
    nsr.renderBufferList({"a"}, 0, 10, 10, sink);
    std::vector<FileListItem> items;
    nsr.renderFileList(items, 0, 0, "/", msg, 10, 10, sink);
    CHECK(sink.calls == 0);
    CHECK(sink.bytes == 0);

    // invalidateCache no hace nada observable
    nsr.invalidateCache();

    // setExternalSyntaxCache no hace nada
    nsr.setExternalSyntaxCache(nullptr);

    // toggleTheme alterna flag interno
    CHECK(nsr.isDarkTheme());
    nsr.toggleTheme();
    CHECK(!nsr.isDarkTheme());
    nsr.toggleTheme();
    CHECK(nsr.isDarkTheme());
}

// Editor(clipboard, watcher) usa NullScreenRenderer explícito.
// renderFrame no crashea, no emite con StringSink, toggleTheme via command funciona.
TEST(editor_null_renderer_no_crash_no_emit) {
    auto clipboard = std::make_unique<FakeClipboard>();
    auto watcher = std::make_unique<NullFileWatcher>();
    Editor ed(std::move(clipboard), std::move(watcher));

    StringSink sink;

    ed.setSink(sink);
    ed.renderFrame(); // no emite, no crashea
    CHECK(sink.buf.empty());

    // El toggle delega al Null (alterna su flag opaco) y deja mensaje.
    ed.executeCommand("theme.toggle");
    CHECK(ed.statusMessage_.text == "Tema claro");
}

// El ctor total y setRenderer exigen renderer no-nulo: pasar nullptr es
// error de programación. En debug (assert activo) aborta con SIGABRT; en
// release setRenderer lo ignora y conserva el renderer anterior.
#ifndef NDEBUG
TEST(editor_total_ctor_rejects_null_setrenderer_guards) {
    fflush(stdout);
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        // Silenciar el mensaje del assert en stderr del hijo.
        freopen("/dev/null", "w", stderr);
        Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<NullFileWatcher>());
        NullSink sink;
        ed.setSink(sink);
        ed.setRenderer(nullptr); // debe abortar por assert
        _exit(42); // llegó acá => el assert no disparó
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}
#else
TEST(editor_total_ctor_rejects_null_setrenderer_guards) {
    Editor ed(std::make_unique<FakeClipboard>(), std::make_unique<NullFileWatcher>());
    NullSink sink;
    ed.setSink(sink);
    ed.setRenderer(nullptr); // no-op: conserva el Null anterior
    ed.renderFrame(); // sigue andando
}
#endif