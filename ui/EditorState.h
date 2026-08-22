#pragma once

// Maquina de estados interna del editor (v0.5: modalidad tipo modal).
//   Navegacion:  estado por defecto. No se puede escribir. Tecla 'i' entra
//                a Interaccion (edicion), 's' a Seleccion. Las flechas
//                se mueven libremente sin iniciar seleccion.
//   Interaccion: edicion libre de texto (todo se inserta tal cual). ESC
//                vuelve a Navegacion. Antes era el comportamiento "Normal".
//   Seleccion:   modo seleccion (activo con 's'). Las flechas/Home/End
//                extienden la seleccion; ESC o 'c'/'x' la terminan y
//                vuelven a Navegacion. Un caracter ya NO la reemplaza.
//   Prefix:      tras Ctrl+K; el siguiente evento decide (Ctrl+S guarda,
//                Ctrl+Q sale, Ctrl+K n/t/w operan sobre buffers, cualquier
//                otra cosa lo cancela y se descarta).
//   BufferSelector (v0.6.3): pantalla modal de listado de buffers tras
//                Ctrl+K t. Solo se aceptan
//                ↑/↓, Enter y ESC; todo lo demas es no-op. Nota v0.8:
//                cerrar un buffer (Ctrl+K w) ya NO abre este modal: el
//                buffer vecino que hereda la ranura queda activo. El
//                selector es el verbo EXPLICITO para elegir (Ctrl+K t).
//   SaveAs (v0.7): prompt "Guardar archivo:" tras Ctrl+K Ctrl+S sobre un
//                buffer SIN NOMBRE (p.ej. creado con Ctrl+K n). El usuario
//                escribe una ruta en la fila de mensajes; Enter guarda,
//                ESC cancela. Modal: solo se aceptan caracteres, Backspace,
//                Enter y ESC.
//   FileBrowser (v0.6.4): explorador de archivos modal tras Ctrl+K o. Se
//                navega desde el directorio de trabajo (cwd()). Solo se
//                aceptan ↑/↓, Enter y ESC (Ctrl+K tambien cancela). Enter
//                sobre una carpeta entra; sobre ".." sube un nivel; sobre un
//                archivo lo abre (o activa el buffer existente) y sale.
//
// IMPORTANTE: estas son DOS cosas distintas que NO hay que confundir.
//   state_ == State::Seleccion  == "el modo seleccion esta ACTIVO" (modo).
//   hasSelection()              == "existe un rango de texto seleccionado",
//                                  es decir un rango NO vacio (anchor != position).
//
// Al entrar al modo ('s') sin haber movido el cursor, beginSelection()
// fija anchor == position, asi que es posible tener:
//     state_ == State::Seleccion   y   hasSelection() == false
// (modo seleccion listo pero todavia sin texto marcado). Eso es correcto
// por diseño: el modo es la capacidad de extender; hasSelection() es el
// resultado concreto. Un estado solo significa lo que anuncia: "modo"
// NO implica "texto seleccionado".
enum class State {
    Navegacion,
    Interaccion,
    Seleccion,
    Prefix,
    BufferSelector,
    SaveAs,
    FileBrowser,
    Busqueda,
};
