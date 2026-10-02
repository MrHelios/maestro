#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rendering/ScreenRenderer.h"

// ---------------------------------------------------------------------------
// NullScreenRenderer: implementación no-op neutra del puerto ScreenRenderer.
//
// No pinta nada (render* descartan), no cachea (invalidate es no-op), no
// engancha sintaxis y lleva su propio flag de tema opaco (arranca oscuro,
// igual que el backend real).
//
// Para qué existe: es el default del Editor (ver Editor.h). Los tests de
// lógica (edición, modos, clipboard, watcher) nunca pintan y así no nombran
// ningún backend; solo los tests que afirman bytes de pantalla inyectan el
// backend real vía setRenderer(). Al vivir en rendering/ (zona neutra) el
// objeto Editor.o no referencia ningún símbolo de rendering/tty/ y un build
// sin TTY linkea igual. Producción siempre inyecta el backend real en el
// composition root (main.cpp); el Null nunca pinta en producción.
//
// A TENER EN CUENTA (fallo silencioso): si un futuro entry point (tool,
// bench, GUI) construye un Editor y olvida setRenderer, la app corre con
// pantalla en blanco SIN error: renderFrame ejecuta scroll/brackets/mensajes
// pero estos render* no emiten ni un byte (ni siquiera llaman a writeStdout,
// así que el assert de sink() tampoco salta). Es riesgo solo futuro: hoy
// main.cpp es el único entry point y siempre inyecta. Si el silencio alguna
// vez muerde, la salida es exigir el renderer por ctor (convierte el olvido
// en error de compilación) a costa de migrar los tests que usan el default.
// ---------------------------------------------------------------------------
class NullScreenRenderer : public ScreenRenderer {
public:
    void invalidateCache() override {}
    void setExternalSyntaxCache(SyntaxCache*) override {}
    void toggleTheme() override { dark_ = !dark_; }
    bool isDarkTheme() const override { return dark_; }

    void renderScreenDiff(const Document&, const Cursor&, const Viewport&,
                          const std::string&, bool, const Message&, State,
                          Sink&, const std::optional<Selection>&,
                          const std::optional<Selection>&,
                          const std::optional<BracketPair>&) override {}

    void renderBufferList(const std::vector<std::string>&, int, int, int,
                          Sink&) override {}

    void renderFileList(const std::vector<FileListItem>&, int, int,
                        const std::string&, const Message&, int, int,
                        Sink&) override {}

private:
    bool dark_ = true;
};
