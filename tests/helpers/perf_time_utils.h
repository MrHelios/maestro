#pragma once
#include <chrono>
#include <cstdio>
#include <functional>

namespace perf_time {

inline size_t g_sink = 0;

template<typename F>
double bench_us(const char* label, int iters, F&& fn) {
    fn();
    auto s = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto e = std::chrono::steady_clock::now();
    double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
    double us = ns / iters / 1000.0;
    std::printf("%-48s %8.2f us/op  (%d iters)\n", label, us, iters);
    return us;
}

template<typename F>
double bench_ns(const char* label, int iters, F&& fn) {
    fn();
    auto s = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto e = std::chrono::steady_clock::now();
    double ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
    double per = ns / iters;
    std::printf("%-46s %8.1f us/frame  (%d frames)\n", label, per / 1000.0, iters);
    return per;
}

template<typename F>
double bench_with_total(const char* label, int iters, double total_ns, F&& fn) {
    fn();
    auto s = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto e = std::chrono::steady_clock::now();
    double ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
    double us = ns / iters / 1000.0;
    double total_us = total_ns / 1000.0;
    std::printf("%-46s %8.1f us  (%4.1f%% del total)\n", label, us,
                total_us > 0 ? 100.0 * us / total_us : 0.0);
    return us;
}

}
