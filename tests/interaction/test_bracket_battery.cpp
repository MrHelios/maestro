#include "test_support.h"

// ---------------------------------------------------------------------------
// Matcher
// ---------------------------------------------------------------------------
TEST(bracket_matcher_single) {
    Document doc;
    doc.restore({"{}"});
    CHECK(findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp).has_value());
    CHECK(findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"[]"});
    CHECK(findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"()"});
    CHECK(findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp).has_value());
}

TEST(bracket_matcher_nested) {
    Document doc;
    doc.restore({"{[]}"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value());
    CHECK_EQ(p->open.col, 0); CHECK_EQ(p->close.col, 3);
    doc.restore({"([{}])"});
    auto p2 = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p2.has_value());
    CHECK_EQ(p2->open.col, 0); CHECK_EQ(p2->close.col, 5);
}

TEST(bracket_matcher_cursor_on_opening) {
    Document doc; doc.restore({"(a)"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.col, 0);
}
TEST(bracket_matcher_cursor_on_closing) {
    Document doc; doc.restore({"(a)"});
    auto p = findMatchingBracket(doc, {0,2}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->close.col, 2);
}
TEST(bracket_matcher_cursor_inside) {
    Document doc; doc.restore({"(a)"}); // open 0, close 2, inside 1
    auto p = findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.col, 0);
    doc.restore({"[x]"}); CHECK(findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"{x}"}); CHECK(findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_matcher_nested_inside) {
    Document doc; doc.restore({"({[]})"});
    // innermost [2,3] when cursor inside
    auto p = findMatchingBracket(doc, {0,2}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.col, 2);
    // cursor at 0 '(' -> outer
    auto p2 = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK_EQ(p2->open.col, 0);
}
TEST(bracket_matcher_multiline) {
    Document doc; doc.restore({"{" ,"  foo();" ,"}"});
    auto p = findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.line, 0); CHECK_EQ(p->close.line, 2);
    auto p2 = findMatchingBracket(doc, {1,3}, SyntaxLanguage::Cpp);
    CHECK(p2.has_value()); CHECK_EQ(p2->open.line, 0);
}
TEST(bracket_matcher_multiline_nested) {
    Document doc; doc.restore({"{" ,"  [" ,"    (" ,"    )" ,"  ]" ,"}"});
    auto p = findMatchingBracket(doc, {2,4}, SyntaxLanguage::Cpp);
    CHECK(p.has_value()); CHECK_EQ(p->open.line, 2); // '('
    auto p2 = findMatchingBracket(doc, {1,2}, SyntaxLanguage::Cpp);
    CHECK(p2.has_value()); CHECK_EQ(p2->open.line, 1);
}
TEST(bracket_matcher_mismatch) {
    Document doc; doc.restore({"{[}]"});
    CHECK(!findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"([)]"});
    CHECK(!findMatchingBracket(doc, {0,0}, SyntaxLanguage::Cpp).has_value());
    doc.restore({"{[}]"});
    // enclosing inside should also be null strict
    CHECK(!findMatchingBracket(doc, {0,1}, SyntaxLanguage::Cpp).has_value());
}

// ---------------------------------------------------------------------------
// Highlight persistente
// ---------------------------------------------------------------------------
TEST(bracket_highlight_persistent) {
    Editor ed;
    ed.active().document.restore({"{" ,"    foo();" ,"}"});
    ed.active().cursor.line = 1; ed.active().cursor.col = 4; // dentro de foo()
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.line, 0);
    CHECK_EQ(ed.bracketPair_->close.line, 2);
    // moverse dentro del rango sigue mostrando mismo par
    ed.active().cursor.col = 5;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.line, 0);
    // sobre opening
    ed.active().cursor.line = 0; ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    // sobre closing
    ed.active().cursor.line = 2; ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    // salir del rango
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
    // close fuera de viewport (line 51)
    CHECK(ed.bracketPair_->close.line > 4);
    // Ctrl+K m debe hacer scroll
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m')); // -> open (ya está)
    CHECK_EQ(ed.active().cursor.line, 0);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m')); // -> close
    // Simula render que hace centerViewportOnCursor / scroll
    // refresh ya hizo center, check viewport contiene close
    CHECK(ed.active().cursor.line == 51);
    // viewport debe haberse movido para incluir close
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
    ed.active().viewport.top = 48; // cerca del final, open fuera arriba
    ed.active().cursor.line = 51; ed.active().cursor.col = 0;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK(ed.bracketPair_->open.line == 0);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m')); // -> open
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
    ed.active().viewport.top = 40; // medio, ambos fuera
    ed.active().cursor.line = 40; ed.active().cursor.col = 2; // dentro del rango
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    // Ctrl+K m -> open
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
    // entrar en selección
    ed.handleEvent(insert('s')); // s -> seleccion
    CHECK(ed.getStateForTesting() == State::Seleccion);
    // En selección, highlight no debe renderizarse: buildScreen con Seleccion y bracketPair no debe contener bracket color
    Renderer r; r.setTestMode(true);
    Document doc = ed.active().document;
    Cursor cur = ed.active().cursor;
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=40;
    // Editor en Seleccion no pasa bracketPair al Renderer (toRender = nullopt)
    std::string outSel = r.buildScreen(doc, cur, vp, "t.cpp", false, Message{}, State::Seleccion, std::nullopt, std::nullopt, std::nullopt);
    std::string outNav = r.buildScreen(doc, cur, vp, "t.cpp", false, Message{}, State::Navegacion, std::nullopt, std::nullopt, ed.bracketPair_);
    CHECK(outSel.find("\x1b[48;5;221m") == std::string::npos);
    CHECK(outNav.find("\x1b[48;5;221m") != std::string::npos);
    // Ctrl+K m debe funcionar en selección
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 0); // open
    // salir
    ed.handleEvent(escapeEvent());
    CHECK(ed.getStateForTesting() == State::Navegacion);
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
}

// ---------------------------------------------------------------------------
// Toggle
// ---------------------------------------------------------------------------
TEST(bracket_toggle_three_jumps) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.handleEvent(ev(EventType::Prefix)); ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 0);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    ed.refreshBracketAfterJump(); ed.bracketJumpPendingPreserve_=false;
    ed.handleEvent(ev(EventType::Prefix)); ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.refreshBracketAfterJump(); ed.bracketJumpPendingPreserve_=false;
    ed.handleEvent(ev(EventType::Prefix)); ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 0);
}
TEST(bracket_toggle_manual_reset) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    ed.updateBracketHighlight();
    ed.handleEvent(ev(EventType::Prefix)); ed.handleEvent(insert('m')); // -> open, next=Close
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    ed.refreshBracketAfterJump(); ed.bracketJumpPendingPreserve_=false;
    ed.handleEvent(ev(EventType::MoveRight)); // manual
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.handleEvent(ev(EventType::Prefix)); ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 0); // debe ir a open de nuevo
}

// ---------------------------------------------------------------------------
// Edición (doc.version)
// ---------------------------------------------------------------------------
TEST(bracket_edit_insert_invalidate) {
    Editor ed;
    ed.active().document.restore({"()"});
    ed.active().cursor.line=0; ed.active().cursor.col=1; // dentro, highlight () 
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    // insertar carácter dentro rompe? "()": insert 'x' entre -> "(x)" sigue válido pero posiciones cambian
    enterInteraccion(ed);
    ed.handleEvent(insert('x'));
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->close.col, 2); // "(x)" close desplazado de 1 a 2
}
TEST(bracket_edit_delete) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line=0; ed.active().cursor.col=1;
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    enterInteraccion(ed);
    // borrar '('
    ed.active().cursor.col=0;
    ed.handleEvent(ev(EventType::Delete));
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());
}
TEST(bracket_edit_backspace) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line=0; ed.active().cursor.col=3; // después de )
    ed.updateBracketHighlight();
    // backspace debe borrar )?
    enterInteraccion(ed);
    ed.active().cursor.col=3;
    ed.handleEvent(ev(EventType::Backspace));
    ed.updateBracketHighlight();
    CHECK(!ed.bracketPair_.has_value());
}
TEST(bracket_edit_enter) {
    Editor ed;
    ed.active().document.restore({"{}"});
    ed.active().cursor.line=0; ed.active().cursor.col=1; // entre { y }
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
    // é = 2 bytes, '[' en byte 2
    ed.active().cursor.line=0; ed.active().cursor.col=4; // sobre ' ' dentro
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.col, 2);
    // render no debe cortar: col visual
    Renderer r; r.setTestMode(true);
    Viewport vp; vp.top=0; vp.left=0; vp.height=5; vp.width=40;
    Cursor cur = ed.active().cursor;
    std::string out = r.buildScreen(ed.active().document, cur, vp, "t.cpp", false, Message{}, State::Navegacion, std::nullopt, std::nullopt, ed.bracketPair_);
    CHECK(out.find("\x1b[48;5;221m") != std::string::npos);
}
TEST(bracket_tab_before) {
    Editor ed;
    ed.active().document.restore({"\t{", "\tfoo();", "\t}"});
    ed.active().cursor.line=1; ed.active().cursor.col=1; // dentro
    // tab = byte 0, '{' en byte 1
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

// ---------------------------------------------------------------------------
// Strings / comentarios ignorados (ya cubierto pero se reitera)
// ---------------------------------------------------------------------------
TEST(bracket_ignore_string) {
    Document doc; doc.restore({"std::string s = \"{[()]}\";"});
    CHECK(!findMatchingBracket(doc, {0,19}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_ignore_char) {
    Document doc; doc.restore({"char c = '}';"});
    CHECK(!findMatchingBracket(doc, {0,11}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_ignore_line_comment) {
    Document doc; doc.restore({"// ( [ ] )"});
    CHECK(!findMatchingBracket(doc, {0,3}, SyntaxLanguage::Cpp).has_value());
}
TEST(bracket_ignore_block_comment) {
    Document doc; doc.restore({"/* { */", "int x = (1);"});
    CHECK(!findMatchingBracket(doc, {0,3}, SyntaxLanguage::Cpp).has_value());
    CHECK(findMatchingBracket(doc, {1,8}, SyntaxLanguage::Cpp).has_value());
}
