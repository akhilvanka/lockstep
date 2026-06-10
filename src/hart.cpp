#include "lockstep/rv64.h"

#include <algorithm>

namespace lockstep {

const char* trap_name(Trap t) {
    switch (t) {
        case Trap::None: return "none";
        case Trap::BadInsn: return "bad-insn";
        case Trap::BadFetch: return "bad-fetch";
        case Trap::BadLoad: return "bad-load";
        case Trap::BadStore: return "bad-store";
        case Trap::Ecall: return "ecall";
        case Trap::Ebreak: return "ebreak";
    }
    return "?";
}

static inline void fnv(uint64_t& h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
}

static uint64_t hash_page(const Hart& h, size_t p) {
    size_t at = p * Hart::kPageBytes;
    size_t n = std::min(Hart::kPageBytes, h.mem.size() - at);
    uint64_t d = 1469598103934665603ULL;
    fnv(d, h.mem.data() + at, n);
    return d;
}

uint64_t digest(const Hart& h) {
    size_t pages = (h.mem.size() + Hart::kPageBytes - 1) / Hart::kPageBytes;
    if (h.page_hash.size() != pages) {
        h.page_hash.assign(pages, 0);
        h.page_dirty.assign(pages, 0);
        for (size_t p = 0; p < pages; p++)
            h.page_hash[p] = hash_page(h, p);
    } else {
        for (size_t p = 0; p < pages; p++)
            if (h.page_dirty[p]) {
                h.page_hash[p] = hash_page(h, p);
                h.page_dirty[p] = 0;
            }
    }
    uint64_t d = 1469598103934665603ULL;
    fnv(d, &h.pc, 8);
    fnv(d, &h.x[1], 8 * 31);
    fnv(d, &h.instret, 8);
    fnv(d, h.page_hash.data(), 8 * h.page_hash.size());
    uint64_t olen = h.out.size();
    fnv(d, &olen, 8);
    fnv(d, h.out.data(), h.out.size());
    uint8_t tail[3] = {(uint8_t)h.halted, (uint8_t)h.trap, (uint8_t)h.exit_code};
    fnv(d, tail, 3);
    return d;
}

}
