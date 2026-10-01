#include "rendering/Renderer.h"

#include "rendering/tty/Theme.h"
#include "rendering/tty/TtyDiff.h"
#include "rendering/tty/TtyEncoder.h"

Renderer::Renderer()
    : encoder_(std::make_unique<TtyEncoder>()),
      diff_(std::make_unique<TtyDiff>()) {}

Renderer::~Renderer() = default;

void Renderer::setTheme(const Theme& t) {
    encoder_->setTheme(t);
    diff_->setTheme(t);
}

const Theme& Renderer::theme() const { return encoder_->theme(); }

void Renderer::invalidateCache() { diff_->invalidateCache(); }

bool Renderer::hasCache() const { return diff_->hasCache(); }

int Renderer::lastViewportH() const { return diff_->lastViewportH(); }

void Renderer::setExternalSyntaxCache(SyntaxCache* c) {
    frameBuilder_.setExternalSyntaxCache(c);
    diff_->setExternalSyntaxCache(c);
}

std::string Renderer::buildScreen(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    Frame f = frameBuilder_.buildFrame(doc, cursor, viewport, filename,
                                       modified, message, state, selection,
                                       searchHighlight, bracketPair);
    std::string out;
    encoder_->appendFrame(out, f);
    return out;
}

void Renderer::renderScreen(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, Sink& sink,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    std::string buffer = buildScreen(doc, cursor, viewport, filename, modified,
                                     message, state, selection,
                                     searchHighlight, bracketPair);
    sink.writeStdout(buffer);
}

void Renderer::renderScreenDiff(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, Sink& sink,
    const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    const std::string out =
        buildDiffFrame(doc, cursor, viewport, filename, modified, message,
                       state, selection, searchHighlight, bracketPair);
    if (!sink.writeStdout(out)) diff_->invalidateCache();
}

std::string Renderer::buildDiffFrame(
    const Document& doc, const Cursor& cursor, const Viewport& viewport,
    const std::string& filename, bool modified, const Message& message,
    State state, const std::optional<Selection>& selection,
    const std::optional<Selection>& searchHighlight,
    const std::optional<BracketPair>& bracketPair) {
    return diff_->buildDiffFrame(doc, cursor, viewport, filename, modified,
                                 message, state, selection, searchHighlight,
                                 bracketPair);
}

