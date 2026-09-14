#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fnmatch.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
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

inline int& skipCount() {
    static int c = 0;
    return c;
}

inline bool& currentTestSkipped() {
    static thread_local bool s = false;
    return s;
}

inline void report(bool ok, const std::string& cond, const char* file, int line) {
    if (!ok) {
        std::cout << "  [FAIL] " << cond << "   (" << file << ":" << line << ")\n";
        failureCount()++;
    }
}

inline void reportSkip(const std::string& reason, const char* file, int line) {
    std::cout << "  [SKIP] " << reason << "   (" << file << ":" << line << ")\n";
    skipCount()++;
    currentTestSkipped() = true;
}

struct Registrar {
    Registrar(const std::string& n, std::function<void()> fn) {
        registry().push_back({n, std::move(fn)});
    }
};

inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) out.push_back(item);
    return out;
}

inline bool matchesFilter(const std::string& name, const std::string& filter) {
    if (filter.empty()) return true;
    std::string posPart = filter;
    std::string negPart;
    auto dash = filter.find('-');
    if (dash != std::string::npos) {
        posPart = filter.substr(0, dash);
        negPart = filter.substr(dash + 1);
    }
    if (posPart.empty()) posPart = "*";
    bool posMatch = false;
    for (auto& pat : split(posPart, ':')) {
        if (pat.empty()) continue;
        if (fnmatch(pat.c_str(), name.c_str(), 0) == 0) { posMatch = true; break; }
    }
    if (!posMatch) return false;
    if (!negPart.empty()) {
        for (auto& pat : split(negPart, ':')) {
            if (pat.empty()) continue;
            if (fnmatch(pat.c_str(), name.c_str(), 0) == 0) return false;
        }
    }
    return true;
}

inline int runAll(const std::string& filter = {}) {
    std::string eff = filter;
    if (eff.empty()) {
        if (const char* e = std::getenv("FILTER")) eff = e;
    }
    int total = 0;
    int run = 0;
    for (const Test& t : registry()) {
        if (!matchesFilter(t.name, eff)) continue;
        run++;
    }
    if (run == 0 && !eff.empty()) {
        std::cout << "No tests match filter \"" << eff << "\" (" << registry().size() << " total)\n";
        return 0;
    }
    for (const Test& t : registry()) {
        if (!matchesFilter(t.name, eff)) continue;
        total++;
        std::set<std::string> beforeFiles;
        try {
            for (auto& e : std::filesystem::directory_iterator(".")) beforeFiles.insert(e.path().string());
        } catch (...) {}
        const int before = failureCount();
        currentTestSkipped() = false;
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
        try {
            for (auto& e : std::filesystem::directory_iterator(".")) {
                auto p = e.path().string();
                if (beforeFiles.find(p) == beforeFiles.end()) std::filesystem::remove(p);
            }
        } catch (...) {}
        if (currentTestSkipped())
            std::cout << "  skipped\n";
        else if (failureCount() == before)
            std::cout << "  ok\n";
    }
    std::cout << "-----------------------------------\n";
    std::cout << total << " tests, " << failureCount() << " failure(s), " << skipCount() << " skipped\n";
    return failureCount() == 0 ? 0 : 1;
}

} // namespace testfw

#define TEST(name) \
    static void testfw_##name(); \
    static ::testfw::Registrar testfw_reg_##name(#name, testfw_##name); \
    static void testfw_##name()

#define CHECK(cond) \
    do { ::testfw::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__); } while (0)

#define SKIP(reason) \
    do { ::testfw::reportSkip(reason, __FILE__, __LINE__); return; } while (0)

#define CHECK_EQ(a, b) \
    do { \
        auto ta = (a); auto tb = (b); \
        if (!(ta == tb)) { \
            ::testfw::report(false, #a " == " #b, __FILE__, __LINE__); \
            std::cout << "          lhs=" << ta << " rhs=" << tb << "\n"; \
        } \
    } while (0)

namespace testfw {

struct TempDir {
    std::string path;
    explicit TempDir(std::string p) : path(std::move(p)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

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
