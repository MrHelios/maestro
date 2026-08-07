#pragma once

#include <atomic>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace testfw {

struct Test {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

inline int& failureCount() {
    static int c = 0;
    return c;
}

inline void report(bool ok, const std::string& cond, const char* file, int line) {
    if (!ok) {
        std::cout << "  [FAIL] " << cond << "   (" << file << ":" << line << ")\n";
        failureCount()++;
    }
}

struct Registrar {
    Registrar(const std::string& n, std::function<void()> fn) {
        registry().push_back({n, std::move(fn)});
    }
};

inline int runAll() {
    const int total = static_cast<int>(registry().size());
    for (const Test& t : registry()) {
        const int before = failureCount();
        std::cout << "[RUN] " << t.name << "\n";
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::cout << "  [EXCEPTION] " << e.what() << "\n";
            failureCount()++;
        } catch (...) {
            std::cout << "  [EXCEPTION] unknown\n";
            failureCount()++;
        }
        if (failureCount() == before)
            std::cout << "  ok\n";
    }
    std::cout << "-----------------------------------\n";
    std::cout << total << " tests, " << failureCount() << " failure(s)\n";
    return failureCount() == 0 ? 0 : 1;
}

} // namespace testfw

#define TEST(name) \
    static void testfw_##name(); \
    static ::testfw::Registrar testfw_reg_##name(#name, testfw_##name); \
    static void testfw_##name()

#define CHECK(cond) \
    do { ::testfw::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__); } while (0)

#define CHECK_EQ(a, b) \
    do { \
        auto ta = (a); auto tb = (b); \
        if (!(ta == tb)) { \
            ::testfw::report(false, #a " == " #b, __FILE__, __LINE__); \
            std::cout << "          lhs=" << ta << " rhs=" << tb << "\n"; \
        } \
    } while (0)

namespace testfw {

inline std::string tmpPath() {
    static std::atomic<int> n{0};
    return "/tmp/edit_test_" + std::to_string(static_cast<long>(::getpid())) + "_" +
           std::to_string(n++) + ".txt";
}

// Archivo temporal que se elimina en ~TempFile, aunque un CHECK falle antes.
struct TempFile {
    std::string path;

    TempFile() : path(tmpPath()) {}

    explicit TempFile(std::string p) : path(std::move(p)) {}

    ~TempFile() {
        std::remove(path.c_str());
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    void write(const std::string& content) const {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);

        CHECK(f.good());

        f << content;

        CHECK(f.good());
    }
};

} // namespace testfw
