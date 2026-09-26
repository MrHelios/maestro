#pragma once

#include <memory>

#include "filesystem/FileWatcher.h"

// Frontier (9): factory de watcher.
//
// El Editor común pide un FileWatcher sin conocer Inotify/Null.
std::unique_ptr<FileWatcher> makeFileWatcher();
std::unique_ptr<FileWatcher> makeNullFileWatcher();
