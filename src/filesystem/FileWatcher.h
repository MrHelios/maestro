#pragma once
#include <functional>
#include <string>

enum class FileChangeKind { Modified, Created, Deleted };

struct FileChangeEvent {
    std::string path;
    FileChangeKind kind;
};

class FileWatcher {
public:
    virtual ~FileWatcher() = default;
    virtual void watch(const std::string& path) = 0;
    virtual void unwatch(const std::string& path) = 0;
    // POSIX transport leak — tolerated during migration (ver
    // docs/architecture.md §2): este fd existe solo para integrar el
    // watcher al ppoll del loop TTY (TtyRunLoop). El camino neutral es
    // pollEvents(); un futuro loop GUI no debe depender de este fd.
    virtual int fd() const = 0;
    virtual void pollEvents(const std::function<void(const FileChangeEvent&)>& cb) = 0;
};
