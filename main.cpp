#include <benchmark/benchmark.h>
#include <cstddef>
#include <vector>
#include <memory>
#include <random>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <cstdlib>

template <size_t PadBytes>
struct Entity {
    int id;
    float x, y, z;
    float vx, vy, vz;
    char padding[PadBytes];
    int touch() { return id += (int)(x + y + z); }
};

// Compile-time size verification for all 12 sweep sizes (28-byte fixed footprint + PadBytes)
static_assert(sizeof(Entity<4>)    == 32,   "size mismatch: expected 32");
static_assert(sizeof(Entity<36>)   == 64,   "size mismatch: expected 64");
static_assert(sizeof(Entity<100>)  == 128,  "size mismatch: expected 128");
static_assert(sizeof(Entity<228>)  == 256,  "size mismatch: expected 256");
static_assert(sizeof(Entity<484>)  == 512,  "size mismatch: expected 512");
static_assert(sizeof(Entity<996>)  == 1024, "size mismatch: expected 1024");
static_assert(sizeof(Entity<2020>) == 2048, "size mismatch: expected 2048");
static_assert(sizeof(Entity<4068>) == 4096, "size mismatch: expected 4096");
static_assert(sizeof(Entity<8164>) == 8192, "size mismatch: expected 8192");
static_assert(sizeof(Entity<16356>) == 16384, "size mismatch: expected 16384");
static_assert(sizeof(Entity<32740>) == 32768, "size mismatch: expected 32768");
static_assert(sizeof(Entity<65508>) == 65536, "size mismatch: expected 65536");

constexpr size_t SMALL_PILE_BYTES = 128 * 1024;         // ~128 KB, §3.5 -- always far below L1/L2, no dynamic scaling needed
constexpr size_t HUGE_PILE_BYTES_FALLBACK = 128 * 1024 * 1024;  // ~128 MB fallback, used only if cache detection fails

// §3.8/§3.12 fix, found via the GitHub Actions run whose L3 (260 MiB) turned out
// bigger than the fixed 128 MB huge-pile target -- meaning "huge pile" silently
// stopped being memory-resident on that specific cloud VM. No longer trusting a
// fixed byte count: detect the largest cache level actually present on THIS run
// (via the same CPUInfo Google Benchmark uses for its own startup banner) and
// set the huge-pile target to a safe multiple beyond it, so the regime stays
// valid no matter which host GitHub Actions (or any machine) hands us.
size_t HUGE_PILE_BYTES = HUGE_PILE_BYTES_FALLBACK;  // overwritten in main() after detection

size_t detect_largest_cache_bytes() {
    const auto& info = benchmark::CPUInfo::Get();
    int max_level = -1;
    size_t total_bytes = 0;
    for (const auto& c : info.caches) {
        if (c.level > max_level) {
            max_level = c.level;
            int sharing = c.num_sharing > 0 ? c.num_sharing : 1;
            int instances = info.num_cpus / sharing;
            if (instances < 1) instances = 1;
            total_bytes = (size_t)c.size * (size_t)instances;
        }
    }
    return total_bytes;  // 0 if detection failed (e.g. no cache info available)
}

// regime: 0 = small pile (cache-resident), 1 = huge pile (memory-resident)
size_t count_for(size_t object_size, int regime) {
    size_t target = (regime == 0) ? SMALL_PILE_BYTES : HUGE_PILE_BYTES;
    size_t n = target / object_size;
    return n < 1 ? 1 : n;
}

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

// §3.6 fix: same objects, same physical locations -- but the CONTAINER's own
// pointer order is shuffled, so walking it in plain slot order (0, 1, 2...)
// no longer matches the tidy allocation order. This decouples "visit order"
// from "physical order" for the indirected container, which is the actual
// thing that was letting the prefetcher cheat.
template <size_t PadBytes>
std::vector<std::unique_ptr<Entity<PadBytes>>> make_indirected_scrambled(size_t count) {
    auto v = make_indirected<PadBytes>(count);
    std::mt19937 rng(99);  // distinct seed: this is allocation-order scrambling, not data or access-order
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

std::vector<size_t> make_shuffled_indices(size_t count) {
    std::vector<size_t> idx(count);
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(7);
    std::shuffle(idx.begin(), idx.end(), rng);
    return idx;
}

template <size_t PadBytes>
static void BM_Sequential_Packed(benchmark::State& state) {
    int regime = (int)state.range(0);
    size_t n = count_for(sizeof(Entity<PadBytes>), regime);
    for (auto _ : state) {
        state.PauseTiming();
        auto v = make_packed<PadBytes>(n);
        state.ResumeTiming();
        int result = 0;
        for (auto& e : v) result += e.touch();
        benchmark::DoNotOptimize(result);
    }
    state.SetLabel(regime == 0 ? "small_pile" : "huge_pile");
}
template <size_t PadBytes>
static void BM_Sequential_Indirected(benchmark::State& state) {
    int regime = (int)state.range(0);
    size_t n = count_for(sizeof(Entity<PadBytes>), regime);
    for (auto _ : state) {
        state.PauseTiming();
        auto v = make_indirected<PadBytes>(n);
        state.ResumeTiming();
        int result = 0;
        for (auto& e : v) result += e->touch();
        benchmark::DoNotOptimize(result);
    }
    state.SetLabel(regime == 0 ? "small_pile" : "huge_pile");
}
// New: sequential walk over a container whose pointer order has been scrambled.
template <size_t PadBytes>
static void BM_Sequential_Indirected_Scrambled(benchmark::State& state) {
    int regime = (int)state.range(0);
    size_t n = count_for(sizeof(Entity<PadBytes>), regime);
    for (auto _ : state) {
        state.PauseTiming();
        auto v = make_indirected_scrambled<PadBytes>(n);
        state.ResumeTiming();
        int result = 0;
        for (auto& e : v) result += e->touch();
        benchmark::DoNotOptimize(result);
    }
    state.SetLabel(regime == 0 ? "small_pile" : "huge_pile");
}
template <size_t PadBytes>
static void BM_Random_Packed(benchmark::State& state) {
    int regime = (int)state.range(0);
    size_t n = count_for(sizeof(Entity<PadBytes>), regime);
    for (auto _ : state) {
        state.PauseTiming();
        auto v = make_packed<PadBytes>(n);
        auto idx = make_shuffled_indices(n);
        state.ResumeTiming();
        int result = 0;
        for (auto i : idx) result += v[i].touch();
        benchmark::DoNotOptimize(result);
    }
    state.SetLabel(regime == 0 ? "small_pile" : "huge_pile");
}
template <size_t PadBytes>
static void BM_Random_Indirected(benchmark::State& state) {
    int regime = (int)state.range(0);
    size_t n = count_for(sizeof(Entity<PadBytes>), regime);
    for (auto _ : state) {
        state.PauseTiming();
        auto v = make_indirected<PadBytes>(n);
        auto idx = make_shuffled_indices(n);
        state.ResumeTiming();
        int result = 0;
        for (auto i : idx) result += v[i]->touch();
        benchmark::DoNotOptimize(result);
    }
    state.SetLabel(regime == 0 ? "small_pile" : "huge_pile");
}
// New: random-order walk over a container whose pointer order is ALSO scrambled.
// Expectation, worth checking once you have the numbers: this should come out
// very close to BM_Sequential_Indirected_Scrambled -- once the underlying
// pointer order is already unpredictable, adding a second shuffle on top of
// it shouldn't change much. If sequential-scrambled and random-scrambled land
// close together, that's direct confirmation the fix worked (mirrors the
// §3.11 Step 4 validation logic: a genuinely unpredictable pointer target
// shouldn't care whether you also shuffle the visiting order on top).
template <size_t PadBytes>
static void BM_Random_Indirected_Scrambled(benchmark::State& state) {
    int regime = (int)state.range(0);
    size_t n = count_for(sizeof(Entity<PadBytes>), regime);
    for (auto _ : state) {
        state.PauseTiming();
        auto v = make_indirected_scrambled<PadBytes>(n);
        auto idx = make_shuffled_indices(n);
        state.ResumeTiming();
        int result = 0;
        for (auto i : idx) result += v[i]->touch();
        benchmark::DoNotOptimize(result);
    }
    state.SetLabel(regime == 0 ? "small_pile" : "huge_pile");
}

#define REGISTER_SIZE(PAD) \
    BENCHMARK_TEMPLATE(BM_Sequential_Packed, PAD)->Arg(0)->Arg(1); \
    BENCHMARK_TEMPLATE(BM_Sequential_Indirected, PAD)->Arg(0)->Arg(1); \
    BENCHMARK_TEMPLATE(BM_Sequential_Indirected_Scrambled, PAD)->Arg(0)->Arg(1); \
    BENCHMARK_TEMPLATE(BM_Random_Packed, PAD)->Arg(0)->Arg(1); \
    BENCHMARK_TEMPLATE(BM_Random_Indirected, PAD)->Arg(0)->Arg(1); \
    BENCHMARK_TEMPLATE(BM_Random_Indirected_Scrambled, PAD)->Arg(0)->Arg(1);

REGISTER_SIZE(4)
REGISTER_SIZE(36)
REGISTER_SIZE(100)
REGISTER_SIZE(228)
REGISTER_SIZE(484)
REGISTER_SIZE(996)
REGISTER_SIZE(2020)
REGISTER_SIZE(4068)
REGISTER_SIZE(8164)
REGISTER_SIZE(16356)
REGISTER_SIZE(32740)
REGISTER_SIZE(65508)

int main(int argc, char** argv) {
    // Dynamic huge-pile sizing (new): detect the largest cache level on THIS
    // machine and set HUGE_PILE_BYTES to comfortably clear it, instead of
    // trusting a fixed 128 MB guess. Runs before benchmark::Initialize() so
    // every BM_* function (which reads the HUGE_PILE_BYTES global via
    // count_for()) sees the corrected value once RunSpecifiedBenchmarks()
    // actually executes them.
    {
        size_t detected = detect_largest_cache_bytes();
        std::cout << "=== Dynamic huge-pile sizing ===\n";
        if (detected > 0) {
            const size_t SAFETY_MULTIPLE = 4;  // matches the ~4x-21x margins already used on Machines 1/2
            size_t dynamic_target = detected * SAFETY_MULTIPLE;
            HUGE_PILE_BYTES = std::max(HUGE_PILE_BYTES_FALLBACK, dynamic_target);
            std::cout << "Largest detected cache level: " << detected
                      << " bytes (" << (detected / (1024.0 * 1024.0)) << " MiB)\n";
        } else {
            std::cout << "Cache detection failed -- falling back to fixed 128 MiB target.\n";
        }
        std::cout << "HUGE_PILE_BYTES set to: " << HUGE_PILE_BYTES
                  << " bytes (" << (HUGE_PILE_BYTES / (1024.0 * 1024.0)) << " MiB)\n";
        std::cout << "==================================\n\n";
    }

    // §3.6 allocation-order diagnostic, part 1 (unchanged): confirms the tidy
    // allocation problem we already found.
    {
        auto v = make_indirected<228>(1000);
        std::vector<uintptr_t> addrs;
        for (auto& e : v) addrs.push_back(reinterpret_cast<uintptr_t>(e.get()));
        long long total_dist = 0, min_dist = -1, max_dist = 0;
        for (size_t i = 1; i < addrs.size(); i++) {
            long long dist = std::llabs((long long)addrs[i] - (long long)addrs[i - 1]);
            total_dist += dist;
            if (min_dist == -1 || dist < min_dist) min_dist = dist;
            if (dist > max_dist) max_dist = dist;
        }
        double avg_dist = (double)total_dist / (double)(addrs.size() - 1);
        std::cout << "=== S3.6 diagnostic, TIDY allocation (256-byte objects, n=1000) ===\n";
        std::cout << "Average distance: " << avg_dist << " bytes, Min: " << min_dist
                  << ", Max: " << max_dist << "\n";
        std::cout << "=====================================================================\n\n";
    }

    // §3.6 allocation-order diagnostic, part 2 (new): same objects, but walked
    // in the SCRAMBLED pointer order -- proves the fix actually broke the
    // predictable stride.
    {
        auto v = make_indirected_scrambled<228>(1000);
        std::vector<uintptr_t> addrs;
        for (auto& e : v) addrs.push_back(reinterpret_cast<uintptr_t>(e.get()));
        long long total_dist = 0, min_dist = -1, max_dist = 0;
        for (size_t i = 1; i < addrs.size(); i++) {
            long long dist = std::llabs((long long)addrs[i] - (long long)addrs[i - 1]);
            total_dist += dist;
            if (min_dist == -1 || dist < min_dist) min_dist = dist;
            if (dist > max_dist) max_dist = dist;
        }
        double avg_dist = (double)total_dist / (double)(addrs.size() - 1);
        std::cout << "=== S3.6 diagnostic, SCRAMBLED allocation (256-byte objects, n=1000) ===\n";
        std::cout << "Average distance: " << avg_dist << " bytes, Min: " << min_dist
                  << ", Max: " << max_dist << "\n";
        std::cout << "(Expect much larger average and a big min/max spread -- unlike the\n";
        std::cout << " tidy version above, which was a constant 272 bytes every time.)\n";
        std::cout << "==========================================================================\n\n";
    }

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}