#include "common/refs_data.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << 26)
#endif
#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000
#endif

namespace rinha {

const int16_t*  g_refs_ptr   = rinha_refs_bin_start;
const uint8_t*  g_labels_ptr = rinha_labels_bin_start;
const uint16_t* g_norms_ptr  = rinha_norms_bin_start;

namespace {

constexpr size_t kHugePage = 2u * 1024u * 1024u;

inline size_t align_up(size_t v, size_t a) noexcept {
    return (v + (a - 1)) & ~(a - 1);
}

template <typename T>
bool relocate_to_hugepage(const T*& slot, const T* src, size_t bytes) noexcept {
    const size_t mapping = align_up(bytes, kHugePage);
    void* p = ::mmap(nullptr, mapping,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB,
                     -1, 0);
    if (p == MAP_FAILED) {
        ::madvise(const_cast<T*>(src), bytes, MADV_HUGEPAGE);
        ::madvise(const_cast<T*>(src), bytes, MADV_WILLNEED);
        return false;
    }

    std::memcpy(p, src, bytes);
    volatile uint8_t sink = 0;
    for (size_t off = 0; off < bytes; off += 4096) {
        sink ^= static_cast<const uint8_t*>(p)[off];
    }
    (void)sink;

    ::mprotect(p, mapping, PROT_READ);
    ::madvise(p, mapping, MADV_WILLNEED);

    slot = static_cast<const T*>(p);
    return true;
}

}

void init_refs_residency() noexcept {
    if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0 && errno != EPERM) {
        std::fprintf(stderr, "[residency] mlockall: %s\n", std::strerror(errno));
    }

    const size_t refs_bytes   = static_cast<size_t>(rinha_refs_bin_end - rinha_refs_bin_start)
                              * sizeof(int16_t);
    const size_t labels_bytes = static_cast<size_t>(rinha_labels_bin_end - rinha_labels_bin_start);
    const size_t norms_bytes  = static_cast<size_t>(rinha_norms_bin_end - rinha_norms_bin_start)
                              * sizeof(uint16_t);

    relocate_to_hugepage(g_refs_ptr, rinha_refs_bin_start, refs_bytes);

    ::madvise(const_cast<uint8_t*>(rinha_labels_bin_start), labels_bytes, MADV_WILLNEED);
    ::madvise(const_cast<uint16_t*>(rinha_norms_bin_start), norms_bytes,  MADV_WILLNEED);
}

}
