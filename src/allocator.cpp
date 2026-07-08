#include "allocator.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <iomanip>

alignas(16) char HeapAllocator::m_heap[HeapAllocator::HEAP_SIZE];
std::array<BlockHeader*, HeapAllocator::NUM_SIZE_CLASSES> HeapAllocator::m_free_lists;

// Compile-time layout assertions
static_assert(HeapAllocator::OVERHEAD < HeapAllocator::MIN_BLOCK_SIZE,
              "Metadata overhead must be smaller than minimum block size");
static_assert(HeapAllocator::HEAP_SIZE % HeapAllocator::ALIGNMENT == 0,
              "Heap size must be aligned");
static_assert(HeapAllocator::MIN_BLOCK_SIZE >= HeapAllocator::OVERHEAD,
              "Minimum block must accommodate header + footer");


int HeapAllocator::size_class_index(std::size_t size) {
    int idx = 0;
    std::size_t s = size >> 7;
    while (s) {
        idx++;
        s >>= 1;
    }
    if (idx >= static_cast<int>(NUM_SIZE_CLASSES)) {
        idx = static_cast<int>(NUM_SIZE_CLASSES) - 1;
    }
    return idx;
}

void HeapAllocator::write_footer(BlockHeader* hdr) {
    BlockFooter* ftr = hdr->footer();
    ftr->size = hdr->size;
    ftr->is_free = hdr->is_free;
}

void HeapAllocator::list_remove(BlockHeader* hdr) {
    int idx = size_class_index(hdr->size);

    if (hdr->prev) {
        hdr->prev->next = hdr->next;
    } else {
        m_free_lists[static_cast<std::size_t>(idx)] = hdr->next;
    }

    if (hdr->next) {
        hdr->next->prev = hdr->prev;
    }

    hdr->next = nullptr;
    hdr->prev = nullptr;
}

void HeapAllocator::list_insert(BlockHeader* hdr) {
    int idx = size_class_index(hdr->size);
    auto uidx = static_cast<std::size_t>(idx);

    hdr->prev = nullptr;
    hdr->next = m_free_lists[uidx];

    if (m_free_lists[uidx]) {
        m_free_lists[uidx]->prev = hdr;
    }

    m_free_lists[uidx] = hdr;
}

void HeapAllocator::init() {
    std::fill(std::begin(m_heap), std::end(m_heap), '\0');
    m_free_lists.fill(nullptr);

    auto* hdr = reinterpret_cast<BlockHeader*>(m_heap);
    hdr->size = HEAP_SIZE;
    hdr->is_free = true;
    hdr->next = nullptr;
    hdr->prev = nullptr;

    write_footer(hdr);
    list_insert(hdr);
}

void* HeapAllocator::allocate(std::size_t requested) {
    if (requested == 0) return nullptr;

    std::size_t total = round_up_pow2(requested + OVERHEAD);
    int idx = size_class_index(total);

    BlockHeader* blk = nullptr;
    for (int i = idx; i < static_cast<int>(NUM_SIZE_CLASSES); ++i) {
        BlockHeader* cur = m_free_lists[static_cast<std::size_t>(i)];
        while (cur) {
            if (cur->size >= total) {
                blk = cur;
                break;
            }
            cur = cur->next;
        }
        if (blk) break;
    }

    if (!blk) return nullptr;

    list_remove(blk);

    if (blk->size >= total + MIN_BLOCK_SIZE) {
        std::size_t remainder = blk->size - total;

        blk->size = total;
        write_footer(blk);

        auto* split = reinterpret_cast<BlockHeader*>(
            reinterpret_cast<char*>(blk) + total
        );
        split->size = remainder;
        split->is_free = true;
        split->next = nullptr;
        split->prev = nullptr;

        write_footer(split);
        list_insert(split);
    }

    blk->is_free = false;
    write_footer(blk);

    return blk->payload();
}

void HeapAllocator::deallocate(void* ptr) {
    if (!ptr) return;

    // Bounds check: verify pointer falls within heap range
    auto* raw = reinterpret_cast<char*>(ptr);
    assert(raw >= m_heap + HEADER_SIZE && raw < m_heap + HEAP_SIZE &&
           "deallocate: pointer is outside the managed heap");

    BlockHeader* hdr = BlockHeader::from_payload(ptr);
    hdr->is_free = true;

    // Coalesce right neighbor
    auto* next = reinterpret_cast<BlockHeader*>(
        reinterpret_cast<char*>(hdr) + hdr->size
    );
    if (reinterpret_cast<char*>(next) < m_heap + HEAP_SIZE && next->is_free) {
        list_remove(next);
        hdr->size += next->size;
    }

    // Coalesce left neighbor
    if (reinterpret_cast<char*>(hdr) > m_heap) {
        auto* prev_ftr = reinterpret_cast<BlockFooter*>(
            reinterpret_cast<char*>(hdr) - FOOTER_SIZE
        );
        if (prev_ftr->is_free) {
            auto* prev = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<char*>(hdr) - prev_ftr->size
            );
            list_remove(prev);
            prev->size += hdr->size;
            hdr = prev;
        }
    }

    hdr->is_free = true;
    write_footer(hdr);
    list_insert(hdr);
}

void HeapAllocator::deallocate_raw(void* ptr) {
    if (!ptr) return;

    BlockHeader* hdr = BlockHeader::from_payload(ptr);
    hdr->is_free = true;
    write_footer(hdr);
    list_insert(hdr);
}

std::size_t HeapAllocator::total_free_bytes() {
    std::size_t total = 0;
    const char* pos = m_heap;
    while (pos < m_heap + HEAP_SIZE) {
        auto* hdr = reinterpret_cast<const BlockHeader*>(pos);
        if (hdr->size == 0 || hdr->size > HEAP_SIZE) break;
        if (hdr->is_free) {
            total += hdr->size - OVERHEAD;
        }
        pos += hdr->size;
    }
    return total;
}

std::size_t HeapAllocator::largest_free_block() {
    std::size_t largest = 0;
    const char* pos = m_heap;
    while (pos < m_heap + HEAP_SIZE) {
        auto* hdr = reinterpret_cast<const BlockHeader*>(pos);
        if (hdr->size == 0 || hdr->size > HEAP_SIZE) break;
        if (hdr->is_free) {
            std::size_t payload = hdr->size - OVERHEAD;
            if (payload > largest) largest = payload;
        }
        pos += hdr->size;
    }
    return largest;
}

double HeapAllocator::fragmentation_ratio() {
    std::size_t total = total_free_bytes();
    std::size_t largest = largest_free_block();
    if (total == 0) return 0.0;
    return 1.0 - (static_cast<double>(largest) / static_cast<double>(total));
}

void HeapAllocator::dump_layout() {
    std::cout << "\n  " << std::left
              << std::setw(6)  << "Block"
              << std::setw(12) << "Offset"
              << std::setw(12) << "Size"
              << std::setw(8)  << "Status" << "\n";
    std::cout << "  "
              << std::setw(6)  << "-----"
              << std::setw(12) << "------"
              << std::setw(12) << "----"
              << std::setw(8)  << "------" << "\n";

    int n = 0;
    const char* pos = m_heap;
    while (pos < m_heap + HEAP_SIZE) {
        auto* hdr = reinterpret_cast<const BlockHeader*>(pos);
        if (hdr->size == 0 || hdr->size > HEAP_SIZE) break;
        std::cout << "  " << std::left
                  << std::setw(6)  << n++
                  << "0x" << std::hex << std::setw(10) << std::setfill('0')
                  << static_cast<std::uintptr_t>(pos - m_heap)
                  << std::dec << std::setfill(' ')
                  << std::setw(12) << hdr->size
                  << std::setw(8)  << (hdr->is_free ? "FREE" : "ALLOC") << "\n";
        pos += hdr->size;
    }
    std::cout << "\n";
}
