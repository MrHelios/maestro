#include "filesystem/InotifyFileWatcher.h"
#include <cerrno>
#include <filesystem>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

InotifyFileWatcher::InotifyFileWatcher() {
    fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
}

InotifyFileWatcher::~InotifyFileWatcher() {
    if (fd_ >= 0) close(fd_);
}

void InotifyFileWatcher::watch(const std::string& path) {
    if (path.empty() || fd_ < 0) return;
    bool already = trackedFiles_.find(path) != trackedFiles_.end();
    trackedFiles_.insert(path);
    if (already && fileWatches_.find(path) != fileWatches_.end()) return;
    if (already) {
        // file was tracked but file watch not active (e.g. after delete) - try to re-watch
        watchFile(path);
        return;
    }
    watchFile(path);
    std::string dir = std::filesystem::path(path).parent_path().string();
    if (dir.empty()) dir = ".";
    watchDir(dir);
}

void InotifyFileWatcher::unwatch(const std::string& path) {
    if (path.empty() || fd_ < 0) return;
    trackedFiles_.erase(path);
    unwatchFile(path);
    std::string dir = std::filesystem::path(path).parent_path().string();
    if (dir.empty()) dir = ".";
    unwatchDir(dir);
}

void InotifyFileWatcher::watchFile(const std::string& path) {
    auto it = fileWatches_.find(path);
    if (it != fileWatches_.end()) {
        return;
    }
    int wd = inotify_add_watch(fd_, path.c_str(), IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd < 0) return;
    uint64_t gen = nextGen_++;
    wdToEntry_[wd] = {path, gen, false};
    refCount_[wd] = 1;
    fileWatches_[path] = {wd, gen};
}

void InotifyFileWatcher::watchDir(const std::string& dir) {
    auto it = dirWatches_.find(dir);
    if (it != dirWatches_.end()) {
        refCount_[it->second.first]++;
        return;
    }
    int wd = inotify_add_watch(fd_, dir.c_str(), IN_CREATE | IN_MOVED_TO);
    if (wd < 0) return;
    uint64_t gen = nextGen_++;
    wdToEntry_[wd] = {dir, gen, true};
    refCount_[wd] = 1;
    dirWatches_[dir] = {wd, gen};
}

void InotifyFileWatcher::unwatchFile(const std::string& path) {
    auto it = fileWatches_.find(path);
    if (it == fileWatches_.end()) return;
    int wd = it->second.first;
    uint64_t gen = it->second.second;
    auto rcIt = refCount_.find(wd);
    if (rcIt == refCount_.end()) {
        fileWatches_.erase(it);
        return;
    }
    rcIt->second--;
    if (rcIt->second > 0) {
        fileWatches_.erase(it);
        return;
    }
    if (inotify_rm_watch(fd_, wd) < 0 && errno != EINVAL) {}
    pending_[wd].push(gen);
    fileWatches_.erase(it);
}

void InotifyFileWatcher::unwatchDir(const std::string& dir) {
    auto it = dirWatches_.find(dir);
    if (it == dirWatches_.end()) return;
    int wd = it->second.first;
    uint64_t gen = it->second.second;
    auto rcIt = refCount_.find(wd);
    if (rcIt == refCount_.end()) {
        dirWatches_.erase(it);
        return;
    }
    rcIt->second--;
    if (rcIt->second > 0) {
        dirWatches_.erase(it);
        return;
    }
    if (inotify_rm_watch(fd_, wd) < 0 && errno != EINVAL) {}
    pending_[wd].push(gen);
    dirWatches_.erase(it);
}

void InotifyFileWatcher::pollEvents(const std::function<void(const FileChangeEvent&)>& cb) {
    if (fd_ < 0) return;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(fd_, buf, sizeof(buf));
    if (len <= 0) return;
    for (char* ptr = buf; ptr < buf + len; ) {
        const struct inotify_event* ev = reinterpret_cast<const struct inotify_event*>(ptr);
        uint32_t mask = ev->mask;
        int wd = ev->wd;
        std::string name = ev->len > 0 ? std::string(ev->name) : "";

        if (mask & IN_IGNORED) {
            auto pit = pending_.find(wd);
            if (pit != pending_.end() && !pit->second.empty()) {
                uint64_t removedGen = pit->second.front();
                pit->second.pop();
                if (pit->second.empty()) pending_.erase(pit);
                auto eit = wdToEntry_.find(wd);
                if (eit != wdToEntry_.end() && eit->second.gen == removedGen) {
                    wdToEntry_.erase(eit);
                    refCount_.erase(wd);
                }
            } else {
                auto eit = wdToEntry_.find(wd);
                if (eit != wdToEntry_.end()) {
                    std::string p = eit->second.path;
                    bool isDir = eit->second.isDir;
                    wdToEntry_.erase(eit);
                    refCount_.erase(wd);
                    if (isDir) dirWatches_.erase(p);
                    else fileWatches_.erase(p);
                }
            }
            ptr += sizeof(struct inotify_event) + ev->len;
            continue;
        }

        auto eit = wdToEntry_.find(wd);
        if (eit == wdToEntry_.end()) {
            ptr += sizeof(struct inotify_event) + ev->len;
            continue;
        }

        if (!eit->second.isDir) {
            std::string path = eit->second.path;
            if (mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                cb({path, FileChangeKind::Deleted});
            } else if (mask & (IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE)) {
                cb({path, FileChangeKind::Modified});
            }
        } else {
            if (mask & (IN_CREATE | IN_MOVED_TO)) {
                std::string dir = eit->second.path;
                std::string full = dir == "." ? name : dir + "/" + name;
                full = std::filesystem::path(full).lexically_normal().string();
                if (trackedFiles_.find(full) != trackedFiles_.end()) {
                    cb({full, FileChangeKind::Created});
                    // try to re-establish file watch for recreated file
                    watchFile(full);
                }
            }
        }

        ptr += sizeof(struct inotify_event) + ev->len;
    }
}
