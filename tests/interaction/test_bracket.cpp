#include "test_support.h"

// Test directo de la máquina de estados del bracket jump:
TEST(bracket_jump_toggle_preserves_after_render) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1;
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 40;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.line, 0);
    CHECK_EQ(ed.bracketPair_->open.col, 0);
    CHECK_EQ(ed.bracketPair_->close.col, 2);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.handleEvent(ev(EventType::Prefix));
    CHECK(ed.getStateForTesting() == State::Prefix);
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    CHECK(ed.bracketJumpPendingPreserve_);
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.col, 0);
    CHECK_EQ(ed.bracketPair_->close.col, 2);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    CHECK(ed.bracketJumpPendingPreserve_);
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    CHECK_EQ(ed.active().cursor.col, 2);
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
}
TEST(bracket_jump_second_render_preserves_close) {
    Editor ed;
    ed.active().document.restore({"({[]})"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 2;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.col, 2);
    CHECK_EQ(ed.bracketPair_->close.col, 3);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
}
TEST(bracket_jump_manual_move_resets) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1;
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    ed.handleEvent(ev(EventType::MoveRight));
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
}
// ---------------------------------------------------------------------------
// Highlight persistente
// ---------------------------------------------------------------------------
TEST(bracket_highlight_persistent) {
    Editor ed;
    ed.active().document.restore({"{" ,"    foo();" ,"}"});
    ed.active().cursor.line = 1; ed.active().cursor.col = 4;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.line, 0);
    CHECK_EQ(ed.bracketPair_->close.line, 2);
    ed.active().cursor.col = 5;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.line, 0);
    ed.active().cursor.line = 0; ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    ed.active().cursor.line = 2; ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    ed.active().document.restore({"a b c"});
    ed.active().cursor.line = 0; ed.active().cursor.col = 1;
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());
}
// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------
TEST(bracket_viewport_open_visible_close_outside) {
    Editor ed;
    std::vector<std::string> big;
    big.push_back("{");
    for(int i=0;i<50;i++) big.push_back("  line");
    big.push_back("}");
    ed.active().document.restore(big);
    ed.active().viewport.height = 5;
    ed.active().viewport.width = 40;
    ed.active().viewport.top = 0;
    ed.active().viewport.left = 0;
    ed.active().cursor.line = 0; ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK(ed.bracketPair_->close.line > 4);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK(ed.active().cursor.line == 51);
    CHECK(ed.active().viewport.top <= 51 && 51 < ed.active().viewport.top + ed.active().viewport.height);
}
TEST(bracket_viewport_open_outside_close_visible) {
    Editor ed;
    std::vector<std::string> big;
    big.push_back("{");
    for(int i=0;i<50;i++) big.push_back("  line");
    big.push_back("}");
    ed.active().document.restore(big);
    ed.active().viewport.height = 5;
    ed.active().viewport.width = 40;
    ed.active().viewport.top = 48;
    ed.active().cursor.line = 51; ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK(ed.bracketPair_->open.line == 0);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.line, 0);
    CHECK(ed.active().viewport.top <= 0);
}
TEST(bracket_viewport_both_outside_initially) {
    Editor ed;
    std::vector<std::string> big;
    big.push_back("{");
    for(int i=0;i<100;i++) big.push_back("  x");
    big.push_back("}");
    ed.active().document.restore(big);
    ed.active().viewport.height = 5;
    ed.active().viewport.width = 40;
    ed.active().viewport.top = 40;
    ed.active().cursor.line = 40; ed.active().cursor.col = 2;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.line, 0);
}
// ---------------------------------------------------------------------------
// Selección
// ---------------------------------------------------------------------------
TEST(bracket_selection_highlight_hidden_but_jump_works) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line = 0; ed.active().cursor.col = 1;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    ed.handleEvent(insert('s'));
    CHECK(ed.getStateForTesting() == State::Seleccion);
    Renderer r; r.setTestMode(true);
    Document doc = ed.active().document;
    Cursor cur = ed.active().cursor;
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=40;
    std::string outSel = r.buildScreen(doc, cur, vp, "t.cpp", false, Message{}, State::Seleccion, std::nullopt, std::nullopt, std::nullopt);
    std::string outNav = r.buildScreen(doc, cur, vp, "t.cpp", false, Message{}, State::Navegacion, std::nullopt, std::nullopt, ed.bracketPair_);
    CHECK(outSel.find("\x1b[48;5;221m") == std::string::npos);
    CHECK(outNav.find("\x1b[48;5;221m") != std::string::npos);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 0);
    ed.handleEvent(escapeEvent());
    CHECK(ed.getStateForTesting() == State::Navegacion);
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
}
// ---------------------------------------------------------------------------
// Edición (doc.version)
// ---------------------------------------------------------------------------
TEST(bracket_edit_insert_invalidate) {
    Editor ed;
    ed.active().document.restore({"()"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    enterInteraccion(ed);
    ed.handleEvent(insert('x'));
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->close.col, 2);
}
TEST(bracket_edit_delete) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    enterInteraccion(ed);
    ed.active().cursor.col=0;
    ed.handleEvent(ev(EventType::Delete));
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());
}
TEST(bracket_edit_backspace) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line=0; ed.active().cursor.col=3;
    ed.updateBracketHighlight();
    enterInteraccion(ed);
    ed.active().cursor.col=3;
    ed.handleEvent(ev(EventType::Backspace));
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());
}
TEST(bracket_edit_enter) {
    Editor ed;
    ed.active().document.restore({"{}"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    enterInteraccion(ed);
    ed.handleEvent(ev(EventType::InsertNewline));
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK(ed.bracketPair_->open.line==0 && ed.bracketPair_->close.line==1);
}
// ---------------------------------------------------------------------------
// Unicode / tabs
// ---------------------------------------------------------------------------
TEST(bracket_unicode_before) {
    Editor ed;
    ed.active().document.restore({"é[ ]"});
    // 'é' = 2 bytes (C3 A9), '[' en byte 2, ']' en byte 4: col es byte column (Maestro usa byte column, no visual column)
    const std::string& line = ed.active().document.lineAt(0);
    CHECK_EQ((int)line.find('['), 2);
    CHECK_EQ((int)line.find(']'), 4);
    ed.active().cursor.line=0; ed.active().cursor.col=4; // sobre ']' (byte 4) dentro de "é[ ]"
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.col, 2); // byte column
    Renderer r; r.setTestMode(true);
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=40;
    Cursor cur = ed.active().cursor;
    std::string out = r.buildScreen(ed.active().document, cur, vp, "t.cpp", false, Message{}, State::Navegacion, std::nullopt, std::nullopt, ed.bracketPair_);
    CHECK(out.find("\x1b[48;5;221m") != std::string::npos);
}
TEST(bracket_tab_before) {
    Editor ed;
    ed.active().document.restore({"\t{", "\tfoo();", "\t}"});
    // '\t' = 1 byte, visual 4: '{' en byte 1, cursor en byte 1 (byte column)
    ed.active().cursor.line=1; ed.active().cursor.col=1;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.line, 0);
    CHECK_EQ(ed.bracketPair_->close.line, 2);
    Renderer r; r.setTestMode(true);
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=40;
    Cursor cur = ed.active().cursor;
    std::string out = r.buildScreen(ed.active().document, cur, vp, "t.cpp", false, Message{}, State::Navegacion, std::nullopt, std::nullopt, ed.bracketPair_);
    CHECK(out.find("\x1b[48;5;221m") != std::string::npos);
}
