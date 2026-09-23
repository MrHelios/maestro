#pragma once
#include "platform/clipboard/SystemClipboard.h"
#include <string>
#include <optional>

// Fallback de producción cuando no hay X11 disponible.
// Guarda el texto en memoria dentro del proceso.
class NullClipboard : public SystemClipboard {
public:
    bool copy(const std::string& text) override {
        text_ = text;
        hasContent_ = true;
        return true;
    }
    std::optional<std::string> paste() override {
        if (hasContent_) return text_;
        return std::optional<std::string>{""};
    }
    bool ownsClipboard() const override { return hasContent_; }
    void processEvents() override {}
private:
    std::string text_;
    bool hasContent_ = false;
};
