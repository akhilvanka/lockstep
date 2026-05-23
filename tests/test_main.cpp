#include <cstdio>
#include <cstring>
#include <random>
#include <thread>

#include "lockstep/encoder.h"
#include "lockstep/harness.h"

using namespace lockstep;
using namespace lockstep::enc;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond)                                                  \
    do {                                                             \
        g_checks++;                                                  \
        if (!(cond)) {                                               \
            g_fails++;                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        g_checks++;                                                           \
        auto va = (a);                                                        \
        auto vb = (decltype(va))(b);                                          \
        if (va != vb) {                                                       \
            g_fails++;                                                        \
            printf("FAIL %s:%d: %s == %s (%llx vs %llx)\n", __FILE__,         \
                   __LINE__, #a, #b, (unsigned long long)va,                  \
                   (unsigned long long)vb);                                   \
        }                                                                     \
    } while (0)

static Hart run_on(Engine& eng, const std::vector<uint32_t>& words,
                   uint64_t max = 100000) {
    Hart h(64 * 1024);
    h.load_image(words);
    eng.run(h, max);
    return h;
}

static void li64(Asm& a, uint32_t rd, uint64_t v) {
    a.li(rd, (int32_t)(v >> 32));
    a.emit(SLLI(rd, rd, 32));
    a.li(28, (int32_t)(uint32_t)v);
    a.emit(SLLI(28, 28, 32));
    a.emit(SRLI(28, 28, 32));
    a.emit(OR(rd, rd, 28));
}

static void test_encoder() {
    CHECK_EQ(ADDI(1, 0, 5), 0x00500093u);
    CHECK_EQ(ADD(3, 1, 2), 0x002081b3u);
    CHECK_EQ(LUI(5, 0x12345), 0x123452b7u);
    CHECK_EQ(SD(2, 8, 16), 0x00813823u);
    CHECK_EQ(LD(9, 2, 16), 0x01013483u);
    CHECK_EQ(MUL(4, 5, 6), 0x02628233u);
    CHECK_EQ(EBREAK(), 0x00100073u);

    CHECK_EQ(BEQ(1, 2, 8), 0x00208463u);

    CHECK_EQ(JAL(1, 2048), 0x001000efu);
}

struct Want {
    uint32_t reg;
    uint64_t val;
};

static void check_both(const std::vector<uint32_t>& prog,
                       const std::vector<Want>& wants) {
    auto ea = make_switch_engine();
    auto eb = make_uop_engine();
    Hart ha = run_on(*ea, prog);
    Hart hb = run_on(*eb, prog);
    CHECK(ha.halted && hb.halted);
    CHECK_EQ(digest(ha), digest(hb));
    for (const auto& w : wants) {
        CHECK_EQ(ha.x[w.reg], w.val);
        CHECK_EQ(hb.x[w.reg], w.val);
    }
}

static std::vector<uint32_t> with_setup(uint64_t a_val, uint64_t b_val,
                                        std::vector<uint32_t> body) {
    Asm a;
    li64(a, 1, a_val);
    li64(a, 2, b_val);
    for (uint32_t w : body) a.emit(w);
    a.emit(EBREAK());
    return a.finish();
}

static void test_alu() {

    {
        const uint64_t A = 0xdeadbeefcafebabeULL, B = 0x123456789abcdef0ULL;
        const uint64_t mul = A * B;
        const uint64_t mulh =
            (uint64_t)(((__int128)(int64_t)A * (int64_t)B) >> 64);
        const uint64_t mulhu = (uint64_t)(((unsigned __int128)A * B) >> 64);
        const uint64_t mulhsu = (uint64_t)(
            (unsigned __int128)((__int128)(int64_t)A) * B >> 64);
        check_both(with_setup(A, B,
                              {MUL(3, 1, 2), MULH(4, 1, 2), MULHU(5, 1, 2),
                               MULHSU(6, 1, 2)}),
                   {{3, mul}, {4, mulh}, {5, mulhu}, {6, mulhsu}});
    }

    check_both(with_setup(0x8000000000000000ULL, (uint64_t)-1,
                          {DIV(3, 1, 2), REM(4, 1, 2)}),
               {{3, 0x8000000000000000ULL}, {4, 0}});
    check_both(with_setup(42, 0,
                          {DIV(3, 1, 2), DIVU(4, 1, 2), REM(5, 1, 2),
                           REMU(6, 1, 2)}),
               {{3, ~0ULL}, {4, ~0ULL}, {5, 42}, {6, 42}});
    check_both(with_setup((uint64_t)-7, 2, {DIV(3, 1, 2), REM(4, 1, 2)}),
               {{3, (uint64_t)-3}, {4, (uint64_t)-1}});

    check_both(with_setup(0x00000000ffffffffULL, 1, {ADDW(3, 1, 2)}),
               {{3, 0}});
    check_both(with_setup(0x80000000ULL, 0, {ADDW(3, 1, 2)}),
               {{3, 0xffffffff80000000ULL}});
    check_both(with_setup(0x80000000ULL, (uint64_t)-1,
                          {DIVW(3, 1, 2), REMW(4, 1, 2)}),
               {{3, 0xffffffff80000000ULL}, {4, 0}});
    check_both(with_setup(7, 0, {DIVW(3, 1, 2), REMW(4, 1, 2)}),
               {{3, ~0ULL}, {4, 7}});

    check_both(with_setup(1, 63, {SLL(3, 1, 2), SRL(4, 3, 2), SRA(5, 3, 2)}),
               {{3, 1ULL << 63}, {4, 1}, {5, ~0ULL}});
    check_both(with_setup(0x80000000ULL, 31, {SRAW(3, 1, 2), SRLW(4, 1, 2)}),
               {{3, ~0ULL}, {4, 1}});
    check_both(with_setup(0xf0f0f0f0f0f0f0f0ULL, 0,
                          {SRAI(3, 1, 60), SRLI(4, 1, 60), SLLI(5, 1, 4)}),
               {{3, ~0ULL}, {4, 15}, {5, 0x0f0f0f0f0f0f0f00ULL}});

    check_both(with_setup((uint64_t)-1, 1,
                          {SLT(3, 1, 2), SLTU(4, 1, 2), SLTI(5, 1, 0),
                           SLTIU(6, 1, -1)}),
               {{3, 1}, {4, 0}, {5, 1}, {6, 0}});
}

static void test_mem_and_flow() {

    {
        Asm a;
        a.li(7, (int32_t)(kMemBase + 0x1000));
        li64(a, 1, 0xfedcba9876543210ULL);
        a.emit(SD(7, 1, 0));
        a.emit(LB(3, 7, 1));
        a.emit(LB(4, 7, 7));
        a.emit(LH(5, 7, 6));
        a.emit(LHU(6, 7, 6));
        a.emit(LW(8, 7, 4));
        a.emit(LWU(9, 7, 4));
        a.emit(LD(10, 7, 0));
        a.emit(LBU(11, 7, 7));
        a.emit(EBREAK());
        check_both(a.finish(), {{3, 0x32},
                                {4, (uint64_t)-2},
                                {5, 0xfffffffffffffedcULL},
                                {6, 0xfedc},
                                {8, 0xfffffffffedcba98ULL},
                                {9, 0xfedcba98ULL},
                                {10, 0xfedcba9876543210ULL},
                                {11, 0xfe}});
    }

    {
        Asm a;
        a.li(1, 5);
        a.li(2, 5);
        auto yes = a.label();
        a.beq(1, 2, yes);
        a.li(3, 111);
        a.bind(yes);
        a.li(3, 222);
        auto over = a.label();
        a.blt(2, 1, over);
        a.li(4, 333);
        a.bind(over);
        a.emit(AUIPC(5, 0));
        a.emit(JAL(6, 8));
        a.emit(ADDI(4, 4, 1000));
        a.emit(EBREAK());
        auto prog = a.finish();
        auto ea = make_switch_engine();
        auto eb = make_uop_engine();
        Hart ha = run_on(*ea, prog);
        Hart hb = run_on(*eb, prog);
        CHECK_EQ(digest(ha), digest(hb));
        CHECK_EQ(ha.x[3], 222u);
        CHECK_EQ(ha.x[4], 333u);
        CHECK_EQ(ha.x[6], ha.x[5] + 8);
        CHECK(ha.trap == Trap::Ebreak);
    }

    {
        Asm a;
        a.li(1, (int32_t)(kMemBase + 25));
        a.emit(JALR(0, 1, 0));
        a.emit(EBREAK());
        a.emit(EBREAK());
        a.emit(EBREAK());
        a.emit(ADDI(2, 0, 9));
        a.emit(EBREAK());
        check_both(a.finish(), {{2, 9}, {0, 0}});
    }
}

static void test_traps() {
    auto both_trap = [&](const std::vector<uint32_t>& prog, Trap want) {
        auto ea = make_switch_engine();
        auto eb = make_uop_engine();
        Hart ha = run_on(*ea, prog);
        Hart hb = run_on(*eb, prog);
        CHECK(ha.trap == want);
        CHECK(hb.trap == want);
        CHECK_EQ(ha.trap_pc, hb.trap_pc);
        CHECK_EQ(digest(ha), digest(hb));
    };
    both_trap({0xffffffffu}, Trap::BadInsn);
    both_trap({0x00000000u}, Trap::BadInsn);
    both_trap({JALR(0, 0, 0)}, Trap::BadFetch);
    both_trap({BEQ(0, 0, 2)}, Trap::BadFetch);
    both_trap({LD(3, 0, 64)}, Trap::BadLoad);
    both_trap({SD(0, 3, 64)}, Trap::BadStore);
    both_trap({ECALL()}, Trap::Ecall);
    both_trap({EBREAK()}, Trap::Ebreak);

    {
        Asm a;
        a.li(5, 0x40000000);
        a.li(6, 0x40000008);
        a.li(7, 0x40000010);
        a.emit(LD(3, 7, 0));
        a.li(10, 'h');
        a.emit(SB(5, 10, 0));
        a.li(10, 'i');
        a.emit(SB(5, 10, 0));
        a.li(10, 7);
        a.emit(SW(6, 10, 0));
        auto prog = a.finish();
        auto ea = make_switch_engine();
        auto eb = make_uop_engine();
        Hart ha = run_on(*ea, prog);
        Hart hb = run_on(*eb, prog);
        CHECK_EQ(digest(ha), digest(hb));
        CHECK(ha.halted && ha.trap == Trap::None);
        CHECK_EQ(ha.exit_code, 7);
        CHECK(ha.out == "hi");
        CHECK_EQ(ha.x[3], 5u);
    }
}

static void test_differential() {
    std::mt19937_64 rng(0xC0FFEE);
    auto ea = make_switch_engine();
    auto eb = make_uop_engine();
    int mismatches = 0;

    for (int trial = 0; trial < 2000; trial++) {

        std::vector<uint32_t> prog;
        prog.push_back(LUI(7, 0x11));
        for (int i = 0; i < 64; i++) {
            uint32_t rd = 1 + rng() % 6;
            uint32_t rs1 = 1 + rng() % 14;
            uint32_t rs2 = 1 + rng() % 14;
            int32_t imm = (int32_t)(rng() % 4096) - 2048;
            switch (rng() % 24) {
                case 0: prog.push_back(ADD(rd, rs1, rs2)); break;
                case 1: prog.push_back(SUB(rd, rs1, rs2)); break;
                case 2: prog.push_back(MUL(rd, rs1, rs2)); break;
                case 3: prog.push_back(MULH(rd, rs1, rs2)); break;
                case 4: prog.push_back(MULHU(rd, rs1, rs2)); break;
                case 5: prog.push_back(MULHSU(rd, rs1, rs2)); break;
                case 6: prog.push_back(DIV(rd, rs1, rs2)); break;
                case 7: prog.push_back(REM(rd, rs1, rs2)); break;
                case 8: prog.push_back(DIVU(rd, rs1, rs2)); break;
                case 9: prog.push_back(REMU(rd, rs1, rs2)); break;
                case 10: prog.push_back(SLL(rd, rs1, rs2)); break;
                case 11: prog.push_back(SRA(rd, rs1, rs2)); break;
                case 12: prog.push_back(ADDI(rd, rs1, imm)); break;
                case 13: prog.push_back(XORI(rd, rs1, imm)); break;
                case 14: prog.push_back(ADDIW(rd, rs1, imm)); break;
                case 15: prog.push_back(SLLIW(rd, rs1, (uint32_t)(rng() % 32))); break;
                case 16: prog.push_back(SRAIW(rd, rs1, (uint32_t)(rng() % 32))); break;
                case 17: prog.push_back(MULW(rd, rs1, rs2)); break;
                case 18: prog.push_back(DIVW(rd, rs1, rs2)); break;
                case 19: prog.push_back(REMUW(rd, rs1, rs2)); break;
                case 20: prog.push_back(SD(7, rs2, (int32_t)(rng() % 960))); break;
                case 21: prog.push_back(LD(rd, 7, (int32_t)(rng() % 960))); break;
                case 22: prog.push_back(SB(7, rs2, (int32_t)(rng() % 1000))); break;
                case 23: prog.push_back(LH(rd, 7, (int32_t)(rng() % 1000))); break;
            }
        }
        prog.push_back(EBREAK());

        Hart ha(64 * 1024), hb(64 * 1024);
        ha.load_image(prog);
        for (int r = 1; r < 15; r++) ha.x[r] = rng();
        for (int k = 0; k < 256; k++)
            ha.mem[0x1000 + k] = (uint8_t)rng();
        hb = ha;
        ea->run(ha, 1000);
        eb->run(hb, 1000);
        if (digest(ha) != digest(hb)) {
            mismatches++;
            if (mismatches < 4) {
                printf("differential mismatch, trial %d (seed reproducible)\n",
                       trial);
                for (int r = 0; r < 32; r++)
                    if (ha.x[r] != hb.x[r])
                        printf("  x%d: %016llx vs %016llx\n", r,
                               (unsigned long long)ha.x[r],
                               (unsigned long long)hb.x[r]);
            }
        }
    }
    CHECK_EQ(mismatches, 0);
}

static void test_serialize() {
    Hart h(4096);
    h.pc = kMemBase + 44;
    for (int i = 1; i < 32; i++) h.x[i] = 0x1111111111111111ULL * i;
    h.instret = 987654321;
    h.mem[7] = 0xab;
    h.out = "pending bytes";
    h.trap = Trap::BadLoad;
    h.trap_pc = kMemBase + 40;
    h.exit_code = -3;
    h.halted = true;

    auto blob = serialize(h);
    Hart g(0);
    CHECK(deserialize(blob, g));
    CHECK_EQ(digest(g), digest(h));
    CHECK_EQ(g.exit_code, -3);
    CHECK(g.trap == Trap::BadLoad);
    CHECK(g.out == "pending bytes");

    blob.pop_back();
    CHECK(!deserialize(blob, g));
}

static std::vector<uint32_t> harness_guest() {
    Asm a;
    a.li(5, 0x40000000);
    a.li(6, 0x40000008);
    a.li(7, (int32_t)(kMemBase + 0x2000));
    a.li(1, 0);
    a.li(2, 20000);
    auto loop = a.here();
    a.emit(ADDI(9, 0, 0));
    a.emit(ANDI(3, 1, 0xff));
    a.emit(ADD(4, 7, 0));
    a.emit(ANDI(8, 1, 1023));
    a.emit(ADD(4, 4, 8));
    a.emit(SB(4, 3, 0));
    a.emit(ADDI(1, 1, 1));
    a.blt(1, 2, loop);
    for (const char c : std::string("done\n")) {
        a.li(10, c);
        a.emit(SB(5, 10, 0));
    }
    a.emit(SW(6, 0, 0));
    return a.finish();
}

static void test_harness() {
    auto image = harness_guest();

    {
        Lockstep ls(make_local_lane(make_switch_engine()),
                    make_local_lane(make_uop_engine()));
        ls.load(image, 64 * 1024);
        std::string out;
        RunStats st = ls.run(1'000'000, 1024, 4,
                             [&](const std::string& s) { out += s; });
        CHECK(st.ok);
        CHECK_EQ(st.exit_code, 0);
        CHECK(out == "done\n");
        CHECK_EQ(st.divergences, 0u);
        CHECK(st.instructions > 100000);
    }

    {
        Lockstep ls(make_local_lane(make_switch_engine()),
                    make_local_lane(make_uop_engine()));
        ls.load(image, 64 * 1024);
        std::string out;
        bool fired = false;
        RunStats st = ls.run(
            1'000'000, 1024, 4, [&](const std::string& s) { out += s; },
            [&](Lockstep& l, uint64_t q) {
                if (!fired && q == 9) {
                    l.inject(1, 1, 0x2000 + 5, 3);
                    fired = true;
                }
            });
        CHECK(fired);
        CHECK(st.ok);
        CHECK(out == "done\n");
        CHECK(st.divergences >= 1);
        CHECK_EQ(st.recoveries, st.divergences);
        CHECK_EQ(st.faults_injected, 1u);
        CHECK_EQ(st.detect_latency.size(), 1u);
        CHECK(st.detect_latency[0] <= 1024 * 2);
    }

    {
        Lockstep ls(make_local_lane(make_switch_engine()),
                    make_local_lane(make_uop_engine()));
        ls.load(image, 64 * 1024);
        std::string out;
        bool fired = false;
        RunStats st = ls.run(
            1'000'000, 1024, 4, [&](const std::string& s) { out += s; },
            [&](Lockstep& l, uint64_t q) {
                if (!fired && q == 9) {
                    l.inject(0, 0, 9, 60);
                    fired = true;
                }
            });
        CHECK(st.ok);
        CHECK(out == "done\n");
        CHECK_EQ(st.divergences, 0u);
        CHECK_EQ(st.faults_masked, 1u);
    }

    {
        Lockstep ls(make_local_lane(make_switch_engine()),
                    make_local_lane(make_uop_engine()));
        ls.load(image, 64 * 1024);
        std::string out;
        bool fired = false;
        RunStats st = ls.run(
            1'000'000, 1024, 4, [&](const std::string& s) { out += s; },
            [&](Lockstep& l, uint64_t q) {
                if (!fired && q == 3) {
                    l.inject(0, 1, 60000, 0);
                    fired = true;
                }
            });
        CHECK(st.ok);
        CHECK(out == "done\n");
        CHECK(st.divergences >= 1);
    }
}

static void test_remote_lane() {
    const uint16_t port = 47912;
    auto eng = make_uop_engine();
    volatile bool stop = false;
    std::thread server([&] { serve_lane(*eng, port, &stop); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        Lockstep ls(make_local_lane(make_switch_engine()),
                    make_remote_lane("127.0.0.1", port));
        ls.load(harness_guest(), 64 * 1024);
        std::string out;
        bool fired = false;
        RunStats st = ls.run(
            1'000'000, 2048, 4, [&](const std::string& s) { out += s; },
            [&](Lockstep& l, uint64_t q) {
                if (!fired && q == 5) {
                    l.inject(1, 1, 0x2000 + 9, 1);
                    fired = true;
                }
            });
        CHECK(st.ok);
        CHECK(out == "done\n");
        CHECK(st.divergences >= 1);
        CHECK_EQ(st.recoveries, st.divergences);
    }

    {
        auto lane = make_remote_lane("127.0.0.1", port);
        CHECK(lane->name() == "uop@remote");
    }

    shutdown_lane("127.0.0.1", port);
    server.join();
    CHECK(true);
}

int main() {
    test_encoder();
    test_alu();
    test_mem_and_flow();
    test_traps();
    test_differential();
    test_serialize();
    test_harness();
    test_remote_lane();
    printf("%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
