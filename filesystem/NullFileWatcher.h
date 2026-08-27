#pragma once
#include "filesystem/FileWatcher.h"

class NullFileWatcher : public FileWatcher {
public:
    void watch(const std::string&) override {}
    void unwatch(const std::string&) override {}
    int fd() const override { return -1; }
    void pollEvents(const std::function<void(const FileChangeEvent&)>&) override {}
};
