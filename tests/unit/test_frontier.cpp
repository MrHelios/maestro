#include <string>

#include "test_framework.h"
#include "rendering/Sink.h"
#include "platform/clipboard/ClipboardFactory.h"
#include "filesystem/FileWatcherFactory.h"

namespace {
// Sink capturador: retiene lo escrito para assertar el primer frame
struct CaptureSink : public Sink {
    std::string data;
    bool writeStdout(const std::string& s) override {
        data += s;
        return true;
    }
};
} // namespace

// Clipboard y FileWatcher ??
// factories devuelven interfaces sin conocer concretos.
TEST(frontier_factories_return_interfaces) {
    auto cb = makeSystemClipboard();
    
    CHECK(cb != nullptr);
    CHECK(cb->copy("hola"));
    
    auto pasted = cb->paste();
    CHECK(pasted.has_value());
    
    auto nullCb = makeNullClipboard();
    CHECK(nullCb != nullptr);
    
    auto w = makeFileWatcher();
    CHECK(w != nullptr);
    
    auto nullW = makeNullFileWatcher();
    CHECK(nullW != nullptr);
}


