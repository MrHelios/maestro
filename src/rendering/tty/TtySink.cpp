#include "rendering/tty/TtySink.h"

#include <cerrno>
#include <unistd.h>

bool TtySink::writeAll(int fd, const std::string& s) {
    const char* p = s.c_str();
    std::size_t remaining = s.size();
    while (remaining > 0) {
        ssize_t n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        p += static_cast<std::size_t>(n);
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

bool TtySink::writeStdout(const std::string& s) {
    return writeAll(STDOUT_FILENO, s);
}
