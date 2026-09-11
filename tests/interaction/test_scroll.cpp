#include "test_support.h"
#include <unistd.h>
#include <fcntl.h>

TEST(scroll_up_moves_viewport) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 20;
    ed.active().cursor.line = 25;
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, 17);
    CHECK_EQ(ed.active().cursor.line, 25);
}

TEST(scroll_down_moves_viewport) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 20;
    ed.active().cursor.line = 22;
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 23);
    CHECK_EQ(ed.active().cursor.line, 22);
}

TEST(scroll_up_clamps_at_top) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 1;
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, 0);
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, 0);
}

TEST(scroll_down_clamps_at_bottom) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 38;
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 40);
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 40);
}

TEST(scroll_does_not_move_cursor) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 50;
    ed.active().cursor.line = 50;
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 53);
    CHECK_EQ(ed.active().cursor.line, 50);
}

TEST(scroll_in_selection_does_not_extend) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 20;
    ed.active().cursor.line = 22;
    enterSeleccion(ed);
    int topBefore = ed.active().viewport.top;
    int cursorBefore = ed.active().cursor.line;
    press(ed, EventType::ScrollDown);
    CHECK(ed.active().viewport.top > topBefore);
    CHECK_EQ(ed.active().cursor.line, cursorBefore);
}

TEST(scroll_does_not_snap_back_on_render) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = 50;
    ed.active().cursor.line = 50;
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 53);
    CHECK_EQ(ed.active().cursor.line, 50);
    ed.renderFrame();
    CHECK_EQ(ed.active().viewport.top, 53);
    CHECK_EQ(ed.active().cursor.line, 50);
    press(ed, EventType::ScrollUp);
    ed.renderFrame();
    CHECK_EQ(ed.active().viewport.top, 50);
}

TEST(scroll_small_file_no_move) {
    Editor ed;
    ed.active().document.restore({"a", "b", "c"});
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 0;
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 0);
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, 0);
}

TEST(scroll_renders_new_window) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = 10;
    ed.active().cursor.line = 12;
    Renderer r;
    std::string before = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "t", false, "", ed.state_, std::nullopt);
    press(ed, EventType::ScrollDown);
    std::string after = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "t", false, "", ed.state_, std::nullopt);
    CHECK(before != after);
    CHECK(contains(after, "line 13"));
}

TEST(scroll_repeated_up_clamps_at_top) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 40;
    ed.active().cursor.line = 50;
    for (int i = 0; i < 1000; ++i) press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, 0);
    CHECK(ed.active().viewport.top >= 0);
    CHECK_EQ(ed.active().cursor.line, 50);
}

TEST(scroll_repeated_down_clamps_at_max) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 20;
    ed.active().viewport.top = 10;
    ed.active().cursor.line = 15;
    for (int i = 0; i < 1000; ++i) press(ed, EventType::ScrollDown);
    int maxTop = 100 - 20;
    CHECK_EQ(ed.active().viewport.top, maxTop);
    CHECK(ed.active().viewport.top <= maxTop);
    CHECK_EQ(ed.active().cursor.line, 15);
}

TEST(scroll_never_negative_nor_beyond_eof) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 20;
    ed.active().cursor.line = 10;
    for (int i = 0; i < 500; ++i) press(ed, EventType::ScrollUp);
    CHECK(ed.active().viewport.top >= 0);
    for (int i = 0; i < 1000; ++i) press(ed, EventType::ScrollDown);
    CHECK(ed.active().viewport.top <= 80);
    for (int i = 0; i < 500; ++i) press(ed, EventType::ScrollUp);
    CHECK(ed.active().viewport.top >= 0);
    CHECK_EQ(ed.active().cursor.line, 10);
}

TEST(scroll_navegacion) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 10;
    ed.active().cursor.line = 12;
    auto topBefore = ed.active().viewport.top;
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, topBefore + 3);
    CHECK_EQ(ed.active().cursor.line, 12);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
}

TEST(scroll_interaccion) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("l");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 10;
    enterInteraccion(ed);
    ed.active().cursor.line = 12;
    auto topBefore = ed.active().viewport.top;
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.active().viewport.top, topBefore - 3);
    CHECK_EQ(ed.active().cursor.line, 12);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Interaccion));
}

TEST(scroll_busqueda_no_rompe) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("hello");
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.top = 10;
    ed.handleEvent(insert('f'));
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
    int topBefore = ed.active().viewport.top;
    press(ed, EventType::ScrollDown);
    press(ed, EventType::ScrollUp);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Busqueda));
    CHECK_EQ(ed.active().viewport.top, topBefore);
    CHECK(ed.searchQuery_.empty());
}

TEST(scroll_filebrowser) {
    TempDir td;
    td.file("aaa.txt");
    td.file("bbb.txt");
    td.file("ccc.txt");
    CwdGuard cg;
    cg.enter(td.path);
    Editor ed;
    ed.active().viewport.height = 10;
    ed.active().document.restore({"a"});
    openFileBrowser(ed);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::FileBrowser));
    ed.fileBrowser.index_ = 0;
    ed.fileBrowser.clampScroll(ed.active().viewport.height);
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.fileBrowser.index_, 1);
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.fileBrowser.index_, 2);
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.fileBrowser.index_, 1);
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.fileBrowser.index_, 0);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::FileBrowser));
}

TEST(scroll_bufferselector) {
    Editor ed;
    ed.active().document.restore({"a"});
    newBuffer(ed);
    ed.active().document.restore({"b"});
    newBuffer(ed);
    ed.active().document.restore({"c"});
    openSelector(ed);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::BufferSelector));
    ed.bufferSelectorIndex_ = 0;
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.bufferSelectorIndex_, 1);
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.bufferSelectorIndex_, 2);
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.bufferSelectorIndex_, 1);
    press(ed, EventType::ScrollUp);
    CHECK_EQ(ed.bufferSelectorIndex_, 0);
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::BufferSelector));
}

TEST(scroll_terminal_event_to_viewport) {
    int savedStdin = dup(STDIN_FILENO);
    int pfd[2];
    if (pipe(pfd) != 0) SKIP("pipe failed");
    dup2(pfd[0], STDIN_FILENO);
    close(pfd[0]);
    std::string seq = "\x1b[<64;10;5M";
    write(pfd[1], seq.c_str(), seq.size());
    Terminal t;
    Event e;
    bool got = t.readEvent(e, 100);
    dup2(savedStdin, STDIN_FILENO);
    close(savedStdin);
    close(pfd[1]);
    CHECK(got);
    CHECK_EQ(static_cast<int>(e.type), static_cast<int>(EventType::ScrollUp));

    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = 20;
    ed.active().cursor.line = 25;
    Renderer r;
    std::string before = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "t", false, "", ed.state_, std::nullopt);
    ed.handleEvent(e);
    CHECK_EQ(ed.active().viewport.top, 17);
    CHECK_EQ(ed.active().cursor.line, 25);
    std::string after = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "t", false, "", ed.state_, std::nullopt);
    CHECK(before != after);
    CHECK(contains(after, "line 17"));

    Event e2;
    Terminal::parseMouseSgr("[<65;10;5M", e2);
    ed.handleEvent(e2);
    CHECK_EQ(ed.active().viewport.top, 20);
    ed.renderFrame();
    CHECK_EQ(ed.active().viewport.top, 20);
}

TEST(scroll_renderFrame_diff_visual) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = 10;
    ed.active().cursor.line = 12;
    ed.renderFrame();
    CHECK_EQ(ed.active().viewport.top, 10);
    Renderer r;
    std::string screenBefore = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "t", false, "", ed.state_, std::nullopt);
    CHECK(contains(screenBefore, "line 10"));
    press(ed, EventType::ScrollDown);
    CHECK_EQ(ed.active().viewport.top, 13);
    CHECK_EQ(ed.active().cursor.line, 12);
    ed.renderFrame();
    CHECK_EQ(ed.active().viewport.top, 13);
    CHECK_EQ(ed.active().cursor.line, 12);
    std::string screenAfter = r.buildScreen(ed.active().document, ed.active().cursor, ed.active().viewport, "t", false, "", ed.state_, std::nullopt);
    CHECK(screenBefore != screenAfter);
    CHECK(contains(screenAfter, "line 13"));
    CHECK(screenAfter.find("\x1b[?1049h") == std::string::npos);
    CHECK(screenAfter.find("\x1b[?1049l") == std::string::npos);
    CHECK(screenAfter.find("\x1b[?1000h") == std::string::npos);
}

TEST(scroll_renderFrame_captures_diff) {
    Editor ed;
    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) lines.push_back("line " + std::to_string(i));
    ed.active().document.restore(lines);
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 80;
    ed.active().viewport.top = 10;
    ed.active().cursor.line = 12;

    Renderer::setTestMode(true);
    ed.renderFrame();
    int savedStdout = dup(STDOUT_FILENO);
    int pfd[2];
    pipe(pfd);
    dup2(pfd[1], STDOUT_FILENO);
    Renderer::setTestMode(false);
    press(ed, EventType::ScrollDown);
    ed.renderFrame();
    Renderer::setTestMode(true);
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(pfd[1]);
    char buf[8192] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf)-1);
    close(pfd[0]);
    std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
    CHECK(!out.empty());
    CHECK(contains(out, "line 13"));
    CHECK(out.find("\x1b[?1049h") == std::string::npos);
    CHECK(ed.active().viewport.top == 13);
    CHECK(ed.active().cursor.line == 12);
}
