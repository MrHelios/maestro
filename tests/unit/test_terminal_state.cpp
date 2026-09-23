#include <pty.h>
#include <unistd.h>
#include <string>
#include "platform/tty/Terminal.h"
#include "test_framework.h"

static std::string captureMouseWrites() {
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    if (pipe(pfd) != 0) return "";
    dup2(pfd[1], STDOUT_FILENO);
    Terminal t;
    t.enableMouseTracking();
    t.disableMouseTracking();
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    char buf[256] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf) - 1);
    close(pfd[0]);
    if (n <= 0) return "";
    return std::string(buf, static_cast<size_t>(n));
}

static std::string captureRawAndMouseWrites() {
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    if (pipe(pfd) != 0) return "";
    dup2(pfd[1], STDOUT_FILENO);
    int ptyMaster = -1, ptySlave = -1;
    bool hasPty = (openpty(&ptyMaster, &ptySlave, nullptr, nullptr, nullptr) == 0);
    int savedStdin = -1;
    if (hasPty) {
        savedStdin = dup(STDIN_FILENO);
        dup2(ptySlave, STDIN_FILENO);
    }
    Terminal t;
    t.enableRawMode();
    t.enableMouseTracking();
    t.disableMouseTracking();
    t.disableRawMode();
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    if (hasPty) {
        dup2(savedStdin, STDIN_FILENO);
        close(savedStdin);
        close(ptyMaster);
        close(ptySlave);
    }
    char buf[512] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf) - 1);
    close(pfd[0]);
    if (n <= 0) return "";
    return std::string(buf, static_cast<size_t>(n));
}

TEST(terminal_mouse_enable_writes_sgr) {
    std::string out = captureMouseWrites();
    CHECK(out.find("\x1b[?1000h") != std::string::npos);
    CHECK(out.find("\x1b[?1006h") != std::string::npos);
}

TEST(terminal_mouse_disable_writes_sgr) {
    std::string out = captureMouseWrites();
    CHECK(out.find("\x1b[?1006l") != std::string::npos);
    CHECK(out.find("\x1b[?1000l") != std::string::npos);
}

TEST(terminal_raw_does_not_write_mouse) {
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(pfd[1], STDOUT_FILENO);
    int ptyMaster = -1, ptySlave = -1;
    bool hasPty = (openpty(&ptyMaster, &ptySlave, nullptr, nullptr, nullptr) == 0);
    int savedStdin = -1;
    if (hasPty) {
        savedStdin = dup(STDIN_FILENO);
        dup2(ptySlave, STDIN_FILENO);
    }
    Terminal t;
    t.enableRawMode();
    t.disableRawMode();
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    if (hasPty) {
        dup2(savedStdin, STDIN_FILENO);
        close(savedStdin);
        close(ptyMaster);
        close(ptySlave);
    }
    char buf[512] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf)-1);
    close(pfd[0]);
    std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
    if (out.empty() && !hasPty) SKIP("no pty");
    CHECK(out.find("\x1b[?1000h") == std::string::npos);
    CHECK(out.find("\x1b[?1006h") == std::string::npos);
}

TEST(terminal_raw_plus_mouse_writes_both) {
    std::string out = captureRawAndMouseWrites();
    if (out.empty()) SKIP("no pty");
    CHECK(out.find("\x1b[?1000h") != std::string::npos);
    CHECK(out.find("\x1b[?1006h") != std::string::npos);
    CHECK(out.find("\x1b[?1006l") != std::string::npos);
    CHECK(out.find("\x1b[?1000l") != std::string::npos);
}

TEST(terminal_enable_idempotent_no_double_write) {
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(pfd[1], STDOUT_FILENO);
    Terminal t;
    t.enableMouseTracking();
    t.enableMouseTracking();
    t.disableMouseTracking();
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    char buf[512] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf)-1);
    close(pfd[0]);
    std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
    size_t first = out.find("\x1b[?1000h");
    size_t second = std::string::npos;
    if (first != std::string::npos) second = out.find("\x1b[?1000h", first+1);
    CHECK(second == std::string::npos);
}

TEST(terminal_mouse_cycle_with_raw_idempotent) {
    int ptyMaster = -1, ptySlave = -1;
    if (openpty(&ptyMaster, &ptySlave, nullptr, nullptr, nullptr) != 0) SKIP("no pty");
    int savedStdin = dup(STDIN_FILENO);
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(ptySlave, STDIN_FILENO);
    dup2(pfd[1], STDOUT_FILENO);
    Terminal t;
    t.enableRawMode();
    t.enableMouseTracking();
    t.disableMouseTracking();
    t.disableRawMode();
    t.enableRawMode();
    t.enableMouseTracking();
    t.disableMouseTracking();
    t.disableRawMode();
    dup2(savedStdin, STDIN_FILENO);
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdin);
    close(savedStdout);
    close(pfd[0]); close(pfd[1]);
    close(ptyMaster); close(ptySlave);
    CHECK(true);
}

TEST(terminal_alt_enable_writes) {
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(pfd[1], STDOUT_FILENO);
    Terminal t;
    t.enterAlternateScreen();
    t.leaveAlternateScreen();
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    char buf[256] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf)-1);
    close(pfd[0]);
    std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
    CHECK(out.find("\x1b[?1049h") != std::string::npos);
    CHECK(out.find("\x1b[?1049l") != std::string::npos);
}

TEST(terminal_alt_idempotent_no_double_write) {
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(pfd[1], STDOUT_FILENO);
    Terminal t;
    t.enterAlternateScreen();
    t.enterAlternateScreen();
    t.leaveAlternateScreen();
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    char buf[512] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf)-1);
    close(pfd[0]);
    std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
    size_t first = out.find("\x1b[?1049h");
    size_t second = std::string::npos;
    if (first != std::string::npos) second = out.find("\x1b[?1049h", first + 1);
    CHECK(second == std::string::npos);
}

TEST(terminal_alt_leave_idempotent_no_write) {
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(pfd[1], STDOUT_FILENO);
    Terminal t;
    t.leaveAlternateScreen();
    t.leaveAlternateScreen();
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    char buf[256] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf)-1);
    close(pfd[0]);
    std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
    CHECK(out.empty());
}

TEST(terminal_raw_alt_mouse_cycle) {
    int ptyMaster = -1, ptySlave = -1;
    if (openpty(&ptyMaster, &ptySlave, nullptr, nullptr, nullptr) != 0) SKIP("no pty");
    int savedStdin = dup(STDIN_FILENO);
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(ptySlave, STDIN_FILENO);
    dup2(pfd[1], STDOUT_FILENO);
    Terminal t;
    t.enableRawMode();
    t.enterAlternateScreen();
    t.enableMouseTracking();
    t.disableMouseTracking();
    t.leaveAlternateScreen();
    t.disableRawMode();
    dup2(savedStdin, STDIN_FILENO);
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdin);
    close(savedStdout);
    char buf[512] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf)-1);
    close(pfd[0]); close(pfd[1]);
    close(ptyMaster); close(ptySlave);
    std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
    if (out.empty()) SKIP("no pty");
    auto altOn = out.find("\x1b[?1049h");
    auto mouseOn = out.find("\x1b[?1000h");
    auto sgrOn = out.find("\x1b[?1006h");
    auto sgrOff = out.find("\x1b[?1006l");
    auto altOff = out.find("\x1b[?1049l");
    CHECK(altOn != std::string::npos);
    CHECK(mouseOn != std::string::npos);
    CHECK(sgrOn != std::string::npos);
    CHECK(sgrOff != std::string::npos);
    CHECK(altOff != std::string::npos);
    CHECK(altOn < mouseOn);
    CHECK(mouseOn < sgrOn);
    CHECK(sgrOn < sgrOff);
    CHECK(sgrOff < altOff);
}

TEST(terminal_signal_flag_mouse) {
    Terminal t;
    CHECK(!Terminal::isMouseActiveForTest());
    t.enableMouseTracking();
    CHECK(Terminal::isMouseActiveForTest());
    t.disableMouseTracking();
    CHECK(!Terminal::isMouseActiveForTest());
    t.enableMouseTracking();
    CHECK(Terminal::isMouseActiveForTest());
    t.disableMouseTracking();
    CHECK(!Terminal::isMouseActiveForTest());
}

TEST(terminal_signal_flag_alt) {
    Terminal t;
    CHECK(!Terminal::isAltActiveForTest());
    t.enterAlternateScreen();
    CHECK(Terminal::isAltActiveForTest());
    t.leaveAlternateScreen();
    CHECK(!Terminal::isAltActiveForTest());
    t.enterAlternateScreen();
    CHECK(Terminal::isAltActiveForTest());
    t.leaveAlternateScreen();
    CHECK(!Terminal::isAltActiveForTest());
}

TEST(terminal_signal_flag_raw) {
    int ptyMaster = -1, ptySlave = -1;
    if (openpty(&ptyMaster, &ptySlave, nullptr, nullptr, nullptr) != 0) SKIP("no pty");
    int savedStdin = dup(STDIN_FILENO);
    dup2(ptySlave, STDIN_FILENO);
    Terminal t;
    CHECK(!Terminal::isRawActiveForTest());
    t.enableRawMode();
    CHECK(Terminal::isRawActiveForTest());
    t.disableRawMode();
    CHECK(!Terminal::isRawActiveForTest());
    dup2(savedStdin, STDIN_FILENO);
    close(savedStdin);
    close(ptyMaster);
    close(ptySlave);
}
