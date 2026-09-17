#include "test_support.h"

// Test directo de la máquina de estados del bracket jump:
// Después de Ctrl+K m -> open, Ctrl+K m -> close, el cursor queda sobre close
// y el siguiente render (refreshBracketAfterJump) debe NO resetear el toggle.
TEST(bracket_jump_toggle_preserves_after_render) {
    Editor ed;
    // Documento simple con paréntesis
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1; // sobre 'a' dentro de (a) -> enclosing (0,0)-(0,2)
    ed.active().viewport.height = 10;
    ed.active().viewport.width = 40;

    // Simula primer render: calcula bracketPair y next=Open
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.line, 0);
    CHECK_EQ(ed.bracketPair_->open.col, 0);
    CHECK_EQ(ed.bracketPair_->close.col, 2);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);

    // Primer Ctrl+K m -> debe ir a open
    ed.handleEvent(ev(EventType::Prefix));
    CHECK(ed.getStateForTesting() == State::Prefix);
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 0); // open
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    CHECK(ed.bracketJumpPendingPreserve_);
    // Simula siguiente render: debe preservar Close
    // renderFrame haría: if pending -> refreshBracketAfterJump()
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false; // renderFrame lo limpia
    // Tras refresh sobre close, el par sigue siendo el mismo y next debe seguir Close
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.col, 0);
    CHECK_EQ(ed.bracketPair_->close.col, 2);

    // Segundo Ctrl+K m -> debe ir a close
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 2); // close
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    CHECK(ed.bracketJumpPendingPreserve_);
    // Siguiente render sobre close debe NO resetear (mantener Open)
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    CHECK_EQ(ed.active().cursor.col, 2);
    // Verificación clave: toggle no fue reseteado a Open por error (ya es Open) pero la invariante es que no se resetea arbitrariamente
    // Para distinguir, hacemos un update normal sin pending: debe seguir Open porque cursor no se movió
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
}

TEST(bracket_jump_second_render_preserves_close) {
    Editor ed;
    ed.active().document.restore({"({[]})"});
    // pos dentro de [] -> innermost [2,3]
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 2; // sobre '['
    ed.updateBracketHighlight();
    CHECK(ed.bracketPair_.has_value());
    CHECK_EQ(ed.bracketPair_->open.col, 2);
    CHECK_EQ(ed.bracketPair_->close.col, 3);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);

    // 1) open
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 2);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    // render
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);

    // 2) close
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m'));
    CHECK_EQ(ed.active().cursor.col, 3);
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    // Este es el punto crítico del bug: cursor exactamente sobre close, siguiente render no debe resetear
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    // Si hubiera bug que resetea a Open, este test pasaría igual porque ya es Open.
    // Por eso verificamos el caso intermedio: tras primer salto, el siguiente render preserva Close
    // Ya verificado arriba, pero repetimos con cursor sobre close y un update normal que no debe cambiar si no hay movimiento
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
}

// Test adicional: movimiento manual resetea a Open
TEST(bracket_jump_manual_move_resets) {
    Editor ed;
    ed.active().document.restore({"(a)"});
    ed.active().cursor.line = 0;
    ed.active().cursor.col = 1;
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
    ed.handleEvent(ev(EventType::Prefix));
    ed.handleEvent(insert('m')); // -> open
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Close);
    ed.refreshBracketAfterJump();
    ed.bracketJumpPendingPreserve_ = false;
    // movimiento manual
    ed.handleEvent(ev(EventType::MoveRight)); // se mueve, next debe resetear a Open en siguiente update
    ed.updateBracketHighlight();
    CHECK(ed.nextBracketJump_ == Editor::BracketJumpTarget::Open);
}
