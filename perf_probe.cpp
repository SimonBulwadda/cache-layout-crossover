// Standalone diagnostic for Step 13 -- NOT part of the main experiment harness.
// Builds ONE container, then loops the touch operation many times with no
// reconstruction in between, so a perf-stat wrapper measures the touch loop
// itself instead of being swamped by repeated memory allocation.
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include <iostream>
#include <string>

template <size_t PadBytes>
struct Entity {
    int id;
    float x, y, z;
    float vx, vy, vz;
    char padding[PadBytes];
    int touch() { return id += (int)(x + y + z); }
};

template <size_t PadBytes>
std::vector<Entity<PadBytes>> make_packed(size_t count) {
    std::vector<Entity<PadBytes>> v;
    v.reserve(count);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (size_t i = 0; i < count; i++) {
        Entity<PadBytes> e{};
        e.id = (int)i;
        e.x = dist(rng); e.y = dist(rng); e.z = dist(rng);
        v.push_back(e);
    }
    return v;
}

template <size_t PadBytes>
std::vector<std::unique_ptr<Entity<PadBytes>>> make_indirected(size_t count) {
    std::vector<std::unique_ptr<Entity<PadBytes>>> v;
    v.reserve(count);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (size_t i = 0; i < count; i++) {
        auto e = std::make_unique<Entity<PadBytes>>();
        e->id = (int)i;
        e->x = dist(rng); e->y = dist(rng); e->z = dist(rng);
        v.push_back(std::move(e));
    }
    return v;
}

// Same scrambling logic as main.cpp's §3.6 fix -- pointer order shuffled
// after construction, distinct seed from the data-generation RNG.
template <size_t PadBytes>
std::vector<std::unique_ptr<Entity<PadBytes>>> make_indirected_scrambled(size_t count) {
    auto v = make_indirected<PadBytes>(count);
    std::mt19937 rng(99);
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

constexpr size_t HUGE_PILE_BYTES = 128 * 1024 * 1024;  // matches main.cpp's fallback target
constexpr long TOUCH_PASSES = 2000;  // full passes over the container -- construction cost gets amortized away

template <size_t PadBytes>
void run_packed(long passes) {
    size_t n = HUGE_PILE_BYTES / sizeof(Entity<PadBytes>);
    auto v = make_packed<PadBytes>(n);   // <-- built ONCE, outside the timed loop
    volatile int sink = 0;
    for (long p = 0; p < passes; p++) {
        int result = 0;
        for (auto& e : v) result += e.touch();
        sink = result;  // keeps the compiler from deleting the loop as dead code
    }
    std::cout << "packed done: n=" << n << " passes=" << passes
              << " total_touches=" << (n * (size_t)passes) << " sink=" << sink << "\n";
}

template <size_t PadBytes>
void run_scrambled(long passes) {
    size_t n = HUGE_PILE_BYTES / sizeof(Entity<PadBytes>);
    auto v = make_indirected_scrambled<PadBytes>(n);   // <-- built ONCE
    volatile int sink = 0;
    for (long p = 0; p < passes; p++) {
        int result = 0;
        for (auto& e : v) result += e->touch();
        sink = result;
    }
    std::cout << "scrambled done: n=" << n << " passes=" << passes
              << " total_touches=" << (n * (size_t)passes) << " sink=" << sink << "\n";
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <size:4096|16384|65536> <container:packed|scrambled>\n";
        return 1;
    }
    std::string size_arg = argv[1];
    std::string container_arg = argv[2];

    if (size_arg == "4096") {
        if (container_arg == "packed") run_packed<4068>(TOUCH_PASSES);
        else if (container_arg == "scrambled") run_scrambled<4068>(TOUCH_PASSES);
        else { std::cerr << "container must be 'packed' or 'scrambled'\n"; return 1; }
    } else if (size_arg == "16384") {
        if (container_arg == "packed") run_packed<16356>(TOUCH_PASSES);
        else if (container_arg == "scrambled") run_scrambled<16356>(TOUCH_PASSES);
        else { std::cerr << "container must be 'packed' or 'scrambled'\n"; return 1; }
    } else if (size_arg == "65536") {
        if (container_arg == "packed") run_packed<65508>(TOUCH_PASSES);
        else if (container_arg == "scrambled") run_scrambled<65508>(TOUCH_PASSES);
        else { std::cerr << "container must be 'packed' or 'scrambled'\n"; return 1; }
    } else {
        std::cerr << "size must be 4096, 16384, or 65536\n";
        return 1;
    }
    return 0;
}