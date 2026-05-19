#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

namespace lockstep::enc {

inline uint32_t r_type(uint32_t op, uint32_t rd, uint32_t f3, uint32_t rs1,
                       uint32_t rs2, uint32_t f7) {
    return op | rd << 7 | f3 << 12 | rs1 << 15 | rs2 << 20 | f7 << 25;
}
inline uint32_t i_type(uint32_t op, uint32_t rd, uint32_t f3, uint32_t rs1,
                       int32_t imm) {
    return op | rd << 7 | f3 << 12 | rs1 << 15 | (uint32_t)(imm & 0xfff) << 20;
}
inline uint32_t s_type(uint32_t op, uint32_t f3, uint32_t rs1, uint32_t rs2,
                       int32_t imm) {
    return op | (uint32_t)(imm & 0x1f) << 7 | f3 << 12 | rs1 << 15 |
           rs2 << 20 | (uint32_t)((imm >> 5) & 0x7f) << 25;
}
inline uint32_t b_type(uint32_t f3, uint32_t rs1, uint32_t rs2, int32_t imm) {
    uint32_t u = (uint32_t)imm;
    return 0x63 | ((u >> 11) & 1) << 7 | ((u >> 1) & 0xf) << 8 | f3 << 12 |
           rs1 << 15 | rs2 << 20 | ((u >> 5) & 0x3f) << 25 |
           ((u >> 12) & 1) << 31;
}
inline uint32_t u_type(uint32_t op, uint32_t rd, int32_t imm20) {
    return op | rd << 7 | (uint32_t)imm20 << 12;
}
inline uint32_t j_type(uint32_t rd, int32_t imm) {
    uint32_t u = (uint32_t)imm;
    return 0x6f | rd << 7 | ((u >> 12) & 0xff) << 12 | ((u >> 11) & 1) << 20 |
           ((u >> 1) & 0x3ff) << 21 | ((u >> 20) & 1) << 31;
}

inline uint32_t LUI(uint32_t rd, int32_t imm20) { return u_type(0x37, rd, imm20); }
inline uint32_t AUIPC(uint32_t rd, int32_t imm20) { return u_type(0x17, rd, imm20); }
inline uint32_t JAL(uint32_t rd, int32_t off) { return j_type(rd, off); }
inline uint32_t JALR(uint32_t rd, uint32_t rs1, int32_t imm) { return i_type(0x67, rd, 0, rs1, imm); }

inline uint32_t BEQ(uint32_t a, uint32_t b, int32_t off) { return b_type(0, a, b, off); }
inline uint32_t BNE(uint32_t a, uint32_t b, int32_t off) { return b_type(1, a, b, off); }
inline uint32_t BLT(uint32_t a, uint32_t b, int32_t off) { return b_type(4, a, b, off); }
inline uint32_t BGE(uint32_t a, uint32_t b, int32_t off) { return b_type(5, a, b, off); }
inline uint32_t BLTU(uint32_t a, uint32_t b, int32_t off) { return b_type(6, a, b, off); }
inline uint32_t BGEU(uint32_t a, uint32_t b, int32_t off) { return b_type(7, a, b, off); }

inline uint32_t LB(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x03, rd, 0, rs1, i); }
inline uint32_t LH(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x03, rd, 1, rs1, i); }
inline uint32_t LW(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x03, rd, 2, rs1, i); }
inline uint32_t LD(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x03, rd, 3, rs1, i); }
inline uint32_t LBU(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x03, rd, 4, rs1, i); }
inline uint32_t LHU(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x03, rd, 5, rs1, i); }
inline uint32_t LWU(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x03, rd, 6, rs1, i); }

inline uint32_t SB(uint32_t rs1, uint32_t rs2, int32_t i) { return s_type(0x23, 0, rs1, rs2, i); }
inline uint32_t SH(uint32_t rs1, uint32_t rs2, int32_t i) { return s_type(0x23, 1, rs1, rs2, i); }
inline uint32_t SW(uint32_t rs1, uint32_t rs2, int32_t i) { return s_type(0x23, 2, rs1, rs2, i); }
inline uint32_t SD(uint32_t rs1, uint32_t rs2, int32_t i) { return s_type(0x23, 3, rs1, rs2, i); }

inline uint32_t ADDI(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x13, rd, 0, rs1, i); }
inline uint32_t SLTI(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x13, rd, 2, rs1, i); }
inline uint32_t SLTIU(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x13, rd, 3, rs1, i); }
inline uint32_t XORI(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x13, rd, 4, rs1, i); }
inline uint32_t ORI(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x13, rd, 6, rs1, i); }
inline uint32_t ANDI(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x13, rd, 7, rs1, i); }
inline uint32_t SLLI(uint32_t rd, uint32_t rs1, uint32_t sh) { return i_type(0x13, rd, 1, rs1, (int32_t)sh); }
inline uint32_t SRLI(uint32_t rd, uint32_t rs1, uint32_t sh) { return i_type(0x13, rd, 5, rs1, (int32_t)sh); }
inline uint32_t SRAI(uint32_t rd, uint32_t rs1, uint32_t sh) { return i_type(0x13, rd, 5, rs1, (int32_t)(sh | 0x400)); }

inline uint32_t ADD(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 0, a, b, 0); }
inline uint32_t SUB(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 0, a, b, 0x20); }
inline uint32_t SLL(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 1, a, b, 0); }
inline uint32_t SLT(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 2, a, b, 0); }
inline uint32_t SLTU(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 3, a, b, 0); }
inline uint32_t XOR(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 4, a, b, 0); }
inline uint32_t SRL(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 5, a, b, 0); }
inline uint32_t SRA(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 5, a, b, 0x20); }
inline uint32_t OR(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 6, a, b, 0); }
inline uint32_t AND(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 7, a, b, 0); }

inline uint32_t ADDIW(uint32_t rd, uint32_t rs1, int32_t i) { return i_type(0x1b, rd, 0, rs1, i); }
inline uint32_t SLLIW(uint32_t rd, uint32_t rs1, uint32_t sh) { return i_type(0x1b, rd, 1, rs1, (int32_t)sh); }
inline uint32_t SRLIW(uint32_t rd, uint32_t rs1, uint32_t sh) { return i_type(0x1b, rd, 5, rs1, (int32_t)sh); }
inline uint32_t SRAIW(uint32_t rd, uint32_t rs1, uint32_t sh) { return i_type(0x1b, rd, 5, rs1, (int32_t)(sh | 0x400)); }
inline uint32_t ADDW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 0, a, b, 0); }
inline uint32_t SUBW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 0, a, b, 0x20); }
inline uint32_t SLLW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 1, a, b, 0); }
inline uint32_t SRLW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 5, a, b, 0); }
inline uint32_t SRAW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 5, a, b, 0x20); }

inline uint32_t MUL(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 0, a, b, 1); }
inline uint32_t MULH(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 1, a, b, 1); }
inline uint32_t MULHSU(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 2, a, b, 1); }
inline uint32_t MULHU(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 3, a, b, 1); }
inline uint32_t DIV(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 4, a, b, 1); }
inline uint32_t DIVU(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 5, a, b, 1); }
inline uint32_t REM(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 6, a, b, 1); }
inline uint32_t REMU(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x33, rd, 7, a, b, 1); }
inline uint32_t MULW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 0, a, b, 1); }
inline uint32_t DIVW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 4, a, b, 1); }
inline uint32_t DIVUW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 5, a, b, 1); }
inline uint32_t REMW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 6, a, b, 1); }
inline uint32_t REMUW(uint32_t rd, uint32_t a, uint32_t b) { return r_type(0x3b, rd, 7, a, b, 1); }

inline uint32_t ECALL() { return 0x73; }
inline uint32_t EBREAK() { return 0x00100073; }
inline uint32_t FENCE() { return 0x0ff0000f; }
inline uint32_t NOP() { return ADDI(0, 0, 0); }

inline void li32(std::vector<uint32_t>& v, uint32_t rd, int32_t val) {
    int32_t hi = (val + 0x800) >> 12;
    int32_t lo = val - (hi << 12);
    if (hi != 0) {
        v.push_back(LUI(rd, hi));
        if (lo != 0) v.push_back(ADDIW(rd, rd, lo));
    } else {
        v.push_back(ADDI(rd, 0, lo));
    }
}

class Asm {
  public:
    using Label = int;

    Label label() {
        labels_.push_back(-1);
        return (int)labels_.size() - 1;
    }
    void bind(Label l) { labels_[l] = (int)code_.size(); }
    Label here() {
        Label l = label();
        bind(l);
        return l;
    }

    void emit(uint32_t insn) { code_.push_back(insn); }
    void li(uint32_t rd, int32_t val) { li32(code_, rd, val); }

    void beq(uint32_t a, uint32_t b, Label l) { fix_b(0, a, b, l); }
    void bne(uint32_t a, uint32_t b, Label l) { fix_b(1, a, b, l); }
    void blt(uint32_t a, uint32_t b, Label l) { fix_b(4, a, b, l); }
    void bge(uint32_t a, uint32_t b, Label l) { fix_b(5, a, b, l); }
    void bltu(uint32_t a, uint32_t b, Label l) { fix_b(6, a, b, l); }
    void bgeu(uint32_t a, uint32_t b, Label l) { fix_b(7, a, b, l); }
    void jal(uint32_t rd, Label l) {
        fixups_.push_back({(int)code_.size(), l, true, 0, rd, 0});
        code_.push_back(0);
    }

    std::vector<uint32_t> finish() {
        for (const auto& f : fixups_) {
            assert(labels_[f.target] >= 0 && "unbound label");
            int32_t off = (labels_[f.target] - f.at) * 4;
            code_[f.at] = f.is_jal ? JAL(f.a, off) : b_type(f.f3, f.a, f.b, off);
        }
        fixups_.clear();
        return code_;
    }

  private:
    struct Fixup {
        int at;
        Label target;
        bool is_jal;
        uint32_t f3, a, b;
    };
    void fix_b(uint32_t f3, uint32_t a, uint32_t b, Label l) {
        fixups_.push_back({(int)code_.size(), l, false, f3, a, b});
        code_.push_back(0);
    }
    std::vector<uint32_t> code_;
    std::vector<int> labels_;
    std::vector<Fixup> fixups_;
};

}
