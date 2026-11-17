#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "test_framework.h"
#include "platform/clipboard/ClipboardFactory.h"
#include "platform/clipboard/SdlClipboard.h"
#include "filesystem/InotifyFileWatcher.h"
#include "platform/gui/GuiRunLoop.h"
#include "rendering/Sink.h"

#define private public
#include "app/Editor.h"
#undef private
#include "helpers/FakeClipboard.h"

namespace {

using testfw::TempFile;

static void muteRender(Editor& ed) {
    thread_local NullSink nullSink;
    ed.setSink(nullSink);
}

// Parte un string UTF-8 en codepoints (cada elemento es un caracter completo).
std::vector<std::string> utf8Chars(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        size_t len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : 4;
        out.push_back(s.substr(i, len));
        i += len;
    }
    return out;
}

std::unique_ptr<SdlClipboard> makeClipboard(SdlClipboard*& raw) {
    auto sc = std::make_unique<SdlClipboard>();
    raw = sc.get();
    return sc;
}

struct ClipFixture {
    SdlClipboard* clip = nullptr;
    Editor ed;

    ClipFixture() : ed(makeClipboard(clip)) { muteRender(ed); }

    // Un evento simple (MoveRight, Escape, ...), repetible n veces.
    void press(InputEventType t, int times = 1) {
        for (int k = 0; k < times; ++k) {
            InputEvent e;
            e.type = t;
            ed.handleEvent(e);
        }
    }

    // Una tecla de comando ("i", "s", "c", "p", "a"...).
    void key(const std::string& k) {
        InputEvent e;
        e.type = InputEventType::InsertChar;
        e.text = k;
        ed.handleEvent(e);
    }

    // Entra en modo insercion, escribe (UTF-8 seguro) y vuelve a modo normal.
    void typeText(const std::string& s) {
        key("i");
        for (const auto& ch : utf8Chars(s)) {
            if (ch == "\n") press(InputEventType::InsertNewline);
            else            key(ch);
        }
        press(InputEventType::Escape);
    }

    // Selecciona con 's': mueve el cursor n veces a la derecha y copia con 'c'.
    void selectRightAndCopy(int n) {
        key("s");
        press(InputEventType::MoveRight, n);
        key("c");
    }

    // Cursor al final del documento (clamp), y pega.
    void pasteAtEndOfDoc() {
        press(InputEventType::MoveDown, 100);
        press(InputEventType::MoveEnd);
        key("p");
    }
};

} // namespace

TEST(sdl_clipboard_factory_returns_nonnull) {
    auto cb = makeSdlClipboard();
    
    CHECK(cb != nullptr);
}

TEST(sdl_clipboard_copy_paste_simple) {
    SdlClipboard cb;
    
    CHECK(cb.copy("hola"));
    CHECK(cb.ownsClipboard());
    
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hola");
}

TEST(sdl_clipboard_empty_initial) {
    SdlClipboard cb;
    
    CHECK(!cb.ownsClipboard());
    
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "");
}

TEST(sdl_clipboard_utf8_multiline) {
    SdlClipboard cb;
    
    std::string cafe = std::string("caf\xC3\xA9");
    std::string emoji = std::string("\xF0\x9F\x98\x80");
    std::string multi = "linea 1\nlinea 2\nlinea 3";
    
    for (auto s : {cafe, emoji, multi}) {
        CHECK(cb.copy(s));
        
        auto p = cb.paste();
        CHECK(p.has_value());
        CHECK_EQ(*p, s);
    }
}

TEST(sdl_clipboard_sync_no_fd_no_pending) {
    SdlClipboard cb;
    
    CHECK_EQ(cb.fd(), -1);
    CHECK(!cb.hasPending());
    
    cb.copy("x");
    cb.processEvents();      // no-op, no debe romper nada
    CHECK(!cb.hasPending());
    
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "x");
}

TEST(sdl_clipboard_editor_copy_paste) {
    ClipFixture f;
    f.typeText("hola");
    f.press(InputEventType::MoveHome);

    f.selectRightAndCopy(2);
    CHECK_EQ(f.clip->paste().value(), "ho");

    f.press(InputEventType::MoveEnd);
    f.key("p");
    CHECK_EQ(f.ed.active().document.lineAt(0), "holaho");
}

TEST(sdl_clipboard_editor_copy_paste_multiline) {
    ClipFixture f;
    f.typeText("uno\ndos\ntres");

    // cursor a (0,0)
    f.press(InputEventType::MoveUp, 2);
    f.press(InputEventType::MoveHome);

    // seleccion de "uno\nd"
    f.key("s");
    f.press(InputEventType::MoveDown);
    f.press(InputEventType::MoveRight);
    f.key("c");
    CHECK_EQ(f.clip->paste().value(), "uno\nd");

    f.pasteAtEndOfDoc();
    CHECK_EQ(f.ed.active().document.lineAt(0), "uno");
    CHECK_EQ(f.ed.active().document.lineAt(1), "dos");
    CHECK_EQ(f.ed.active().document.lineAt(2), "tresuno");
    CHECK_EQ(f.ed.active().document.lineAt(3), "d");
}

TEST(sdl_clipboard_editor_copy_paste_utf8) {
    ClipFixture f;
    f.typeText("áéíóú");
    f.press(InputEventType::MoveHome);

    f.selectRightAndCopy(2);              // 2 codepoints = 4 bytes
    CHECK_EQ(f.clip->paste().value(), "áé");

    f.press(InputEventType::MoveEnd);
    f.key("p");
    CHECK_EQ(f.ed.active().document.lineAt(0), "áéíóúáé");
}

TEST(sdl_clipboard_editor_copy_paste_emoji) {
    ClipFixture f;
    f.typeText("a😀b🎉c");
    f.press(InputEventType::MoveHome);
    f.press(InputEventType::MoveRight);   // despues de 'a'

    f.selectRightAndCopy(2);              // "😀b"
    CHECK_EQ(f.clip->paste().value(), "😀b");

    f.press(InputEventType::MoveEnd);
    f.key("p");
    CHECK_EQ(f.ed.active().document.lineAt(0), "a😀b🎉c😀b");
}

TEST(sdl_clipboard_editor_select_all_copy_paste) {
    ClipFixture f;
    const std::string text = "uno\ndos\ntres";
    f.typeText(text);

    f.key("a");                           // selecciona todo
    f.key("c");
    CHECK_EQ(f.clip->paste().value(), text);

    f.pasteAtEndOfDoc();
    CHECK_EQ(f.ed.active().document.lineAt(0), "uno");
    CHECK_EQ(f.ed.active().document.lineAt(1), "dos");
    CHECK_EQ(f.ed.active().document.lineAt(2), "tresuno");
    CHECK_EQ(f.ed.active().document.lineAt(3), "dos");
    CHECK_EQ(f.ed.active().document.lineAt(4), "tres");
}

// --- FILEWATCHER
// @cleanup: posiblemente lo movamos a otro archivo
// ademas solo tiene un caso, deberia que extenderlo

// abrimos un archivo en el editor
// inyectamos un cambio por afuera
// y verificamos que el filewatcher, detecta el cambio
TEST(sdl_clipboard_watcher_idle_step_reloads_external_change) {
    TempFile f;
    f.write("A\n");
    
    auto sc = std::make_unique<SdlClipboard>();
    auto w = std::make_unique<InotifyFileWatcher>();
    Editor ed(std::move(sc), std::move(w));
    
    // desactivamos render real del editor
    muteRender(ed);
    
    CHECK(ed.loadIntoActiveBuffer(f.path));
    CHECK_EQ(ed.active().document.lineAt(0), "A");
    {
        // inyectamos nuevo contenido al archivo    
        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
        out << "B\n";
    }
    
    GuiRunLoop loop(ed);
    bool reloaded = false;
    for (int k = 0; k < 30; ++k) {
        ed.pollExternalEvents();  // lo que run() hace al tope de la vuelta
        loop.idleStep(std::chrono::steady_clock::now());
        if (ed.active().document.lineAt(0) == "B") {
            reloaded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(reloaded);
    CHECK_EQ(ed.active().document.lineAt(0), "B");
    CHECK(!ed.active().modified);
}
