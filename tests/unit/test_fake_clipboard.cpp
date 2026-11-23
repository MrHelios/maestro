// Tests unitarios de FakeClipboard
// FakeClipboard NO es el portapapeles que usa el editor en produccion 
//
// Estos tests validan el doble en aislamiento (sin el Editor):
//   - copia / pegado de texto simple, UTF-8 y multilinea
//   - perdida de propiedad entre dos fakes
//   - copia externa simulada
//   - errores de copia y pegado
//   - processEvents como no-op

#include <string>

#include "test_framework.h"
#include "helpers/FakeClipboard.h"

TEST(fake_clipboard_copy_paste_simple) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;

    CHECK(cb.copy("hola"));
    CHECK(cb.ownsClipboard());

    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hola");
}

TEST(fake_clipboard_empty_initial) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;

    CHECK(!cb.ownsClipboard());

    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "");
}

TEST(fake_clipboard_utf8) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;

    std::string cafe = std::string("caf\xC3\xA9");
    std::string ae = std::string("\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA");
    std::string em = std::string("\xE2\x80\x94");
    std::string jp = std::string("\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF");
    std::string emoji = std::string("\xF0\x9F\x98\x80");

    for (auto s : {cafe, ae, em, jp, emoji}) {
        CHECK(cb.copy(s));

        auto p = cb.paste();
        CHECK(p.has_value());
        CHECK_EQ(*p, s);
    }
}

TEST(fake_clipboard_multiline) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;

    std::string multi = "linea 1\nlinea 2\nlinea 3";
    CHECK(cb.copy(multi));

    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, multi);
}

TEST(fake_clipboard_ownership_loss) {
    FakeClipboard::resetGlobal();
    FakeClipboard a, b;

    CHECK(a.copy("A"));
    CHECK(a.ownsClipboard());
    CHECK(b.copy("B"));
    CHECK(b.ownsClipboard());
    CHECK(!a.ownsClipboard());

    auto p = a.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "B");
}

TEST(fake_clipboard_external_copy) {
    FakeClipboard::resetGlobal();
    FakeClipboard a;

    CHECK(a.copy("A"));
    a.simulateExternalCopy("B");
    CHECK(!a.ownsClipboard());

    auto p = a.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "B");
}

TEST(fake_clipboard_copy_error) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;

    cb.setFailCopy(true);
    CHECK(!cb.copy("hola"));
    CHECK(!cb.ownsClipboard());
}

TEST(fake_clipboard_paste_error) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;

    cb.copy("hola");
    cb.setFailPaste(true);

    auto p = cb.paste();
    CHECK(!p.has_value());
}

TEST(fake_clipboard_processEvents_noop) {
    FakeClipboard::resetGlobal();
    FakeClipboard cb;

    cb.copy("x");
    cb.processEvents();

    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "x");
}
