#pragma once
#include "filesystem/FileWatcher.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>

class InotifyFileWatcher : public FileWatcher {
public:
    InotifyFileWatcher();
    ~InotifyFileWatcher() override;

    void watch(const std::string& path) override;
    void unwatch(const std::string& path) override;
    int fd() const override { return fd_; }
    void pollEvents(const std::function<void(const FileChangeEvent&)>& cb) override;

private:
    struct Entry {
        std::string path;
        uint64_t gen = 0;
        bool isDir = false;
    };

    void watchFile(const std::string& path);
    void watchDir(const std::string& dir);
    void unwatchFile(const std::string& path);
    void unwatchDir(const std::string& dir);

    int fd_ = -1;
    uint64_t nextGen_ = 1;
    std::unordered_map<int, Entry> wdToEntry_;
    std::unordered_map<int, int> refCount_;
    std::unordered_map<std::string, std::pair<int, uint64_t>> fileWatches_;
    std::unordered_map<std::string, std::pair<int, uint64_t>> dirWatches_;
    std::unordered_map<int, std::queue<uint64_t>> pending_;
    std::unordered_set<std::string> trackedFiles_;
};
