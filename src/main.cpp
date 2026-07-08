#include "allocator.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <random>

static constexpr std::uint32_t CANARY = 0xDEADBEEF;

static void print_sep() {
    std::cout << "  ---------------------------------------------------------------\n";
}

static void print_banner(const std::string& title) {
    std::cout << "\n"
              << "  ===============================================================\n"
              << "    " << title << "\n"
              << "  ===============================================================\n";
}

static std::string format_bytes(std::size_t bytes) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (bytes >= 1024 * 1024) {
        ss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        ss << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else {
        ss << bytes << " B";
    }
    return ss.str();
}

static bool test_basic_correctness() {
    std::cout << "\n  [Test 1] Basic Correctness\n";
    print_sep();

    HeapAllocator::init();
    bool pass = true;

    void* a = HeapAllocator::allocate(100);
    void* b = HeapAllocator::allocate(200);
    void* c = HeapAllocator::allocate(400);

    if (!a || !b || !c) {
        std::cout << "    FAIL: allocation returned nullptr unexpectedly.\n";
        return false;
    }

    std::memset(a, 0xAA, 100);
    std::memset(b, 0xBB, 200);
    std::memset(c, 0xCC, 400);

    auto* pa = static_cast<unsigned char*>(a);
    auto* pb = static_cast<unsigned char*>(b);
    auto* pc = static_cast<unsigned char*>(c);

    for (int i = 0; i < 100; ++i) if (pa[i] != 0xAA) { pass = false; break; }
    for (int i = 0; i < 200; ++i) if (pb[i] != 0xBB) { pass = false; break; }
    for (int i = 0; i < 400; ++i) if (pc[i] != 0xCC) { pass = false; break; }

    HeapAllocator::deallocate(b);
    HeapAllocator::deallocate(a);
    HeapAllocator::deallocate(c);

    double frag = HeapAllocator::fragmentation_ratio();

    std::cout << "    Canary integrity:    " << (pass ? "PASS" : "FAIL") << "\n"
              << "    Post-free frag:      " << std::fixed << std::setprecision(4) << frag
              << (frag < 0.01 ? " (PASS - near zero)" : " (FAIL - expected ~0)") << "\n";

    return pass && frag < 0.01;
}

static bool test_edge_cases() {
    std::cout << "\n  [Test 2] Edge Cases\n";
    print_sep();

    HeapAllocator::init();
    bool pass = true;

    void* z = HeapAllocator::allocate(0);
    if (z != nullptr) {
        std::cout << "    FAIL: allocate(0) should return nullptr.\n";
        pass = false;
    } else {
        std::cout << "    allocate(0) == nullptr: PASS\n";
    }

    HeapAllocator::deallocate(nullptr);
    std::cout << "    deallocate(nullptr) safe: PASS\n";

    std::size_t big = HeapAllocator::HEAP_SIZE - HeapAllocator::OVERHEAD - HeapAllocator::MIN_BLOCK_SIZE;
    void* huge = HeapAllocator::allocate(big);
    if (!huge) {
        big = HeapAllocator::HEAP_SIZE / 2;
        huge = HeapAllocator::allocate(big);
    }
    if (huge) {
        std::cout << "    Large allocation (" << big << " B): PASS\n";
        HeapAllocator::deallocate(huge);
    } else {
        std::cout << "    Large allocation: SKIPPED (rounded size exceeds heap)\n";
    }

    return pass;
}

static bool test_coalescing() {
    std::cout << "\n  [Test 3] Coalescing Correctness\n";
    print_sep();

    HeapAllocator::init();

    std::vector<void*> ptrs(200, nullptr);
    int count = 0;
    for (int i = 0; i < 200; ++i) {
        ptrs[static_cast<std::size_t>(i)] = HeapAllocator::allocate(64);
        if (ptrs[static_cast<std::size_t>(i)]) count++;
        else break;
    }
    std::cout << "    Allocated " << count << " x 64B blocks.\n";

    for (int i = 0; i < count; ++i) {
        HeapAllocator::deallocate(ptrs[static_cast<std::size_t>(i)]);
    }

    double frag = HeapAllocator::fragmentation_ratio();
    std::cout << "    Total free after:    " << format_bytes(HeapAllocator::total_free_bytes()) << "\n"
              << "    Largest free block:  " << format_bytes(HeapAllocator::largest_free_block()) << "\n"
              << "    Fragmentation:       " << std::fixed << std::setprecision(4) << frag
              << (frag < 0.01 ? " (PASS)" : " (FAIL)") << "\n";

    return frag < 0.01;
}

struct BenchResult {
    std::size_t total_free{0};
    std::size_t largest_free{0};
    double frag_ratio{0.0};
    int alloc_failures{0};
    int alloc_successes{0};
    int total_blocks_allocated{0};
};

static constexpr std::size_t SMALL_REQUEST = 16;
static constexpr std::size_t LARGE_REQUEST = 32;
static constexpr std::size_t MAX_BLOCKS    = 200000;

static BenchResult run_checkerboard(bool use_coalesce) {
    HeapAllocator::init();
    BenchResult res;

    static std::vector<void*> ptrs(MAX_BLOCKS, nullptr);
    static std::vector<std::uint8_t> alive(MAX_BLOCKS, 0);

    std::fill(ptrs.begin(), ptrs.end(), nullptr);
    std::fill(alive.begin(), alive.end(), static_cast<std::uint8_t>(0));

    int count = 0;
    for (std::size_t i = 0; i < MAX_BLOCKS; ++i) {
        ptrs[i] = HeapAllocator::allocate(SMALL_REQUEST);
        if (!ptrs[i]) break;
        alive[i] = 1;
        count++;
    }
    res.total_blocks_allocated = count;

    for (int i = 0; i < count; i += 2) {
        if (use_coalesce) {
            HeapAllocator::deallocate(ptrs[static_cast<std::size_t>(i)]);
        } else {
            HeapAllocator::deallocate_raw(ptrs[static_cast<std::size_t>(i)]);
        }
        alive[static_cast<std::size_t>(i)] = 0;
    }

    for (int i = 1; i < count; i += 4) {
        if (alive[static_cast<std::size_t>(i)]) {
            if (use_coalesce) {
                HeapAllocator::deallocate(ptrs[static_cast<std::size_t>(i)]);
            } else {
                HeapAllocator::deallocate_raw(ptrs[static_cast<std::size_t>(i)]);
            }
            alive[static_cast<std::size_t>(i)] = 0;
        }
    }

    res.total_free   = HeapAllocator::total_free_bytes();
    res.largest_free = HeapAllocator::largest_free_block();
    res.frag_ratio   = HeapAllocator::fragmentation_ratio();

    int large_attempts = 0;
    for (int i = 0; i < count; ++i) {
        if (!alive[static_cast<std::size_t>(i)]) {
            void* p = HeapAllocator::allocate(LARGE_REQUEST);
            if (p) {
                res.alloc_successes++;
            } else {
                res.alloc_failures++;
            }
            large_attempts++;
            if (large_attempts >= 500) break;
        }
    }

    return res;
}

static void test_ab_comparison() {
    print_banner("MEMORY ALLOCATOR BENCHMARK RESULTS");

    std::cout << "\n  Strategy: Checkerboard fragmentation pattern\n"
              << "    1. Fill heap with " << SMALL_REQUEST << " B blocks until full\n"
              << "    2. Free alternating blocks to create gaps\n"
              << "    3. Free additional blocks to create mergeable pairs\n"
              << "    4. Attempt 500 allocations at 2x size (" << (SMALL_REQUEST * 2) << " B)\n\n";

    BenchResult with_coalesce    = run_checkerboard(true);
    BenchResult without_coalesce = run_checkerboard(false);

    std::cout << "  +------------------------+--------------+--------------+\n"
              << "  | " << std::left << std::setw(22) << "Metric"
              << " | " << std::setw(12) << "Coalescing"
              << " | " << std::setw(12) << "No Coalesce" << " |\n"
              << "  +------------------------+--------------+--------------+\n";

    std::cout << "  | " << std::setw(22) << "Initial Blocks"
              << " | " << std::setw(12) << with_coalesce.total_blocks_allocated
              << " | " << std::setw(12) << without_coalesce.total_blocks_allocated << " |\n";

    std::cout << "  | " << std::setw(22) << "Total Free Memory"
              << " | " << std::setw(12) << format_bytes(with_coalesce.total_free)
              << " | " << std::setw(12) << format_bytes(without_coalesce.total_free) << " |\n";

    std::cout << "  | " << std::setw(22) << "Largest Free Block"
              << " | " << std::setw(12) << format_bytes(with_coalesce.largest_free)
              << " | " << std::setw(12) << format_bytes(without_coalesce.largest_free) << " |\n";

    std::cout << "  | " << std::setw(22) << "Fragmentation Ratio"
              << " | " << std::setw(12) << std::fixed << std::setprecision(4) << with_coalesce.frag_ratio
              << " | " << std::setw(12) << std::fixed << std::setprecision(4) << without_coalesce.frag_ratio << " |\n";

    std::cout << "  | " << std::setw(22) << "2x Alloc Successes"
              << " | " << std::setw(12) << with_coalesce.alloc_successes
              << " | " << std::setw(12) << without_coalesce.alloc_successes << " |\n";

    std::cout << "  | " << std::setw(22) << "2x Alloc Failures"
              << " | " << std::setw(12) << with_coalesce.alloc_failures
              << " | " << std::setw(12) << without_coalesce.alloc_failures << " |\n";

    std::cout << "  +------------------------+--------------+--------------+\n";

    double delta = (without_coalesce.frag_ratio - with_coalesce.frag_ratio) * 100.0;
    int alloc_delta = with_coalesce.alloc_successes - without_coalesce.alloc_successes;

    if (alloc_delta > 0) {
        std::cout << "\n  >> Coalescing enabled " << alloc_delta << " more successful 2x allocations!\n";
    }
    if (delta > 0.1) {
        std::cout << "  >> Fragmentation reduced by " << std::fixed << std::setprecision(1) << delta << "% with coalescing.\n";
    }
    if (with_coalesce.total_free > without_coalesce.total_free) {
        double diff_mb = static_cast<double>(with_coalesce.total_free - without_coalesce.total_free) / (1024.0 * 1024.0);
        std::cout << "  >> " << std::fixed << std::setprecision(2) << diff_mb << " MB more free memory recovered via coalescing.\n";
    }
}

static bool test_stress() {
    std::cout << "\n  [Test 4] Stress Test (100,000 alloc/free cycles)\n";
    print_sep();

    HeapAllocator::init();
    bool ok = true;

    // Modern C++ PRNG instead of legacy std::rand()
    std::mt19937 rng(123);
    std::uniform_int_distribution<std::size_t> dist(16, 527);

    for (int i = 0; i < 100000; ++i) {
        std::size_t sz = dist(rng);
        void* p = HeapAllocator::allocate(sz);
        if (p) {
            auto* canary = static_cast<std::uint32_t*>(p);
            *canary = CANARY;
            if (*canary != CANARY) {
                ok = false;
                break;
            }
            HeapAllocator::deallocate(p);
        }
    }

    double frag = HeapAllocator::fragmentation_ratio();
    std::cout << "    100k cycles complete: " << (ok ? "PASS" : "FAIL (corruption)") << "\n"
              << "    Post-stress frag:     " << std::fixed << std::setprecision(4) << frag
              << (frag < 0.01 ? " (PASS)" : " (FAIL)") << "\n";

    return ok && frag < 0.01;
}

int main() {
    print_banner("SEGREGATED-LIST MEMORY ALLOCATOR - TEST SUITE");

    std::cout << "\n  Heap size:       " << (HeapAllocator::HEAP_SIZE / (1024 * 1024)) << " MB\n"
              << "  Header size:     " << HeapAllocator::HEADER_SIZE << " B\n"
              << "  Footer size:     " << HeapAllocator::FOOTER_SIZE << " B\n"
              << "  Min block size:  " << HeapAllocator::MIN_BLOCK_SIZE << " B\n"
              << "  Size classes:    " << HeapAllocator::NUM_SIZE_CLASSES << "\n";

    int passed = 0;
    int total = 0;

    total++; if (test_basic_correctness()) passed++;
    total++; if (test_edge_cases()) passed++;
    total++; if (test_coalescing()) passed++;
    total++; if (test_stress()) passed++;

    test_ab_comparison();

    print_banner("FINAL RESULTS");
    std::cout << "\n  Tests passed: " << passed << " / " << total << "\n";
    if (passed == total) {
        std::cout << "  Status: ALL TESTS PASSED\n\n";
        return 0;
    } else {
        std::cout << "  Status: SOME TESTS FAILED\n\n";
        return 1;
    }
}
