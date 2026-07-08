# Segregated-List Memory Allocator (C++17)

A low-level custom memory allocator implemented in modern C++17, operating within a statically allocated 10 MB virtual memory pool. The allocator operates independently without relying on standard library memory management routines (`std::malloc`, `std::free`, or OS system calls like `brk`/`sbrk`).

## Architecture Overview

### Block Structure
Each block consists of a header, usable payload, and boundary-tag footer:

```
+--------------------+-----------------------------------+--------------------+
|    BlockHeader     |          Payload (User)           |    BlockFooter     |
|     (32 bytes)     |                                   |     (16 bytes)     |
+--------------------+-----------------------------------+--------------------+
          ^                            ^
          |                            +-- Pointer returned to caller
          +-- Metadata: size, is_free flag, next/prev free list pointers
```

### Segregated Free Lists
Free blocks are grouped into 17 size classes based on power-of-two alignment thresholds (from 64 B up to 4 MB+):

- **Class 0**: `[64 B]`
- **Class 1**: `[128 B]`
- **Class 2**: `[256 B]`
- ...
- **Class 16**: `[4 MB+]` (catch-all for large blocks)

## Key Technical Features

- **Virtual Heap Pool**: 10 MB static byte array (`alignas(16)`) eliminating runtime heap overhead.
- **Boundary-Tag Coalescing**: Uses block footers mirroring header metadata for $O(1)$ bidirectional neighbor inspection during deallocation.
- **Segregated First-Fit**: $O(1)$ size class lookup with first-fit searching within list buckets.
- **Block Splitting**: Allocations exceeding requested sizes split leftover space into new free blocks when above minimum threshold ($64\text{ B}$).

## Modern C++ Features

| Feature | Usage |
|---------|-------|
| `constexpr` | Compile-time power-of-two block size rounding with `round_up_pow2()` |
| `[[nodiscard]]` | Applied to `allocate()`, `total_free_bytes()`, `largest_free_block()`, `fragmentation_ratio()` to prevent silent discard of critical return values |
| `std::array` | Type-safe segregated free list bucket storage replacing raw C-arrays |
| `static_assert` | Compile-time validation of metadata layout constraints (overhead < min block, alignment) |
| `alignas(16)` | Explicit 16-byte alignment guarantee on the static heap pool |
| `reinterpret_cast` | Type-safe boundary arithmetic for header/footer/payload traversal |
| Deleted constructors | `HeapAllocator` enforced as a non-instantiable static singleton class |
| `std::mt19937` | Mersenne Twister PRNG for reproducible stress testing (replaces legacy `rand()`) |

## Project Structure

```
Os_project/
├── Makefile
├── README.md
├── include/
│   └── allocator.hpp       # Struct definitions & HeapAllocator interface
└── src/
    ├── allocator.cpp       # Memory pool, size classes, coalescing implementation
    └── main.cpp            # Verification test suite & A/B benchmark driver
```

## Building & Running

### Requirements
- C++17 compatible compiler (`g++` or `clang++`)

### Build Command
```bash
g++ -Wall -Wextra -Wpedantic -O2 -std=c++17 -Iinclude -o allocator.exe src/allocator.cpp src/main.cpp
```

Or using Make (if installed):
```bash
make
make run    # build and run
```

### Run Tests and Benchmarks
In PowerShell:
```powershell
.\allocator.exe
```

In Command Prompt / CMD:
```cmd
allocator.exe
```

## Test Suite & Benchmarks

| Component | Coverage / Description |
|-----------|------------------------|
| **Test 1: Basic Correctness** | Validates allocations, canary pattern integrity (`0xAA`, `0xBB`, `0xCC`), non-sequential deallocations, and post-free heap restoration. |
| **Test 2: Edge Cases** | Verifies `allocate(0)` returns `nullptr`, `deallocate(nullptr)` safety, and max size handling. |
| **Test 3: Coalescing Correctness** | Allocates 200 blocks, frees all, and checks that boundary tag coalescing merges memory back to full heap capacity. |
| **Test 4: Stress Test** | 100,000 randomized allocation/free iterations verified against canary data corruption. |
| **A/B Benchmark** | Simulates a checkerboard fragmentation pattern to measure performance delta between coalescing vs. non-coalescing strategy. |

### Benchmark Highlights

```
  +------------------------+--------------+--------------+
  | Metric                 | Coalescing   | No Coalesce  |
  +------------------------+--------------+--------------+
  | Initial Blocks         | 163840       | 163840       |
  | Total Free Memory      | 5.62 MB      | 1.88 MB      |
  | Largest Free Block     | 144 B        | 16 B         |
  | 2x Alloc Successes     | 500          | 0            |
  | 2x Alloc Failures      | 0            | 500          |
  +------------------------+--------------+--------------+
```
Without coalescing, standard fragmentation prevents 2x allocation requests from succeeding. Coalescing recovers $3.75\text{ MB}$ of memory and allows $100\%$ of secondary allocations to succeed.
