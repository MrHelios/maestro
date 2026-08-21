#pragma once
#include "clipboard/SystemClipboard.h"
#include <string>
#include <optional>
#include <vector>

struct _XDisplay;
using Display = struct _XDisplay;

class X11Clipboard : public SystemClipboard {
public:
    X11Clipboard();
    ~X11Clipboard() override;
    X11Clipboard(const X11Clipboard&) = delete;
    X11Clipboard& operator=(const X11Clipboard&) = delete;

    bool copy(const std::string& text) override;
    std::optional<std::string> paste() override;
    bool ownsClipboard() const override;
    void processEvents() override;
    int fd() const override;
    bool hasPending() const override { return !incrSends_.empty(); }
    bool isAvailable() const { return display_ != nullptr; }
private:
    void handleSelectionRequest(void* ev);
    std::optional<std::string> readProperty(unsigned long win, unsigned long prop);
    void deleteProperty(unsigned long win, unsigned long prop);
    std::optional<std::string> fetchProperty(unsigned long win, unsigned long prop);
    std::optional<std::string> readIncrProperty(unsigned long win, unsigned long prop);
    bool waitForSelectionNotify(unsigned long target, unsigned long property, int timeoutMs);
    void handlePropertyNotify(void* ev);
    static constexpr size_t INCR_CHUNK_SIZE = 4096;
    static constexpr size_t INCR_THRESHOLD = 65536;
    struct IncrSend {
        unsigned long requestor = 0;
        unsigned long property = 0;
        unsigned long target = 0;
        std::string data;
        size_t offset = 0;
    };
    std::vector<IncrSend> incrSends_;
    Display* display_ = nullptr;
    unsigned long window_ = 0;
    unsigned long clipboardAtom_ = 0;
    unsigned long utf8Atom_ = 0;
    unsigned long textAtom_ = 0;
    unsigned long stringAtom_ = 0;
    unsigned long targetsAtom_ = 0;
    unsigned long incrAtom_ = 0;
    unsigned long propertyAtom_ = 0;
    std::string ownedText_;
    bool ownsClipboard_ = false;
};
