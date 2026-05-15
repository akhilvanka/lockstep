#include "lockstep/rv64.h"

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

uint64_t digest(const Hart& h) {
    uint64_t d = 1469598103934665603ULL;
    fnv(d, &h.pc, 8);
    fnv(d, &h.x[1], 8 * 31);
    fnv(d, &h.instret, 8);
    fnv(d, h.mem.data(), h.mem.size());
    uint64_t olen = h.out.size();
    fnv(d, &olen, 8);
    fnv(d, h.out.data(), h.out.size());
    uint8_t tail[3] = {(uint8_t)h.halted, (uint8_t)h.trap, (uint8_t)h.exit_code};
    fnv(d, tail, 3);
    return d;
}

}
