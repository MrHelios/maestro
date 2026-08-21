#pragma once
#include "clipboard/SystemClipboard.h"
#include <string>
#include <optional>

class FakeClipboard : public SystemClipboard {
public:
    bool copy(const std::string& text) override {
        if (failCopy_) return false;
        text_ = text;
        hasContent_ = true;
        s_globalText = text;
        s_globalHas = true;
        s_owner = this;
        return true;
    }
    std::optional<std::string> paste() override {
        if (failPaste_) return std::nullopt;
        if (s_owner == this) return text_;
        if (s_globalHas) return s_globalText;
        if (hasContent_) return text_;
        return std::optional<std::string>{""};
    }
    bool ownsClipboard() const override { return s_owner == this; }
    void processEvents() override {}
    void simulateExternalCopy(const std::string& text) {
        s_globalText = text;
        s_globalHas = true;
        s_owner = nullptr;
        text_.clear();
        hasContent_ = false;
    }
    void simulateLossOfOwnership() {
        if (s_owner == this) s_owner = nullptr;
        text_.clear();
        hasContent_ = false;
    }
    void setFailCopy(bool v) { failCopy_ = v; }
    void setFailPaste(bool v) { failPaste_ = v; }
    static void resetGlobal() { s_globalText.clear(); s_globalHas = false; s_owner = nullptr; }
    const std::string& storedText() const { return text_; }
private:
    std::string text_;
    bool hasContent_ = false;
    bool failCopy_ = false;
    bool failPaste_ = false;
    static inline std::string s_globalText;
    static inline bool s_globalHas = false;
    static inline FakeClipboard* s_owner = nullptr;
};
