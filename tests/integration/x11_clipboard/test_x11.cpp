#include "test_framework.h"
#include "clipboard/X11Clipboard.h"
#include <unistd.h>
#include <thread>
#include <atomic>

TEST(x11_maestro_to_maestro) {
    X11Clipboard cb;
    if (!cb.isAvailable()) return;
    CHECK(cb.copy("hello x11"));
    CHECK(cb.ownsClipboard());
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hello x11");
}

TEST(x11_utf8) {
    X11Clipboard cb;
    if (!cb.isAvailable()) return;
    std::string s = std::string("caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x98\x80 \xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF");
    CHECK(cb.copy(s));
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, s);
}

TEST(x11_multiline) {
    X11Clipboard cb;
    if (!cb.isAvailable()) return;
    std::string multi = "linea 1\nlinea 2\nlinea 3";
    CHECK(cb.copy(multi));
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, multi);
}

TEST(x11_ownership) {
    X11Clipboard a, b;
    if (!a.isAvailable() || !b.isAvailable()) return;
    CHECK(a.copy("A"));
    CHECK(a.ownsClipboard());
    usleep(50000);
    a.processEvents();
    CHECK(b.copy("B"));
    CHECK(b.ownsClipboard());
    usleep(50000);
    a.processEvents();
    b.processEvents();
    CHECK(!a.ownsClipboard());
    std::atomic<bool> done{false};
    std::thread pump([&]{
        while (!done) { a.processEvents(); b.processEvents(); usleep(5000); }
    });
    auto p = a.paste();
    done = true;
    pump.join();
    CHECK(p.has_value());
    CHECK_EQ(*p, "B");
}

TEST(x11_maestro_to_firefox_simulated) {
    X11Clipboard maestro, firefox;
    if (!maestro.isAvailable() || !firefox.isAvailable()) return;
    CHECK(maestro.copy("from maestro"));
    usleep(50000);
    maestro.processEvents();
    firefox.processEvents();
    std::atomic<bool> done{false};
    std::thread pump([&]{
        while (!done) { maestro.processEvents(); firefox.processEvents(); usleep(5000); }
    });
    auto p = firefox.paste();
    done = true;
    pump.join();
    CHECK(p.has_value());
    CHECK_EQ(*p, "from maestro");
}

TEST(x11_firefox_to_maestro_simulated) {
    X11Clipboard maestro, firefox;
    if (!maestro.isAvailable() || !firefox.isAvailable()) return;
    CHECK(firefox.copy("from firefox"));
    usleep(50000);
    maestro.processEvents();
    firefox.processEvents();
    std::atomic<bool> done{false};
    std::thread pump([&]{
        while (!done) { maestro.processEvents(); firefox.processEvents(); usleep(5000); }
    });
    auto p = maestro.paste();
    done = true;
    pump.join();
    CHECK(p.has_value());
    CHECK_EQ(*p, "from firefox");
}

TEST(x11_targets_supported) {
    X11Clipboard cb;
    if (!cb.isAvailable()) return;
    CHECK(cb.copy("targets test"));
    cb.processEvents();
    CHECK(cb.ownsClipboard());
}

TEST(x11_empty) {
    X11Clipboard cb;
    if (!cb.isAvailable()) return;
    CHECK(cb.copy(""));
    auto p = cb.paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "");
}
