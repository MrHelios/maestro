// Petición de suspensión (Ctrl+Z -> InputEventType::Suspend).
//
// Contrato: handleEvent marca la petición en CUALQUIER modo (incluidos
// los modales) sin mutar nada más, y consumeSuspendRequest() la entrega
// una sola vez (consumo destructivo). El loop de plataforma es quien
// ejecuta (raise SIGTSTP); acá solo se verifica el registro/entrega.

#include "test_framework.h"

#include "platform/InputEvent.h"
#include "app/Editor.h"

namespace {

InputEvent evOf(InputEventType t) {
    InputEvent e;
    e.type = t;
    return e;
}

InputEvent charEv(const std::string& s) {
    InputEvent e;
    e.type = InputEventType::InsertChar;
    e.text = s;
    return e;
}

}  // namespace

TEST(editor_suspend_request_navegacion) {
    Editor ed;
    CHECK(!ed.consumeSuspendRequest());
    ed.processEventForTesting(evOf(InputEventType::Suspend));
    CHECK(ed.consumeSuspendRequest());
    CHECK(!ed.consumeSuspendRequest());  // una sola vez
}

TEST(editor_suspend_request_interaccion) {
    Editor ed;
    ed.processEventForTesting(charEv("i"));  // Navegacion -> Interaccion
    CHECK(!ed.consumeSuspendRequest());
    ed.processEventForTesting(evOf(InputEventType::Suspend));
    CHECK(ed.consumeSuspendRequest());
    CHECK(!ed.consumeSuspendRequest());
}

TEST(editor_suspend_request_desde_prefijo) {
    Editor ed;
    ed.processEventForTesting(evOf(InputEventType::Prefix));  // modo modal
    CHECK(!ed.consumeSuspendRequest());
    // Suspend escapa al modal: marca la petición en vez de cancelarlo.
    ed.processEventForTesting(evOf(InputEventType::Suspend));
    CHECK(ed.consumeSuspendRequest());
    CHECK(!ed.consumeSuspendRequest());
}

TEST(editor_suspend_no_muta_estado) {
    Editor ed;
    // La petición no cambia el modo ni produce mensajes: solo el flag.
    ed.processEventForTesting(evOf(InputEventType::Suspend));
    CHECK(ed.getStateForTesting() == State::Navegacion);
    CHECK(ed.consumeSuspendRequest());
}
