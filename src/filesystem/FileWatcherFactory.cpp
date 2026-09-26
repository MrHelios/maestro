#include "filesystem/FileWatcherFactory.h"

#include "filesystem/InotifyFileWatcher.h"
#include "filesystem/NullFileWatcher.h"

std::unique_ptr<FileWatcher> makeNullFileWatcher() {
    return std::make_unique<NullFileWatcher>();
}

std::unique_ptr<FileWatcher> makeFileWatcher() {
    return std::make_unique<InotifyFileWatcher>();
}
