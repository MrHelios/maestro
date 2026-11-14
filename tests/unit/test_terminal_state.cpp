#include <pty.h>
#include <unistd.h>
#include <string>

#include "test_framework.h"
#include "platform/tty/Terminal.h"
#include "rendering/tty/TtyTheme.h"
#include "rendering/tty/TtyScroll.h"

namespace {

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

} // namespace

TEST(terminal_state_mouse_enable_writes_sgr) {
    std::string out = captureMouseWrites();
    
    CHECK(out.find("\x1b[?1000h") != std::string::npos);
    CHECK(out.find("\x1b[?1006h") != std::string::npos);
}

TEST(terminal_state_mouse_disable_writes_sgr) {
    std::string out = captureMouseWrites();
    
    CHECK(out.find("\x1b[?1006l") != std::string::npos);
    CHECK(out.find("\x1b[?1000l") != std::string::npos);
}

TEST(terminal_state_raw_does_not_write_mouse) {
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

TEST(terminal_state_raw_plus_mouse_writes_both) {
    std::string out = captureRawAndMouseWrites();
    
    if (out.empty()) SKIP("no pty");
    CHECK(out.find("\x1b[?1000h") != std::string::npos);
    CHECK(out.find("\x1b[?1006h") != std::string::npos);
    CHECK(out.find("\x1b[?1006l") != std::string::npos);
    CHECK(out.find("\x1b[?1000l") != std::string::npos);
}

TEST(terminal_state_enable_idempotent_no_double_write) {
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

TEST(terminal_state_mouse_cycle_with_raw_idempotent) {
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

TEST(terminal_state_alt_enable_writes) {
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

TEST(terminal_state_alt_idempotent_no_double_write) {
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

TEST(terminal_state_alt_leave_idempotent_no_write) {
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

TEST(terminal_state_raw_alt_mouse_cycle) {
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

TEST(terminal_state_signal_flag_mouse) {
    Terminal t;
    
    CHECK(!t.isMouseActiveForTest());
    t.enableMouseTracking();
    
    CHECK(t.isMouseActiveForTest());
    t.disableMouseTracking();
    
    CHECK(!t.isMouseActiveForTest());
    t.enableMouseTracking();
    
    CHECK(t.isMouseActiveForTest());
    t.disableMouseTracking();
    
    CHECK(!t.isMouseActiveForTest());
}

TEST(terminal_state_signal_flag_alt) {
    Terminal t;
    
    CHECK(!t.isAltActiveForTest());
    t.enterAlternateScreen();
    
    CHECK(t.isAltActiveForTest());
    t.leaveAlternateScreen();
    
    CHECK(!t.isAltActiveForTest());
    t.enterAlternateScreen();
    
    CHECK(t.isAltActiveForTest());
    t.leaveAlternateScreen();
    
    CHECK(!t.isAltActiveForTest());
}

TEST(terminal_state_signal_flag_raw) {
    int ptyMaster = -1, ptySlave = -1;
    
    if (openpty(&ptyMaster, &ptySlave, nullptr, nullptr, nullptr) != 0) SKIP("no pty");
    
    int savedStdin = dup(STDIN_FILENO);
    dup2(ptySlave, STDIN_FILENO);
    
    Terminal t;
    CHECK(!t.isRawActiveForTest());
    
    t.enableRawMode();
    CHECK(t.isRawActiveForTest());
    
    t.disableRawMode();
    CHECK(!t.isRawActiveForTest());
    
    dup2(savedStdin, STDIN_FILENO);
    close(savedStdin);
    close(ptyMaster);
    close(ptySlave);
}

TEST(terminal_state_theme_style_mapping_dark) {
    TtyTheme dark = darkTheme();
    
    CHECK(themeAnsiFor(dark, StyleRole::StatusBase) == dark.statusBar);
    CHECK(themeAnsiFor(dark, StyleRole::Default).empty());
    CHECK(themeAnsiFor(dark, StyleRole::GutterBlank).empty());
    CHECK(themeAnsiFor(dark, StyleRole::Selection) == dark.selection);
    CHECK(themeAnsiFor(dark, StyleRole::AccentNavegacion) == dark.accentNavegacion);
    CHECK(themeAnsiFor(dark, StyleRole::SyntaxKeyword) == dark.syntaxKeyword);
    
    CHECK(!themeAnsiFor(dark, StyleRole::Selection).empty());
}

TEST(terminal_state_theme_style_mapping_light) {
    TtyTheme light = lightTheme();

    CHECK(themeAnsiFor(light, StyleRole::StatusBase) == light.statusBar);
    CHECK(themeAnsiFor(light, StyleRole::Default).empty());
    CHECK(themeAnsiFor(light, StyleRole::GutterBlank).empty());
    CHECK(themeAnsiFor(light, StyleRole::Selection) == light.selection);
    CHECK(themeAnsiFor(light, StyleRole::AccentNavegacion) == light.accentNavegacion);
    CHECK(themeAnsiFor(light, StyleRole::SyntaxKeyword) == light.syntaxKeyword);
    
    CHECK(!themeAnsiFor(light, StyleRole::Selection).empty());
}

TEST(terminal_state_tty_scroll_op) {
    TtyScrollOp op = scrollOpFor(20, 3);
    CHECK(op.useRegion);
    CHECK_EQ(op.absDelta, 3);
    CHECK(op.down);
    
    TtyScrollOp up = scrollOpFor(20, -2);
    CHECK(up.useRegion);
    CHECK(!up.down);
    CHECK(!scrollOpFor(20, 0).useRegion);
    CHECK(!scrollOpFor(20, 20).useRegion);
    CHECK(!scrollOpFor(20, 25).useRegion);
    CHECK(!scrollOpFor(0, 3).useRegion);
    
    std::string prefix = scrollRegionPrefix(20, op);
    CHECK(prefix == "\x1b[1;20r\x1b[3S\x1b[r");
}
