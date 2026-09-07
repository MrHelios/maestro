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
    std::string normalized = std::filesystem::path(path).lexically_normal().string();
    bool already = trackedFiles_.find(normalized) != trackedFiles_.end();
    trackedFiles_.insert(normalized);
    if (already && fileWatches_.find(normalized) != fileWatches_.end()) return;
    if (already) {
        // file was tracked but file watch not active (e.g. after delete) - try to re-watch
        watchFile(normalized);
        return;
    }
    watchFile(normalized);
    std::string dir = std::filesystem::path(normalized).parent_path().string();
    if (dir.empty()) dir = ".";
    watchDir(dir);
}

void InotifyFileWatcher::unwatch(const std::string& path) {
    if (path.empty() || fd_ < 0) return;
    std::string normalized = std::filesystem::path(path).lexically_normal().string();
    trackedFiles_.erase(normalized);
    unwatchFile(normalized);
    std::string dir = std::filesystem::path(normalized).parent_path().string();
    if (dir.empty()) dir = ".";
    unwatchDir(dir);
}

void InotifyFileWatcher::watchFile(const std::string& path) {
    int wd = inotify_add_watch(fd_, path.c_str(), IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd < 0) return;
    auto it = fileWatches_.find(path);
    if (it != fileWatches_.end() && it->second.first == wd) {
        // Even if kernel returns same wd, we bump gen to avoid race with IN_IGNORED of previous watch.
        // The old watch's IN_IGNORED may arrive later and must not destroy the new watch's bookkeeping.
        // We let the old wd entry be cleaned up when its IN_IGNORED arrives with the old gen.
    }
    uint64_t gen = nextGen_++;
    wdToEntry_[wd] = {path, gen, false};
    refCount_[wd] = {gen, 1};
    fileWatches_[path] = {wd, gen};
}

void InotifyFileWatcher::watchDir(const std::string& dir) {
    auto it = dirWatches_.find(dir);
    if (it != dirWatches_.end()) {
        int wd = it->second.first;
        uint64_t gen = it->second.second;
        auto rcIt = refCount_.find(wd);
        if (rcIt != refCount_.end() && rcIt->second.first == gen) {
            // Valid existing watch, just increment refcount
            rcIt->second.second++;
            return;
        }
        // Stale entry (wd invalid or gen mismatch), clean up and fall through to create new watch
        dirWatches_.erase(it);
        wdToEntry_.erase(wd);
        refCount_.erase(wd);
    }
    int wd = inotify_add_watch(fd_, dir.c_str(), IN_CREATE | IN_MOVED_TO);
    if (wd < 0) return;
    uint64_t gen = nextGen_++;
    wdToEntry_[wd] = {dir, gen, true};
    refCount_[wd] = {gen, 1};
    dirWatches_[dir] = {wd, gen};
}

void InotifyFileWatcher::unwatchFile(const std::string& path) {
    auto it = fileWatches_.find(path);
    if (it == fileWatches_.end()) return;
    int wd = it->second.first;
    uint64_t gen = it->second.second;
    auto rcIt = refCount_.find(wd);
    if (rcIt == refCount_.end() || rcIt->second.first != gen) {
        fileWatches_.erase(it);
        return;
    }
    rcIt->second.second--;
    if (rcIt->second.second > 0) {
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
    if (rcIt == refCount_.end() || rcIt->second.first != gen) {
        dirWatches_.erase(it);
        return;
    }
    rcIt->second.second--;
    if (rcIt->second.second > 0) {
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
                auto rcIt = refCount_.find(wd);
                if (eit != wdToEntry_.end() && eit->second.gen == removedGen) {
                    wdToEntry_.erase(eit);
                    if (rcIt != refCount_.end() && rcIt->second.first == removedGen) refCount_.erase(rcIt);
                } else if (rcIt != refCount_.end() && rcIt->second.first == removedGen) {
                    refCount_.erase(rcIt);
                }
            } else {
                auto eit = wdToEntry_.find(wd);
                if (eit != wdToEntry_.end()) {
                    std::string p = eit->second.path;
                    bool isDir = eit->second.isDir;
                    uint64_t gen = eit->second.gen;
                    wdToEntry_.erase(eit);
                    auto rcIt = refCount_.find(wd);
                    if (rcIt != refCount_.end() && rcIt->second.first == gen) refCount_.erase(rcIt);
                    if (isDir) {
                        auto dit = dirWatches_.find(p);
                        if (dit != dirWatches_.end() && dit->second.first == wd && dit->second.second == gen)
                            dirWatches_.erase(dit);
                    } else {
                        auto fit = fileWatches_.find(p);
                        if (fit != fileWatches_.end() && fit->second.first == wd && fit->second.second == gen)
                            fileWatches_.erase(fit);
                    }
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
