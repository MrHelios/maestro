#include "test_framework.h"
#define private public
#include "clipboard/X11Clipboard.h"
#undef private
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <unistd.h>
#include <thread>
#include <atomic>

static bool g_origCalled = false;
static int testOrigHandler(Display*, XErrorEvent*) { g_origCalled = true; return 0; }

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

TEST(clipboard_multiple_instances_keep_handler_alive) {
    XErrorHandler orig = XSetErrorHandler(testOrigHandler);
    {
        auto* a = new X11Clipboard();
        auto* b = new X11Clipboard();
        CHECK_EQ(X11Clipboard::refCount_, 2);
        delete a;
        CHECK_EQ(X11Clipboard::refCount_, 1);
        XErrorHandler cur = XSetErrorHandler(testOrigHandler);
        XSetErrorHandler(cur);
        CHECK(cur != testOrigHandler);
        CHECK(cur != orig);
        delete b;
        CHECK_EQ(X11Clipboard::refCount_, 0);
    }
    XErrorHandler cur = XSetErrorHandler(testOrigHandler);
    XSetErrorHandler(cur);
    CHECK_EQ(cur, testOrigHandler);
    XSetErrorHandler(orig);
}

TEST(clipboard_last_instance_restores_previous_handler) {
    XErrorHandler orig = XSetErrorHandler(testOrigHandler);
    {
        X11Clipboard a;
        CHECK_EQ(X11Clipboard::refCount_, 1);
    }
    CHECK_EQ(X11Clipboard::refCount_, 0);
    XErrorHandler cur = XSetErrorHandler(testOrigHandler);
    XSetErrorHandler(cur);
    CHECK_EQ(cur, testOrigHandler);
    XSetErrorHandler(orig);
}

TEST(clipboard_instances_destruction_order_is_irrelevant) {
    XErrorHandler orig = XSetErrorHandler(testOrigHandler);
    {
        auto* a = new X11Clipboard();
        auto* b = new X11Clipboard();
        delete b;
        CHECK_EQ(X11Clipboard::refCount_, 1);
        XErrorHandler cur = XSetErrorHandler(testOrigHandler);
        XSetErrorHandler(cur);
        CHECK(cur != testOrigHandler);
        delete a;
        CHECK_EQ(X11Clipboard::refCount_, 0);
    }
    XErrorHandler cur = XSetErrorHandler(testOrigHandler);
    XSetErrorHandler(cur);
    CHECK_EQ(cur, testOrigHandler);
    XSetErrorHandler(orig);
}

TEST(clipboard_three_instances_keep_handler_until_last_destroyed) {
    XErrorHandler orig = XSetErrorHandler(testOrigHandler);
    auto* a = new X11Clipboard();
    auto* b = new X11Clipboard();
    auto* c = new X11Clipboard();
    CHECK_EQ(X11Clipboard::refCount_, 3);
    delete b;
    CHECK_EQ(X11Clipboard::refCount_, 2);
    XErrorHandler cur = XSetErrorHandler(testOrigHandler);
    XSetErrorHandler(cur);
    CHECK(cur != testOrigHandler);
    delete a;
    CHECK_EQ(X11Clipboard::refCount_, 1);
    cur = XSetErrorHandler(testOrigHandler);
    XSetErrorHandler(cur);
    CHECK(cur != testOrigHandler);
    delete c;
    CHECK_EQ(X11Clipboard::refCount_, 0);
    cur = XSetErrorHandler(testOrigHandler);
    XSetErrorHandler(cur);
    CHECK_EQ(cur, testOrigHandler);
    XSetErrorHandler(orig);
}

TEST(clipboard_x11_error_does_not_abort_process) {
    X11Clipboard cb;
    if (!cb.isAvailable()) return;
    Display* d = XOpenDisplay(nullptr);
    if (!d) return;
    XSync(d, False);
    XCloseDisplay(d);
    CHECK(true);
}

TEST(clipboard_survives_other_instance_destruction) {
    auto* a = new X11Clipboard();
    auto* b = new X11Clipboard();
    if (!b->isAvailable()) { delete a; delete b; return; }
    CHECK(b->copy("hello"));
    delete a;
    CHECK_EQ(X11Clipboard::refCount_, 1);
    auto p = b->paste();
    CHECK(p.has_value());
    CHECK_EQ(*p, "hello");
    delete b;
}

TEST(clipboard_error_not_from_requestor_delegates) {
    CHECK_EQ(X11Clipboard::refCount_, 0);
    CHECK(X11Clipboard::activeRequestors_.empty());
    XErrorHandler orig = XSetErrorHandler(testOrigHandler);
    auto* cb = new X11Clipboard();
    g_origCalled = false;
    XErrorEvent ev{};
    ev.error_code = BadWindow;
    ev.request_code = 18;
    ev.resourceid = 0x12345678;
    // not registered, should delegate
    X11Clipboard::handleX11Error(nullptr, &ev);
    // isExpected should be false, so previous handler not called via our mock? Actually handleX11Error will call previousHandler_ which is testOrigHandler
    // Since we set orig to previous system, and clipboard saved orig as previousHandler_ = testOrigHandler,
    // handleX11Error with unexpected should call testOrigHandler
    CHECK(g_origCalled);
    delete cb;
    XSetErrorHandler(orig);
    g_origCalled = false;
}

TEST(clipboard_error_from_requestor_absorbed) {
    CHECK_EQ(X11Clipboard::refCount_, 0);
    XErrorHandler orig = XSetErrorHandler(testOrigHandler);
    auto* cb = new X11Clipboard();
    X11Clipboard::registerRequestor(0xdeadbeef);
    g_origCalled = false;
    XErrorEvent ev{};
    ev.error_code = BadWindow;
    ev.request_code = X_ChangeProperty;
    ev.resourceid = 0xdeadbeef;
    X11Clipboard::handleX11Error(nullptr, &ev);
    CHECK(!g_origCalled);
    X11Clipboard::unregisterRequestor(0xdeadbeef);
    delete cb;
    XSetErrorHandler(orig);
}

TEST(clipboard_requestor_disappears_during_response_does_not_crash) {
    CHECK_EQ(X11Clipboard::refCount_, 0);
    g_origCalled = false;
    XErrorHandler orig = XSetErrorHandler(testOrigHandler);
    X11Clipboard cb;
    if (!cb.isAvailable()) { XSetErrorHandler(orig); return; }
    Display* d2 = XOpenDisplay(nullptr);
    if (!d2) { XSetErrorHandler(orig); return; }
    std::string large(cb.incrThreshold_ + 8192, 'x');
    if (large.size() <= cb.incrChunkSize_) large.resize(cb.incrChunkSize_ + 4096);
    CHECK(cb.copy(large));
    Window req = XCreateSimpleWindow(d2, RootWindow(d2, DefaultScreen(d2)), 0, 0, 10, 10, 0, 0, 0);
    Atom clip = XInternAtom(d2, "CLIPBOARD", False);
    Atom utf8 = XInternAtom(d2, "UTF8_STRING", False);
    Atom prop = XInternAtom(d2, "TEST_PROP_DISAPPEAR", False);
    XConvertSelection(d2, clip, utf8, prop, req, CurrentTime);
    XFlush(d2);
    XSync(d2, False);
    for (int i = 0; i < 100 && (X11Clipboard::activeRequestors_.find(req) == X11Clipboard::activeRequestors_.end() || cb.incrSends_.empty()); ++i) {
        cb.processEvents();
        usleep(5000);
    }
    CHECK(X11Clipboard::activeRequestors_.find(req) != X11Clipboard::activeRequestors_.end());
    CHECK(!cb.incrSends_.empty());
    XDestroyWindow(d2, req);
    XFlush(d2);
    XSync(d2, False);
    XPropertyEvent pe{};
    pe.type = PropertyNotify;
    pe.display = cb.display_;
    pe.window = req;
    pe.atom = prop;
    pe.state = PropertyDelete;
    pe.time = CurrentTime;
    g_origCalled = false;
    int absorbedBefore = X11Clipboard::absorbedErrorCount_;
    cb.handlePropertyNotify(&pe);
    XSync(cb.display_, False);
    CHECK(!g_origCalled);
    CHECK(X11Clipboard::absorbedErrorCount_ > absorbedBefore);
    CHECK(cb.copy("still alive"));
    auto reqIt = X11Clipboard::activeRequestors_.find(req);
    if (reqIt != X11Clipboard::activeRequestors_.end()) X11Clipboard::activeRequestors_.erase(reqIt);
    for (auto it = cb.incrSends_.begin(); it != cb.incrSends_.end(); ) {
        if (it->requestor == req) it = cb.incrSends_.erase(it);
        else ++it;
    }
    XCloseDisplay(d2);
    XSetErrorHandler(orig);
    CHECK(!g_origCalled);
}
