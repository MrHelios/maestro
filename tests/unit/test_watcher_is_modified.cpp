#include "core/Buffer.h"
#include "test_framework.h"

static Buffer makeBuffer(const std::vector<std::string>& lines) {
    Buffer b;
    b.document.restore(lines);
    b.syncSavedState();
    return b;
}

TEST(watcher_A_modificacion_simple) {
    Buffer b = makeBuffer({"hello"});
    CHECK(!b.isModified());
    CHECK(!b.modified);
    b.document.deleteRange(0,1,0,2);
    b.document.insertText(0,1,"a");
    b.recalcModified();
    CHECK(b.isModified());
    CHECK(b.modified);
    CHECK_EQ(b.document.lineAt(0), "hallo");
}

TEST(watcher_B_modificar_y_deshacer) {
    Buffer b = makeBuffer({"hello"});
    auto e = b.beginHistoryEntry();
    b.document.deleteRange(0,1,0,2);
    e.edits.push_back({EditType::Delete,{0,1},{0,2},"e"});
    b.document.insertText(0,1,"a");
    e.edits.push_back({EditType::Insert,{0,1},{0,2},"a"});
    b.commitHistoryEntry(std::move(e));
    b.recalcModified();
    CHECK(b.isModified());
    CHECK(b.undo());
    CHECK(!b.isModified());
    CHECK(!b.modified);
    CHECK_EQ(b.document.lineAt(0), "hello");
}

TEST(watcher_C_modificar_misma_fila_varias_veces) {
    Buffer b = makeBuffer({"hello"});
    for (int i=0;i<3;i++) {
        b.document.deleteCharAt(0,1);
        b.document.insertChar(0,1,'a'+i);
    }
    b.recalcModified();
    CHECK(b.isModified());
    b.document.restore({"hello"});
    b.recalcModified();
    CHECK(!b.isModified());
    CHECK(!b.modified);
}

TEST(watcher_D_enter) {
    Buffer b = makeBuffer({"A","B","C"});
    CHECK(!b.isModified());
    b.document.splitLine(1,1);
    b.recalcModified();
    CHECK(b.isModified());
    CHECK_EQ(b.document.lineCount(), 4);
}

TEST(watcher_E_enter_undo) {
    Buffer b = makeBuffer({"A","B","C"});
    auto e = b.beginHistoryEntry();
    b.document.splitLine(1,1);
    e.edits.push_back({EditType::SplitLine,{1,1},{2,0},""});
    b.commitHistoryEntry(std::move(e));
    b.recalcModified();
    CHECK(b.isModified());
    CHECK(b.undo());
    CHECK(!b.isModified());
    CHECK_EQ(b.document.lineCount(), 3);
    CHECK_EQ(b.document.lineAt(0), "A");
    CHECK_EQ(b.document.lineAt(1), "B");
    CHECK_EQ(b.document.lineAt(2), "C");
}

TEST(watcher_F_merge_undo) {
    Buffer b = makeBuffer({"AB","CDE"});
    b.document.mergeLine(0);
    b.recalcModified();
    CHECK(b.isModified());
    CHECK_EQ(b.document.lineAt(0), "ABCDE");
    b.document.splitLine(0,2);
    b.recalcModified();
    CHECK(!b.isModified());
}

TEST(watcher_F_merge_via_backspace_undo) {
    Buffer b = makeBuffer({"AB","CDE"});
    auto e = b.beginHistoryEntry();
    b.document.deleteCharBefore(1,0);
    e.edits.push_back({EditType::MergeLine,{0,2},{1,0},""});
    b.commitHistoryEntry(std::move(e));
    b.recalcModified();
    CHECK(b.isModified());
    CHECK(b.undo());
    CHECK(!b.isModified());
    CHECK_EQ(b.document.lineCount(), 2);
}

TEST(watcher_G_varias_ops_mismas_filas_dedup) {
    Buffer b = makeBuffer({"a","b","c","d","e","f","g","h","i","j"});
    b.document.insertChar(5,1,'X');
    b.document.insertChar(5,1,'Y');
    b.document.insertChar(5,1,'Z');
    b.document.splitLine(5,2);
    b.document.insertChar(6,0,'Q');
    b.recalcModified();
    CHECK(b.isModified());
    CHECK_EQ(b.watcher_.size(), size_t(3));
    b.document.restore({"a","b","c","d","e","f","g","h","i","j"});
    b.recalcModified();
    CHECK(!b.isModified());
}

TEST(watcher_H_bloque_multilinea) {
    Buffer b = makeBuffer({"a","b","c","d"});
    auto e = b.beginHistoryEntry();
    std::vector<std::string> block = {"X","Y","Z"};
    b.document.insertBlock(1,1,block);
    e.edits.push_back({EditType::Insert,{1,1},{3,1},"X\nY\nZ"});
    b.commitHistoryEntry(std::move(e));
    b.recalcModified();
    CHECK(b.isModified());
    CHECK(b.undo());
    CHECK(!b.isModified());
    CHECK(b.document.snapshot() == (std::vector<std::string>{"a","b","c","d"}));
}

TEST(watcher_H_borrar_bloque_multilinea) {
    Buffer b = makeBuffer({"aaa","bbb","ccc","ddd"});
    b.document.deleteRange(1,1,2,2);
    b.recalcModified();
    CHECK(b.isModified());
    b.document.restore({"aaa","bbb","ccc","ddd"});
    b.recalcModified();
    CHECK(!b.isModified());
}

TEST(watcher_I_modificar_y_guardar) {
    Buffer b = makeBuffer({"original"});
    b.document.insertChar(0,8,'X');
    b.recalcModified();
    CHECK(b.isModified());
    b.syncSavedState();
    CHECK(!b.isModified());
    CHECK(b.watcher_.empty());
    CHECK(b.originalSnapshot_ == (std::vector<std::string>{"originalX"}));
    CHECK(!b.modified);
}

TEST(watcher_J_editar_despues_de_guardar) {
    Buffer b = makeBuffer({"original"});
    b.syncSavedState();
    CHECK(!b.isModified());
    b.document.insertChar(0,0,'Y');
    b.recalcModified();
    CHECK(b.isModified());
}

TEST(watcher_cantidad_filas_cambia) {
    Buffer b = makeBuffer({"A","B","C"});
    b.document.splitLine(0,1);
    b.recalcModified();
    CHECK(b.isModified());
    CHECK_EQ(b.document.lineCount(), 4);
    b.undo();
    // undo no existe porque no se hizo commit, simulamos restore
    b.document.restore({"A","B","C"});
    b.recalcModified();
    CHECK(!b.isModified());
}

TEST(watcher_save_limpia_y_nuevo_snapshot) {
    Buffer b = makeBuffer({"a","b"});
    b.document.insertChar(0,1,'X');
    b.recalcModified();
    CHECK(b.isModified());
    bool ok = b.document.saveToFile("/tmp/watcher_test_save.txt");
    CHECK(ok);
    b.syncSavedState();
    CHECK(!b.isModified());
    CHECK(b.watcher_.empty());
    b.document.insertChar(1,1,'Y');
    b.recalcModified();
    CHECK(b.isModified());
}

TEST(watcher_performance_100k_lineas_1000_edits) {
    std::vector<std::string> big(100000, "linea base");
    Buffer b = makeBuffer(big);
    for (int i=0;i<1000;i++) {
        b.document.insertChar(50000, 0, 'x');
        b.document.deleteCharAt(50000, 0);
    }
    b.document.insertChar(50000,0,'X');
    b.recalcModified();
    CHECK(b.isModified());
    CHECK(b.watcher_.size() == size_t(1));
    bool mod = b.isModified();
    CHECK(mod);
    b.document.deleteCharAt(50000,0);
    b.recalcModified();
    CHECK(!b.isModified());
}
