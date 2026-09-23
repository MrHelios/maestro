#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>
#include "test_framework.h"
#define private public
#include "app/Editor.h"
#undef private
#include "base/utf8.h"
#include "helpers/test_render_utils.h"

using testfw::TempFile;
inline Event insert(char c){ Event e; e.type=EventType::InsertChar; e.text=std::string(1,c); return e; }
inline Event ev(EventType t){ Event e; e.type=t; return e; }
inline Event escapeEvent(){ Event e; e.type=EventType::Escape; return e; }
inline Event insertBytes(const std::string& text){ Event e; e.type=EventType::InsertChar; e.text=text; return e; }
// Devuelve la longitud esperada de una secuencia UTF-8 a partir del byte
// inicial. No valida la secuencia ni rechaza bytes iniciales inválidos (ej. 0xFF→1).
inline int utf8Len(unsigned char b){
    if((b&0xE0)==0xC0) return 2;
    if((b&0xF0)==0xE0) return 3;
    if((b&0xF8)==0xF0) return 4;
    return 1;
}
inline void press(Editor& ed, EventType type){ Event e; e.type=type; ed.handleEvent(e); }
inline void pressEvent(Editor& ed, const Event& e){ ed.handleEvent(e); }
// Precondición: estado Navegacion o Interaccion/Seleccion. No maneja modales (Prefix/SaveAs/Busqueda/etc.).
inline void enterInteraccion(Editor& ed){ if(ed.state_!=State::Interaccion){ if(ed.state_==State::Seleccion){ Event esc; esc.type=EventType::Escape; ed.handleEvent(esc); } ed.handleEvent(insert('i')); } }
// Precondición: estado Navegacion o Interaccion. No maneja modales (Prefix/SaveAs/Busqueda/etc.).
inline void enterSeleccion(Editor& ed){ if(ed.state_!=State::Seleccion){ if(ed.state_==State::Interaccion){ ed.handleEvent(escapeEvent()); } ed.handleEvent(insert('s')); } }
inline void type(Editor& ed, const std::string& s){
    if(s.empty()) return;
    CHECK(utf8::isValid(s));
    enterInteraccion(ed);
    for(size_t i=0;i<s.size();){
        int len=utf8Len(static_cast<unsigned char>(s[i]));
        CHECK(i + static_cast<size_t>(len) <= s.size());
        ed.handleEvent(insertBytes(s.substr(i, static_cast<size_t>(len))));
        i += static_cast<size_t>(len);
    }
}
// typeBytes(): escribe byte por byte; usado por tests de búsqueda, que modelan entrada byte-oriented del prompt.
inline void typeBytes(Editor& ed, const std::string& s){ for(unsigned char c: s) ed.handleEvent(insert(c)); }
// typePrompt(): alias semántico de typeBytes para prompt modal SaveAs.
inline void typePrompt(Editor& ed, const std::string& s){ typeBytes(ed, s); }
inline void clearPrompt(Editor& ed){ while(!ed.saveAsPath_.empty()) press(ed, EventType::Backspace); }
// save(): Ctrl+K s (InsertChar 's') → guarda directo si tiene nombre, si no abre SaveAs.
inline void save(Editor& ed){ press(ed, EventType::Prefix); Event e; e.type=EventType::InsertChar; e.text="s"; ed.handleEvent(e); }
inline void newBuffer(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('n')); }
inline void openSelector(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('t')); }
inline void closeBuffer(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('w')); }
inline void openFileBrowser(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('o')); }
inline void previousBuffer(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('b')); }
inline void safeQuit(Editor& ed){ press(ed, EventType::Prefix); pressEvent(ed, insert('q')); }
inline void forcedQuit(Editor& ed){ press(ed, EventType::Prefix); press(ed, EventType::Quit); }
// openSaveAs(): Ctrl+K Ctrl+S (EventType::Save) → siempre abre prompt SaveAs, incluso con nombre.
inline void openSaveAs(Editor& ed){ press(ed, EventType::Prefix); press(ed, EventType::Save); }
inline void prefix(Editor& ed, EventType a, EventType b){ press(ed,a); press(ed,b); }
inline void selectFirstChars(Editor& ed, int n){ press(ed, EventType::MoveHome); enterSeleccion(ed); for(int i=0;i<n;++i) press(ed, EventType::MoveRight); }
inline void selectPress(Editor& ed, EventType t){ enterSeleccion(ed); press(ed,t); }
inline void copySelection(Editor& ed){ ed.handleEvent(insert('c')); }
inline bool contains(const std::string& hay, const std::string& needle){ return hay.find(needle)!=std::string::npos; }
inline bool validUtf8(const std::string& s){ return utf8::isValid(s); }
inline std::string fileContent(const std::string& p){ std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
struct TempDir{
    std::string path;
    TempDir(){ char tmpl[]="/tmp/edit_test_XXXXXX"; char* p=::mkdtemp(tmpl); CHECK(p != nullptr); path=p; }
    ~TempDir(){ if(!path.empty()){ std::error_code ec; std::filesystem::remove_all(path, ec); } }
    TempDir(const TempDir&)=delete; TempDir& operator=(const TempDir&)=delete;
    std::string dir(const std::string& name) const{ std::filesystem::create_directories(path+"/"+name); return path+"/"+name; }
    std::string file(const std::string& name) const{ std::ofstream f(path+"/"+name, std::ios::binary|std::ios::trunc); f<<name; return path+"/"+name; }
};
struct CwdGuard{
    std::string old;
    CwdGuard(){ char buf[4096]; CHECK(::getcwd(buf, sizeof buf) != nullptr); old=buf; }
    ~CwdGuard(){ if(!old.empty()) CHECK(::chdir(old.c_str()) == 0); }
    void enter(const std::string& p){ CHECK(::chdir(p.c_str()) == 0); }
};
// Columna VISUAL (1-based de "\x1b[1;<col>H") donde Renderer dibujaría el cursor.
// buildScreen emite gutter+visual+1; para viewport de 1 línea el gutter mide 3, se resta.
inline int cursorScreenCol(const std::string& line,int byteCol){ Document doc; doc.restore({line}); Viewport vp; vp.top=0; vp.height=1; vp.width=200; Cursor c; c.line=0; c.col=byteCol; Renderer r; std::string f=r.buildScreen(doc,c,vp,"t",false,"",State::Navegacion,std::nullopt); size_t pos=f.rfind("\x1b[1;"); if(pos==std::string::npos) return -1; size_t end=f.find('H',pos); return std::stoi(f.substr(pos+4,end-pos-4))-3; }
// Verifica las invariantes globales del estado de Editor y del buffer activo.
// Para todos los buffers, usar assertBuffersConsistent().
// - documento no vacío;
// - cursor dentro de los límites;
// - undo/redo dentro de MAX_UNDO;
// - selección y hasSelection() coherentes;
// - selección dentro de los límites;
// - selección normalizada;
// - estado global válido;
// - selección compatible con el estado;
// - clipboard UTF-8 válido.
inline void assertStateConsistent(Editor& ed){
    const Document& d=ed.active().document;
    CHECK(d.lineCount()>=1);
    for(int i=0;i<d.lineCount();++i) CHECK_EQ(d.lineAt(i).size(), static_cast<size_t>(d.lineLength(i)));
    CHECK(ed.active().cursor.line>=0); CHECK(ed.active().cursor.col>=0);
    CHECK(ed.active().cursor.line<d.lineCount());
    CHECK(ed.active().cursor.col<=d.lineLength(ed.active().cursor.line));
    CHECK(ed.active().undoStack.size()<=Editor::MAX_UNDO);
    CHECK(ed.active().redoStack.size()<=Editor::MAX_UNDO);
    if(ed.hasSelection()) CHECK(ed.active().selection.has_value());
    if(!ed.active().selection.has_value()) CHECK(!ed.hasSelection());
    if(ed.active().selection.has_value()){
        const Position& anchor=ed.active().selection->anchor;
        const Position& pos=ed.active().selection->position;
        CHECK(anchor.line>=0); CHECK(anchor.line<d.lineCount()); CHECK(anchor.col>=0); CHECK(anchor.col<=d.lineLength(anchor.line));
        CHECK(pos.line>=0); CHECK(pos.line<d.lineCount()); CHECK(pos.col>=0); CHECK(pos.col<=d.lineLength(pos.line));
    }
    if(ed.active().selection.has_value() && ed.active().selection->anchor==ed.active().selection->position) CHECK(!ed.hasSelection());
    if(auto norm=ed.selection()) CHECK(norm->start.line<norm->end.line || (norm->start.line==norm->end.line && norm->start.col<=norm->end.col));
    CHECK(ed.state_==State::Navegacion||ed.state_==State::Interaccion||ed.state_==State::Seleccion||ed.state_==State::Prefix||ed.state_==State::BufferSelector||ed.state_==State::SaveAs||ed.state_==State::FileBrowser||ed.state_==State::Busqueda||ed.state_==State::IrAFila);
    if(ed.hasSelection()) CHECK(ed.state_==State::Seleccion||ed.state_==State::Prefix||ed.state_==State::BufferSelector||ed.state_==State::SaveAs||ed.state_==State::FileBrowser||ed.state_==State::Busqueda||ed.state_==State::IrAFila);
    if(ed.state_==State::Seleccion) CHECK(ed.active().selection.has_value());
    for(const std::string& l: ed.getClipboardBlock()) CHECK(utf8::isValid(l));
}
inline void assertBufferConsistent(const Buffer& b){
    const Document& d=b.document;
    CHECK(d.lineCount()>=1);
    CHECK(b.cursor.line>=0); CHECK(b.cursor.line<d.lineCount());
    CHECK(b.cursor.col>=0); CHECK(b.cursor.col<=d.lineLength(b.cursor.line));
    CHECK(b.undoStack.size()<=Buffer::MAX_UNDO); CHECK(b.redoStack.size()<=Buffer::MAX_UNDO);
    if(b.selection.has_value()){
        const Position& anchor=b.selection->anchor; const Position& position=b.selection->position;
        CHECK(anchor.line>=0); CHECK(anchor.line<d.lineCount()); CHECK(anchor.col>=0); CHECK(anchor.col<=d.lineLength(anchor.line));
        CHECK(position.line>=0); CHECK(position.line<d.lineCount()); CHECK(position.col>=0); CHECK(position.col<=d.lineLength(position.line));
    }
}
inline void assertBuffersConsistent(Editor& ed){
    assertStateConsistent(ed);
    CHECK(!ed.buffers.buffers_.empty());
    CHECK(ed.buffers.activeBuffer_>=0); CHECK(ed.buffers.activeBuffer_<static_cast<int>(ed.buffers.buffers_.size()));
    for(const Buffer& b: ed.buffers.buffers_) assertBufferConsistent(b);
}
inline void assertInitialState(const Editor& ed){
    CHECK_EQ(static_cast<int>(ed.state_), static_cast<int>(State::Navegacion));
    CHECK(ed.active().cursor.line>=0); CHECK(ed.active().cursor.col>=0);
    CHECK(ed.active().cursor.line<ed.active().document.lineCount());
    CHECK(ed.active().cursor.col<=ed.active().document.lineLength(ed.active().cursor.line));
    CHECK(!ed.hasSelection());
    CHECK(ed.getClipboardBlock().empty());
    CHECK(!ed.active().modified);
}
// Snapshot documental completo que undo/redo restaura: lineas, cursor,
// seleccion y flag '\n' final. modified no entra (se recalcula contra savedLines).
struct BufferState{ std::vector<std::string> lines; Position cursor; std::optional<Selection> selection; bool endsWithNewline; };
inline BufferState capture(Buffer& b){ BufferState s; s.lines=b.document.snapshot(); s.cursor={b.cursor.line,b.cursor.col}; s.selection=b.selection; s.endsWithNewline=b.document.endsWithNewline(); return s; }
// Comparacion estructural (Selection no define operator==).
inline bool selEqual(const std::optional<Selection>& a,const std::optional<Selection>& b){ if(a.has_value()!=b.has_value()) return false; if(!a.has_value()) return true; return a->anchor.line==b->anchor.line && a->anchor.col==b->anchor.col && a->position.line==b->position.line && a->position.col==b->position.col; }
inline bool stateEqual(const BufferState& a,const BufferState& b){ return a.lines==b.lines && a.cursor.line==b.cursor.line && a.cursor.col==b.cursor.col && selEqual(a.selection,b.selection) && a.endsWithNewline==b.endsWithNewline; }
// Deshace y rehace toda la cadena committed del historial y exige
// reversibilidad exacta. Se descarta redoStack antes de capturar el estado
// porque una secuencia aleatoria puede haber dejado una rama divergente.
inline void assertUndoRedoRoundtrip(Editor& ed){
    ed.active().redoStack.clear();
    bool wasModified = ed.active().modified;
    const BufferState finalState=capture(ed.active());
    size_t undone=0; while(!ed.active().undoStack.empty()){ ed.handleEvent(ev(EventType::Undo)); assertStateConsistent(ed); ++undone; }
    CHECK(undone>0); assertStateConsistent(ed); ed.handleEvent(ev(EventType::Undo)); assertStateConsistent(ed);
    size_t redone=0; while(!ed.active().redoStack.empty()){ ed.handleEvent(ev(EventType::Redo)); assertStateConsistent(ed); ++redone; }
    CHECK_EQ(redone, undone);
    const BufferState restored=capture(ed.active());
    CHECK(stateEqual(restored, finalState));
    if(!stateEqual(restored, finalState)){
        std::cout << "    roundtrip undo->redo no devolvio el estado exacto\n";
        std::cout << "    lineas finales: " << finalState.lines.size() << ", restauradas: " << restored.lines.size() << "\n";
        for(size_t i=0;i<finalState.lines.size() && i<restored.lines.size();++i) if(finalState.lines[i]!=restored.lines[i]) std::cout << "    diff linea " << i << ": '" << finalState.lines[i] << "' vs '" << restored.lines[i] << "'\n";
    }
    CHECK(ed.active().modified == wasModified); assertStateConsistent(ed);
}
