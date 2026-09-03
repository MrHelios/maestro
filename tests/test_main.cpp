#include "test_framework.h"
#include "ui/Renderer.h"

int main() {
    Renderer::setTestMode(true);
    return testfw::runAll();
}
