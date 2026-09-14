#include "test_framework.h"
#include "ui/Renderer.h"

int main(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--gtest_filter=", 0) == 0) filter = a.substr(15);
        else if (a.rfind("--filter=", 0) == 0) filter = a.substr(9);
        else if (a == "--gtest_list_tests" || a == "--list") {
            for (auto& t : testfw::registry()) std::cout << t.name << "\n";
            return 0;
        }
    }
    Renderer::setTestMode(true);
    return testfw::runAll(filter);
}
