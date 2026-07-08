#ifndef ALLOCATOR_HPP
#define ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <array>

struct BlockFooter;

struct BlockHeader {
    std::size_t size;
    bool is_free;
    BlockHeader* next;
    BlockHeader* prev;

    [[nodiscard]] BlockFooter* footer() const;
    [[nodiscard]] void* payload() const;
    [[nodiscard]] static BlockHeader* from_payload(void* ptr);
};

struct BlockFooter {
    std::size_t size;
    bool is_free;
};

// Compile-time layout validation
static_assert(sizeof(BlockFooter) <= 16, "BlockFooter exceeds expected size");

inline BlockFooter* BlockHeader::footer() const {
    return reinterpret_cast<BlockFooter*>(
        reinterpret_cast<char*>(const_cast<BlockHeader*>(this)) + size - sizeof(BlockFooter)
    );
}

inline void* BlockHeader::payload() const {
    return reinterpret_cast<void*>(
        reinterpret_cast<char*>(const_cast<BlockHeader*>(this)) + sizeof(BlockHeader)
    );
}

inline BlockHeader* BlockHeader::from_payload(void* ptr) {
    if (!ptr) return nullptr;
    return reinterpret_cast<BlockHeader*>(
        reinterpret_cast<char*>(ptr) - sizeof(BlockHeader)
    );
}

class HeapAllocator {
public:
    static constexpr std::size_t HEAP_SIZE        = 10 * 1024 * 1024; // 10 MB virtual heap
    static constexpr std::size_t ALIGNMENT        = 8;
    static constexpr std::size_t MIN_BLOCK_SIZE   = 64;
    static constexpr std::size_t NUM_SIZE_CLASSES = 17;

    static constexpr std::size_t HEADER_SIZE = sizeof(BlockHeader);
    static constexpr std::size_t FOOTER_SIZE = sizeof(BlockFooter);
    static constexpr std::size_t OVERHEAD    = HEADER_SIZE + FOOTER_SIZE;

    // Singleton static class — prevent instantiation, copy, and move
    HeapAllocator() = delete;
    ~HeapAllocator() = delete;
    HeapAllocator(const HeapAllocator&) = delete;
    HeapAllocator& operator=(const HeapAllocator&) = delete;
    HeapAllocator(HeapAllocator&&) = delete;
    HeapAllocator& operator=(HeapAllocator&&) = delete;

    static void init();

    [[nodiscard]] static void* allocate(std::size_t size);
    static void deallocate(void* ptr);
    static void deallocate_raw(void* ptr); // Free without coalescing (for benchmarks)

    [[nodiscard]] static std::size_t total_free_bytes();
    [[nodiscard]] static std::size_t largest_free_block();
    [[nodiscard]] static double fragmentation_ratio();
    static void dump_layout();

private:
    [[nodiscard]] static constexpr std::size_t round_up_pow2(std::size_t x);
    [[nodiscard]] static int size_class_index(std::size_t size);
    static void write_footer(BlockHeader* hdr);
    static void list_remove(BlockHeader* hdr);
    static void list_insert(BlockHeader* hdr);

    alignas(16) static char m_heap[HEAP_SIZE];
    static std::array<BlockHeader*, NUM_SIZE_CLASSES> m_free_lists;
};

// constexpr power-of-two rounding (available at compile time)
constexpr std::size_t HeapAllocator::round_up_pow2(std::size_t x) {
    if (x <= MIN_BLOCK_SIZE) return MIN_BLOCK_SIZE;

    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if UINTPTR_MAX > 0xFFFFFFFF
    x |= x >> 32;
#endif
    x++;
    return x;
}

#endif // ALLOCATOR_HPP
