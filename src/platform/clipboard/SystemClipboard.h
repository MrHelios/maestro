#pragma once
#include <string>
#include <optional>

class SystemClipboard {
public:
    virtual ~SystemClipboard() = default;
    virtual bool copy(const std::string& text) = 0;
    virtual std::optional<std::string> paste() = 0;
    virtual bool ownsClipboard() const = 0;
    virtual void processEvents() = 0;
    virtual int fd() const { return -1; }
    virtual bool hasPending() const { return false; }
};
