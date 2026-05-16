#include <cstdint>

#include "lockstep/rv64.h"

namespace lockstep {
namespace {

inline int64_t sx32(uint64_t v) { return (int64_t)(int32_t)(uint32_t)v; }

struct SwitchEngine final : Engine {
    const char* name() const override { return "switch"; }

    void run(Hart& h, uint64_t n) override {
        for (uint64_t i = 0; i < n && !h.halted; i++) step(h);
    }

    static void fault(Hart& h, Trap t) {
        h.trap = t;
        h.trap_pc = h.pc;
        h.halted = true;
    }

    static void step(Hart& h) {
        if ((h.pc & 3) || !h.in_ram(h.pc, 4)) return fault(h, Trap::BadFetch);
        uint32_t insn = (uint32_t)h.load_ram(h.pc, 4);

        uint32_t op = insn & 0x7f;
        uint32_t rd = (insn >> 7) & 31;
        uint32_t f3 = (insn >> 12) & 7;
        uint32_t rs1 = (insn >> 15) & 31;
        uint32_t rs2 = (insn >> 20) & 31;
        uint32_t f7 = insn >> 25;
        int64_t imm_i = (int32_t)insn >> 20;
        uint64_t a = h.x[rs1], b = h.x[rs2];

        uint64_t next = h.pc + 4;
        uint64_t res = 0;
        bool wb = false;

        switch (op) {
            case 0x37: res = (int64_t)(int32_t)(insn & 0xfffff000); wb = true; break;
            case 0x17: res = h.pc + (int64_t)(int32_t)(insn & 0xfffff000); wb = true; break;
            case 0x6f: {
                int64_t off = ((int64_t)((int32_t)insn >> 31) << 20) |
                              (int64_t)(((insn >> 12) & 0xff) << 12) |
                              (int64_t)(((insn >> 20) & 1) << 11) |
                              (int64_t)(((insn >> 21) & 0x3ff) << 1);
                res = next;
                next = h.pc + off;
                wb = true;
                break;
            }
            case 0x67: {
                if (f3 != 0) return fault(h, Trap::BadInsn);
                res = next;
                next = (a + imm_i) & ~1ULL;
                wb = true;
                break;
            }
            case 0x63: {
                int64_t off = ((int64_t)((int32_t)insn >> 31) << 12) |
                              (int64_t)(((insn >> 7) & 1) << 11) |
                              (int64_t)(((insn >> 25) & 0x3f) << 5) |
                              (int64_t)(((insn >> 8) & 0xf) << 1);
                bool take;
                switch (f3) {
                    case 0: take = a == b; break;
                    case 1: take = a != b; break;
                    case 4: take = (int64_t)a < (int64_t)b; break;
                    case 5: take = (int64_t)a >= (int64_t)b; break;
                    case 6: take = a < b; break;
                    case 7: take = a >= b; break;
                    default: return fault(h, Trap::BadInsn);
                }
                if (take) next = h.pc + off;
                break;
            }
            case 0x03: {
                uint64_t addr = a + imm_i;
                static const unsigned width[8] = {1, 2, 4, 8, 1, 2, 4, 0};
                unsigned w = width[f3];
                if (w == 0) return fault(h, Trap::BadInsn);
                if (h.in_ram(addr, w)) {
                    res = h.load_ram(addr, w);
                } else if (addr == kMmioInstret && w == 8) {
                    res = h.instret;
                } else {
                    return fault(h, Trap::BadLoad);
                }
                if (f3 < 4) {
                    int shift = 64 - 8 * w;
                    res = (uint64_t)((int64_t)(res << shift) >> shift);
                }
                wb = true;
                break;
            }
            case 0x23: {
                if (f3 > 3) return fault(h, Trap::BadInsn);
                unsigned w = 1u << f3;
                int64_t imm_s = (((int32_t)(insn & 0xfe000000)) >> 20) |
                                (int64_t)((insn >> 7) & 0x1f);
                uint64_t addr = a + imm_s;
                if (h.in_ram(addr, w)) {
                    h.store_ram(addr, b, w);
                } else if (addr == kMmioPutc) {
                    h.out.push_back((char)(b & 0xff));
                } else if (addr == kMmioExit) {
                    h.exit_code = (int)(int32_t)b;
                    h.halted = true;
                } else {
                    return fault(h, Trap::BadStore);
                }
                break;
            }
            case 0x13: {
                wb = true;
                switch (f3) {
                    case 0: res = a + imm_i; break;
                    case 2: res = (int64_t)a < imm_i; break;
                    case 3: res = a < (uint64_t)imm_i; break;
                    case 4: res = a ^ imm_i; break;
                    case 6: res = a | imm_i; break;
                    case 7: res = a & imm_i; break;
                    case 1:
                        if (f7 >> 1) return fault(h, Trap::BadInsn);
                        res = a << (rs2 | (f7 & 1) << 5);
                        break;
                    case 5: {
                        uint32_t sh = rs2 | (f7 & 1) << 5;
                        if ((f7 & ~1u) == 0x20)
                            res = (uint64_t)((int64_t)a >> sh);
                        else if ((f7 & ~1u) == 0)
                            res = a >> sh;
                        else
                            return fault(h, Trap::BadInsn);
                        break;
                    }
                }
                break;
            }
            case 0x1b: {
                wb = true;
                switch (f3) {
                    case 0: res = sx32(a + imm_i); break;
                    case 1:
                        if (f7 != 0) return fault(h, Trap::BadInsn);
                        res = sx32((uint32_t)a << rs2);
                        break;
                    case 5:
                        if (f7 == 0x20)
                            res = sx32((uint64_t)((int32_t)(uint32_t)a >> rs2));
                        else if (f7 == 0)
                            res = sx32((uint32_t)a >> rs2);
                        else
                            return fault(h, Trap::BadInsn);
                        break;
                    default: return fault(h, Trap::BadInsn);
                }
                break;
            }
            case 0x33: {
                wb = true;
                if (f7 == 1) {
                    switch (f3) {
                        case 0: res = a * b; break;
                        case 1: res = (uint64_t)(((__int128)(int64_t)a * (int64_t)b) >> 64); break;
                        case 2: res = (uint64_t)(((__int128)(int64_t)a * (unsigned __int128)b) >> 64); break;
                        case 3: res = (uint64_t)(((unsigned __int128)a * b) >> 64); break;
                        case 4:
                            if (b == 0) res = ~0ULL;
                            else if ((int64_t)a == INT64_MIN && (int64_t)b == -1) res = a;
                            else res = (uint64_t)((int64_t)a / (int64_t)b);
                            break;
                        case 5: res = b ? a / b : ~0ULL; break;
                        case 6:
                            if (b == 0) res = a;
                            else if ((int64_t)a == INT64_MIN && (int64_t)b == -1) res = 0;
                            else res = (uint64_t)((int64_t)a % (int64_t)b);
                            break;
                        case 7: res = b ? a % b : a; break;
                    }
                } else if (f7 == 0 || f7 == 0x20) {
                    bool alt = f7 == 0x20;
                    switch (f3) {
                        case 0: res = alt ? a - b : a + b; break;
                        case 1: if (alt) return fault(h, Trap::BadInsn);
                                res = a << (b & 63); break;
                        case 2: if (alt) return fault(h, Trap::BadInsn);
                                res = (int64_t)a < (int64_t)b; break;
                        case 3: if (alt) return fault(h, Trap::BadInsn);
                                res = a < b; break;
                        case 4: if (alt) return fault(h, Trap::BadInsn);
                                res = a ^ b; break;
                        case 5: res = alt ? (uint64_t)((int64_t)a >> (b & 63))
                                          : a >> (b & 63); break;
                        case 6: if (alt) return fault(h, Trap::BadInsn);
                                res = a | b; break;
                        case 7: if (alt) return fault(h, Trap::BadInsn);
                                res = a & b; break;
                    }
                } else {
                    return fault(h, Trap::BadInsn);
                }
                break;
            }
            case 0x3b: {
                wb = true;
                uint32_t aw = (uint32_t)a, bw = (uint32_t)b;
                if (f7 == 1) {
                    switch (f3) {
                        case 0: res = sx32(aw * bw); break;
                        case 4:
                            if (bw == 0) res = ~0ULL;
                            else if ((int32_t)aw == INT32_MIN && (int32_t)bw == -1) res = sx32(aw);
                            else res = sx32((uint32_t)((int32_t)aw / (int32_t)bw));
                            break;
                        case 5: res = bw ? sx32(aw / bw) : ~0ULL; break;
                        case 6:
                            if (bw == 0) res = sx32(aw);
                            else if ((int32_t)aw == INT32_MIN && (int32_t)bw == -1) res = 0;
                            else res = sx32((uint32_t)((int32_t)aw % (int32_t)bw));
                            break;
                        case 7: res = bw ? sx32(aw % bw) : sx32(aw); break;
                        default: return fault(h, Trap::BadInsn);
                    }
                } else if (f7 == 0 || f7 == 0x20) {
                    bool alt = f7 == 0x20;
                    switch (f3) {
                        case 0: res = sx32(alt ? aw - bw : aw + bw); break;
                        case 1: if (alt) return fault(h, Trap::BadInsn);
                                res = sx32(aw << (bw & 31)); break;
                        case 5: res = alt ? sx32((uint64_t)((int32_t)aw >> (bw & 31)))
                                          : sx32(aw >> (bw & 31)); break;
                        default: return fault(h, Trap::BadInsn);
                    }
                } else {
                    return fault(h, Trap::BadInsn);
                }
                break;
            }
            case 0x0f: break;
            case 0x73:
                if (insn == 0x73) return fault(h, Trap::Ecall);
                if (insn == 0x00100073) return fault(h, Trap::Ebreak);
                return fault(h, Trap::BadInsn);
            default: return fault(h, Trap::BadInsn);
        }

        if (wb && rd != 0) h.x[rd] = res;
        h.pc = next;
        h.instret++;
    }
};

}

std::unique_ptr<Engine> make_switch_engine() {
    return std::make_unique<SwitchEngine>();
}

}
