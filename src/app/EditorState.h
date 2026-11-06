#pragma once

#include <array>

//   Navegacion:  (estado por defecto) 
//                No se puede escribir. Tecla 'i' entra
//                a Interaccion (edicion), 's' a Seleccion. Las flechas
//                se mueven libremente sin iniciar seleccion.
//   Interaccion: (activo con 'i')
//                edicion libre de texto (todo se inserta tal cual). ESC
//                vuelve a Navegacion. Antes era el comportamiento "Normal".
//   Seleccion: (activo con 's')
//                Las flechas/Home/End
//                extienden la seleccion; ESC o 'c'/'x' la terminan y
//                vuelven a Navegacion. Un caracter ya NO la reemplaza.
//   Prefix (Ctrl+K) 
//                el siguiente evento decide (Ctrl+S guarda,
//                Ctrl+Q sale, Ctrl+K n/t/w operan sobre buffers, cualquier
//                otra cosa lo cancela y se descarta).
//   BufferSelector (Ctrl+k t)
//                pantalla modal de listado de buffers. Solo se aceptan
//                ↑/↓, Enter y ESC; todo lo demas es no-op.
//   FileBrowser  (Ctrol+k o)
//                explorador de archivos modal tras Ctrl+K (comando) Se
//                navega desde el directorio de trabajo (cwd()). Solo se
//                aceptan ↑/↓, Enter y ESC (Ctrl+K tambien cancela). Enter
//                sobre una carpeta entra; sobre ".." sube un nivel; sobre un
//                archivo lo abre (o activa el buffer existente) y sale.
//   Busqueda: (activo con 'f')
//                activa el prompt donde el usuario puede buscar coincidencias de
//                caracter o plaabras en el archivo
//   Ir a Fila: (activo con 'g')
//                activa el prompt donde el usuario puede escribir a la fila que quiere ir
//                siempre y cuando sea valida
//   SaveAsFileBrowser (Crtl+k Ctrl+s)
//                explorador de archivos modal para "Guardar
//                como" se navega el directorio destino
//                con ↑/↓ (Enter solo entra a carpetas, nunca abre archivos)
//                y se escribe el nombre en el input inferior; Ctrl+S guarda,
//                ESC cancela. Modal puro.
//   Renombrar  (Ctrl+k r) 
//                prompt "Nombre del archivo:" para renombrar el
//                archivo del buffer activo en su mismo directorio (solo
//                basename, sin rutas ni creacion de directorios). Enter
//                confirma, ESC cancela a Navegacion. Solo valido si el
//                buffer ya tiene nombre; si no, se cancela solo.

enum class State {
    Navegacion,
    Interaccion,
    Seleccion,
    Prefix,
    BufferSelector,
    FileBrowser,
    Busqueda,
    IrAFila,
    SaveAsFileBrowser,
    Renombrar,
};

constexpr auto getStateEditor() {
    return std::array{
        State::Navegacion,
        State::Interaccion,
        State::Seleccion,
        State::Prefix,
        State::BufferSelector,
        State::FileBrowser,
        State::Busqueda,
        State::IrAFila,
        State::SaveAsFileBrowser,
        State::Renombrar,
    };
}