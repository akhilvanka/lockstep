#include <cstdint>
#include <vector>

#include "lockstep/rv64.h"

namespace lockstep {
namespace {

enum class Op : uint8_t {
    Bad, Lui, Auipc, Jal, Jalr,
    Beq, Bne, Blt, Bge, Bltu, Bgeu,
    Lb, Lh, Lw, Ld, Lbu, Lhu, Lwu,
    Sb, Sh, Sw, Sd,
    Addi, Slti, Sltiu, Xori, Ori, Andi, Slli, Srli, Srai,
    Addiw, Slliw, Srliw, Sraiw,
    Add, Sub, Sll, Slt, Sltu, Xor, Srl, Sra, Or, And,
    Addw, Subw, Sllw, Srlw, Sraw,
    Mul, Mulh, Mulhsu, Mulhu, Div, Divu, Rem, Remu,
    Mulw, Divw, Divuw, Remw, Remuw,
    Fence, Ecall, Ebreak,
};

struct Uop {
    Op op = Op::Bad;
    uint8_t rd = 0, rs1 = 0, rs2 = 0;
    int64_t imm = 0;
};

int64_t imm_i(uint32_t w) { return (int64_t)(int32_t)w >> 20; }
int64_t imm_s(uint32_t w) {
    return ((int64_t)(int32_t)w >> 25 << 5) | (int64_t)((w >> 7) & 0x1f);
}
int64_t imm_b(uint32_t w) {
    int64_t v = (int64_t)(int32_t)w >> 31 << 12;
    v |= (int64_t)((w >> 7) & 1) << 11;
    v |= (int64_t)((w >> 25) & 0x3f) << 5;
    v |= (int64_t)((w >> 8) & 0xf) << 1;
    return v;
}
int64_t imm_u(uint32_t w) { return (int64_t)(int32_t)(w & ~0xfffu); }
int64_t imm_j(uint32_t w) {
    int64_t v = (int64_t)(int32_t)w >> 31 << 20;
    v |= (int64_t)((w >> 12) & 0xff) << 12;
    v |= (int64_t)((w >> 20) & 1) << 11;
    v |= (int64_t)((w >> 21) & 0x3ff) << 1;
    return v;
}

Uop decode(uint32_t w) {
    Uop u;
    u.rd = (w >> 7) & 31;
    u.rs1 = (w >> 15) & 31;
    u.rs2 = (w >> 20) & 31;
    uint32_t f3 = (w >> 12) & 7, f7 = w >> 25;

    switch (w & 0x7f) {
        case 0x37: u.op = Op::Lui; u.imm = imm_u(w); break;
        case 0x17: u.op = Op::Auipc; u.imm = imm_u(w); break;
        case 0x6f: u.op = Op::Jal; u.imm = imm_j(w); break;
        case 0x67:
            if (f3 == 0) { u.op = Op::Jalr; u.imm = imm_i(w); }
            break;
        case 0x63: {
            static const Op tbl[8] = {Op::Beq, Op::Bne, Op::Bad, Op::Bad,
                                      Op::Blt, Op::Bge, Op::Bltu, Op::Bgeu};
            u.op = tbl[f3];
            u.imm = imm_b(w);
            break;
        }
        case 0x03: {
            static const Op tbl[8] = {Op::Lb, Op::Lh, Op::Lw, Op::Ld,
                                      Op::Lbu, Op::Lhu, Op::Lwu, Op::Bad};
            u.op = tbl[f3];
            u.imm = imm_i(w);
            break;
        }
        case 0x23: {
            static const Op tbl[8] = {Op::Sb, Op::Sh, Op::Sw, Op::Sd,
                                      Op::Bad, Op::Bad, Op::Bad, Op::Bad};
            u.op = tbl[f3];
            u.imm = imm_s(w);
            break;
        }
        case 0x13:
            switch (f3) {
                case 0: u.op = Op::Addi; u.imm = imm_i(w); break;
                case 2: u.op = Op::Slti; u.imm = imm_i(w); break;
                case 3: u.op = Op::Sltiu; u.imm = imm_i(w); break;
                case 4: u.op = Op::Xori; u.imm = imm_i(w); break;
                case 6: u.op = Op::Ori; u.imm = imm_i(w); break;
                case 7: u.op = Op::Andi; u.imm = imm_i(w); break;
                case 1:
                    if ((f7 & 0x7e) == 0) { u.op = Op::Slli; u.imm = (w >> 20) & 63; }
                    break;
                case 5:
                    if ((f7 & 0x7e) == 0) { u.op = Op::Srli; u.imm = (w >> 20) & 63; }
                    else if ((f7 & 0x7e) == 0x20) { u.op = Op::Srai; u.imm = (w >> 20) & 63; }
                    break;
            }
            break;
        case 0x1b:
            switch (f3) {
                case 0: u.op = Op::Addiw; u.imm = imm_i(w); break;
                case 1:
                    if (f7 == 0) { u.op = Op::Slliw; u.imm = u.rs2; }
                    break;
                case 5:
                    if (f7 == 0) { u.op = Op::Srliw; u.imm = u.rs2; }
                    else if (f7 == 0x20) { u.op = Op::Sraiw; u.imm = u.rs2; }
                    break;
            }
            break;
        case 0x33:
            if (f7 == 0) {
                static const Op tbl[8] = {Op::Add, Op::Sll, Op::Slt, Op::Sltu,
                                          Op::Xor, Op::Srl, Op::Or, Op::And};
                u.op = tbl[f3];
            } else if (f7 == 0x20) {
                if (f3 == 0) u.op = Op::Sub;
                else if (f3 == 5) u.op = Op::Sra;
            } else if (f7 == 1) {
                static const Op tbl[8] = {Op::Mul, Op::Mulh, Op::Mulhsu, Op::Mulhu,
                                          Op::Div, Op::Divu, Op::Rem, Op::Remu};
                u.op = tbl[f3];
            }
            break;
        case 0x3b:
            if (f7 == 0) {
                if (f3 == 0) u.op = Op::Addw;
                else if (f3 == 1) u.op = Op::Sllw;
                else if (f3 == 5) u.op = Op::Srlw;
            } else if (f7 == 0x20) {
                if (f3 == 0) u.op = Op::Subw;
                else if (f3 == 5) u.op = Op::Sraw;
            } else if (f7 == 1) {
                static const Op tbl[8] = {Op::Mulw, Op::Bad, Op::Bad, Op::Bad,
                                          Op::Divw, Op::Divuw, Op::Remw, Op::Remuw};
                u.op = tbl[f3];
            }
            break;
        case 0x0f: u.op = Op::Fence; break;
        case 0x73:
            if (w == 0x00000073) u.op = Op::Ecall;
            else if (w == 0x00100073) u.op = Op::Ebreak;
            break;
        default: break;
    }
    return u;
}

uint64_t mulhu64(uint64_t a, uint64_t b) {
    uint64_t al = (uint32_t)a, ah = a >> 32;
    uint64_t bl = (uint32_t)b, bh = b >> 32;
    uint64_t lo = al * bl;
    uint64_t m1 = ah * bl + (lo >> 32);
    uint64_t m2 = al * bh + (uint32_t)m1;
    return ah * bh + (m1 >> 32) + (m2 >> 32);
}
uint64_t mulh64(uint64_t a, uint64_t b) {
    uint64_t r = mulhu64(a, b);
    if ((int64_t)a < 0) r -= b;
    if ((int64_t)b < 0) r -= a;
    return r;
}
uint64_t mulhsu64(uint64_t a, uint64_t b) {
    uint64_t r = mulhu64(a, b);
    if ((int64_t)a < 0) r -= b;
    return r;
}

int64_t w32(uint64_t v) { return (int64_t)(int32_t)v; }

struct UopEngine final : Engine {
    const char* name() const override { return "uop"; }

    struct Slot {
        uint32_t raw = 0;
        bool valid = false;
        Uop u;
    };
    std::vector<Slot> cache_;

    const Uop& lookup(uint64_t pc, uint32_t raw) {
        size_t idx = (pc - kMemBase) / 4;
        if (cache_.size() <= idx) cache_.resize(idx + 1);
        Slot& s = cache_[idx];
        if (!s.valid || s.raw != raw) {
            s.raw = raw;
            s.u = decode(raw);
            s.valid = true;
        }
        return s.u;
    }

    static void fault(Hart& h, Trap t) {
        h.trap = t;
        h.trap_pc = h.pc;
        h.halted = true;
    }

    static uint64_t ld_bytes(const Hart& h, uint64_t addr, unsigned n) {
        uint64_t v = 0;
        for (unsigned i = 0; i < n; i++)
            v |= (uint64_t)h.mem[addr - kMemBase + i] << (8 * i);
        return v;
    }
    static void st_bytes(Hart& h, uint64_t addr, uint64_t v, unsigned n) {
        for (unsigned i = 0; i < n; i++)
            h.mem[addr - kMemBase + i] = (uint8_t)(v >> (8 * i));
    }

    bool do_load(Hart& h, const Uop& u, unsigned n, bool sign) {
        uint64_t addr = h.x[u.rs1] + u.imm;
        uint64_t v;
        if (h.in_ram(addr, n)) {
            v = ld_bytes(h, addr, n);
        } else if (addr == kMmioInstret && n == 8) {
            v = h.instret;
        } else {
            fault(h, Trap::BadLoad);
            return false;
        }
        if (sign && n < 8) {
            uint64_t bit = 1ULL << (8 * n - 1);
            v = (v ^ bit) - bit;
        }
        if (u.rd) h.x[u.rd] = v;
        return true;
    }

    bool do_store(Hart& h, const Uop& u, unsigned n) {
        uint64_t addr = h.x[u.rs1] + u.imm;
        uint64_t v = h.x[u.rs2];
        if (h.in_ram(addr, n)) {
            st_bytes(h, addr, v, n);
        } else if (addr == kMmioPutc) {
            h.out.push_back((char)v);
        } else if (addr == kMmioExit) {
            h.exit_code = (int)(int32_t)v;
            h.halted = true;
        } else {
            fault(h, Trap::BadStore);
            return false;
        }
        return true;
    }

    void run(Hart& h, uint64_t n) override {
        for (uint64_t i = 0; i < n && !h.halted; i++) {
            if ((h.pc & 3) || !h.in_ram(h.pc, 4)) { fault(h, Trap::BadFetch); return; }
            uint32_t raw = (uint32_t)ld_bytes(h, h.pc, 4);
            const Uop& u = lookup(h.pc, raw);

            uint64_t a = h.x[u.rs1], b = h.x[u.rs2];
            uint64_t next = h.pc + 4;
            uint64_t res;
            bool wr = true;

            switch (u.op) {
                case Op::Lui: res = (uint64_t)u.imm; break;
                case Op::Auipc: res = h.pc + u.imm; break;
                case Op::Jal: res = next; next = h.pc + u.imm; break;
                case Op::Jalr: res = next; next = (a + u.imm) & ~1ULL; break;

                case Op::Beq: wr = false; if (a == b) next = h.pc + u.imm; break;
                case Op::Bne: wr = false; if (a != b) next = h.pc + u.imm; break;
                case Op::Blt: wr = false; if ((int64_t)a < (int64_t)b) next = h.pc + u.imm; break;
                case Op::Bge: wr = false; if ((int64_t)a >= (int64_t)b) next = h.pc + u.imm; break;
                case Op::Bltu: wr = false; if (a < b) next = h.pc + u.imm; break;
                case Op::Bgeu: wr = false; if (a >= b) next = h.pc + u.imm; break;

                case Op::Lb: if (!do_load(h, u, 1, true)) return; wr = false; break;
                case Op::Lh: if (!do_load(h, u, 2, true)) return; wr = false; break;
                case Op::Lw: if (!do_load(h, u, 4, true)) return; wr = false; break;
                case Op::Ld: if (!do_load(h, u, 8, true)) return; wr = false; break;
                case Op::Lbu: if (!do_load(h, u, 1, false)) return; wr = false; break;
                case Op::Lhu: if (!do_load(h, u, 2, false)) return; wr = false; break;
                case Op::Lwu: if (!do_load(h, u, 4, false)) return; wr = false; break;

                case Op::Sb: if (!do_store(h, u, 1)) return; wr = false; break;
                case Op::Sh: if (!do_store(h, u, 2)) return; wr = false; break;
                case Op::Sw: if (!do_store(h, u, 4)) return; wr = false; break;
                case Op::Sd: if (!do_store(h, u, 8)) return; wr = false; break;

                case Op::Addi: res = a + u.imm; break;
                case Op::Slti: res = (int64_t)a < u.imm ? 1 : 0; break;
                case Op::Sltiu: res = a < (uint64_t)u.imm ? 1 : 0; break;
                case Op::Xori: res = a ^ u.imm; break;
                case Op::Ori: res = a | u.imm; break;
                case Op::Andi: res = a & u.imm; break;
                case Op::Slli: res = a << u.imm; break;
                case Op::Srli: res = a >> u.imm; break;
                case Op::Srai: res = (uint64_t)((int64_t)a >> u.imm); break;

                case Op::Addiw: res = w32(a + u.imm); break;
                case Op::Slliw: res = w32(a << u.imm); break;
                case Op::Srliw: res = w32((uint32_t)a >> u.imm); break;
                case Op::Sraiw: res = w32((uint64_t)((int32_t)a >> u.imm)); break;

                case Op::Add: res = a + b; break;
                case Op::Sub: res = a - b; break;
                case Op::Sll: res = a << (b & 63); break;
                case Op::Slt: res = (int64_t)a < (int64_t)b ? 1 : 0; break;
                case Op::Sltu: res = a < b ? 1 : 0; break;
                case Op::Xor: res = a ^ b; break;
                case Op::Srl: res = a >> (b & 63); break;
                case Op::Sra: res = (uint64_t)((int64_t)a >> (b & 63)); break;
                case Op::Or: res = a | b; break;
                case Op::And: res = a & b; break;

                case Op::Addw: res = w32(a + b); break;
                case Op::Subw: res = w32(a - b); break;
                case Op::Sllw: res = w32(a << (b & 31)); break;
                case Op::Srlw: res = w32((uint32_t)a >> (b & 31)); break;
                case Op::Sraw: res = w32((uint64_t)((int32_t)a >> (b & 31))); break;

                case Op::Mul: res = a * b; break;
                case Op::Mulh: res = mulh64(a, b); break;
                case Op::Mulhsu: res = mulhsu64(a, b); break;
                case Op::Mulhu: res = mulhu64(a, b); break;
                case Op::Div:
                    if (b == 0) res = ~0ULL;
                    else if (a == 0x8000000000000000ULL && b == ~0ULL) res = a;
                    else res = (uint64_t)((int64_t)a / (int64_t)b);
                    break;
                case Op::Divu: res = b == 0 ? ~0ULL : a / b; break;
                case Op::Rem:
                    if (b == 0) res = a;
                    else if (a == 0x8000000000000000ULL && b == ~0ULL) res = 0;
                    else res = (uint64_t)((int64_t)a % (int64_t)b);
                    break;
                case Op::Remu: res = b == 0 ? a : a % b; break;

                case Op::Mulw: res = w32((uint32_t)a * (uint32_t)b); break;
                case Op::Divw: {
                    int32_t x = (int32_t)a, y = (int32_t)b;
                    if (y == 0) res = ~0ULL;
                    else if (x == INT32_MIN && y == -1) res = (uint64_t)(int64_t)x;
                    else res = (uint64_t)(int64_t)(x / y);
                    break;
                }
                case Op::Divuw: {
                    uint32_t x = (uint32_t)a, y = (uint32_t)b;
                    res = y == 0 ? ~0ULL : w32(x / y);
                    break;
                }
                case Op::Remw: {
                    int32_t x = (int32_t)a, y = (int32_t)b;
                    if (y == 0) res = (uint64_t)(int64_t)x;
                    else if (x == INT32_MIN && y == -1) res = 0;
                    else res = (uint64_t)(int64_t)(x % y);
                    break;
                }
                case Op::Remuw: {
                    uint32_t x = (uint32_t)a, y = (uint32_t)b;
                    res = y == 0 ? w32(x) : w32(x % y);
                    break;
                }

                case Op::Fence: wr = false; break;
                case Op::Ecall: fault(h, Trap::Ecall); return;
                case Op::Ebreak: fault(h, Trap::Ebreak); return;
                case Op::Bad: fault(h, Trap::BadInsn); return;
            }

            if (wr && u.rd) h.x[u.rd] = res;
            h.pc = next;
            h.instret++;
        }
    }
};

}

std::unique_ptr<Engine> make_uop_engine() {
    return std::make_unique<UopEngine>();
}

}
