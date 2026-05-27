#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#include "lockstep/encoder.h"
#include "lockstep/harness.h"

using namespace lockstep;

static constexpr int32_t kN = 10000;
static constexpr int64_t kData = 0x14000;
static constexpr int64_t kScratch = 0x18000;

static std::vector<uint32_t> build_guest() {
    using namespace lockstep::enc;
    Asm a;

    a.li(5, 0x40000000);
    a.li(6, 0x40000008);
    a.li(7, (int32_t)kData);
    a.li(18, kN);

    auto puts = [&](const char* s) {
        for (const char* p = s; *p; p++) {
            a.li(10, *p);
            a.emit(SB(5, 10, 0));
        }
    };
    auto li64 = [&](uint32_t rd, uint64_t v) {
        a.li(rd, (int32_t)(v >> 32));
        a.emit(SLLI(rd, rd, 32));
        a.li(28, (int32_t)(uint32_t)v);
        a.emit(SLLI(28, 28, 32));
        a.emit(SRLI(28, 28, 32));
        a.emit(OR(rd, rd, 28));
    };

    puts("sieve of 10000: ");

    a.emit(ADD(10, 7, 0));
    a.emit(ADD(11, 7, 18));
    {
        auto l = a.here();
        a.emit(SB(10, 0, 0));
        a.emit(ADDI(10, 10, 1));
        a.bltu(10, 11, l);
    }

    a.li(12, 2);
    auto outer = a.here();
    auto done = a.label();
    a.emit(MUL(13, 12, 12));
    a.bge(13, 18, done);
    a.emit(ADD(15, 7, 12));
    a.emit(LBU(15, 15, 0));
    auto next = a.label();
    a.bne(15, 0, next);
    a.li(17, 1);
    {
        auto mark = a.here();
        a.emit(ADD(16, 7, 13));
        a.emit(SB(16, 17, 0));
        a.emit(ADD(13, 13, 12));
        a.blt(13, 18, mark);
    }
    a.bind(next);
    a.emit(ADDI(12, 12, 1));
    a.jal(0, outer);
    a.bind(done);

    a.li(10, 0);
    a.li(11, 2);
    {
        auto cl = a.here();
        a.emit(ADD(14, 7, 11));
        a.emit(LBU(14, 14, 0));
        auto skip = a.label();
        a.bne(14, 0, skip);
        a.emit(ADDI(10, 10, 1));
        a.bind(skip);
        a.emit(ADDI(11, 11, 1));
        a.blt(11, 18, cl);
    }

    a.li(11, 10);
    a.li(12, (int32_t)kScratch + 32);
    a.emit(ADD(13, 12, 0));
    {
        auto dl = a.here();
        a.emit(REMU(14, 10, 11));
        a.emit(ADDI(14, 14, '0'));
        a.emit(ADDI(13, 13, -1));
        a.emit(SB(13, 14, 0));
        a.emit(DIVU(10, 10, 11));
        a.bne(10, 0, dl);
    }
    {
        auto pl = a.here();
        a.emit(LBU(14, 13, 0));
        a.emit(SB(5, 14, 0));
        a.emit(ADDI(13, 13, 1));
        a.bltu(13, 12, pl);
    }

    puts(" primes, fnv ");

    li64(20, 0xcbf29ce484222325ULL);
    li64(21, 0x100000001b3ULL);
    a.emit(ADD(10, 7, 0));
    a.emit(ADD(11, 7, 18));
    {
        auto fl = a.here();
        a.emit(LBU(14, 10, 0));
        a.emit(XOR(20, 20, 14));
        a.emit(MUL(20, 20, 21));
        a.emit(ADDI(10, 10, 1));
        a.bltu(10, 11, fl);
    }

    a.li(12, 60);
    {
        auto hx = a.here();
        a.emit(SRL(14, 20, 12));
        a.emit(ANDI(14, 14, 15));
        a.li(15, 10);
        auto dig = a.label();
        auto emit_ch = a.label();
        a.blt(14, 15, dig);
        a.emit(ADDI(14, 14, 'a' - 10));
        a.jal(0, emit_ch);
        a.bind(dig);
        a.emit(ADDI(14, 14, '0'));
        a.bind(emit_ch);
        a.emit(SB(5, 14, 0));
        a.emit(ADDI(12, 12, -4));
        a.bge(12, 0, hx);
    }

    puts("\n");
    a.emit(SW(6, 0, 0));
    return a.finish();
}

static std::string golden() {
    std::vector<uint8_t> arr(kN, 0);
    for (int i = 2; i * i < kN; i++)
        if (!arr[i])
            for (int j = i * i; j < kN; j += i) arr[j] = 1;
    int cnt = 0;
    for (int i = 2; i < kN; i++)
        if (!arr[i]) cnt++;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < kN; i++) {
        h ^= arr[i];
        h *= 0x100000001b3ULL;
    }
    char buf[128];
    snprintf(buf, sizeof buf, "sieve of 10000: %d primes, fnv %016llx\n", cnt,
             (unsigned long long)h);
    return buf;
}

struct LanePair {
    std::string remote_host;
    uint16_t remote_port = 0;
    Lockstep make() const {
        auto a = make_local_lane(make_switch_engine());
        auto b = remote_host.empty()
                     ? make_local_lane(make_uop_engine())
                     : make_remote_lane(remote_host, remote_port);
        return Lockstep(std::move(a), std::move(b));
    }
};

int main(int argc, char** argv) {
    int trials = 250;
    LanePair lanes;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--remote") && i + 2 < argc) {
            lanes.remote_host = argv[++i];
            lanes.remote_port = (uint16_t)atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--trials N] [--remote HOST PORT]\n", argv[0]);
            return 2;
        }
    }

    const auto image = build_guest();
    const std::string want = golden();
    printf("guest image: %zu instructions; golden: %s", image.size(), want.c_str());

    {
        Lockstep ls = lanes.make();
        ls.load(image);
        std::string out;
        RunStats st = ls.run(50'000'000, 4096, 8,
                             [&](const std::string& s) { out += s; });
        printf("\nclean run:  %llu insns, %llu quanta, %llu checkpoints, "
               "%llu divergences\n",
               (unsigned long long)st.instructions, (unsigned long long)st.quanta,
               (unsigned long long)st.checkpoints, (unsigned long long)st.divergences);
        printf("guest says: %s", out.c_str());
        if (!st.ok || st.exit_code != 0 || out != want) {
            printf("FAIL: clean run mismatch (%s)\n", st.error.c_str());
            return 1;
        }
        printf("output matches golden: yes\n");
    }

    printf("\ncampaign: %d runs, one random architectural bit flip each\n", trials);
    std::mt19937_64 rng(20260609);
    int detected = 0, masked = 0, not_injected = 0, wrong = 0;
    std::vector<uint64_t> lat;

    for (int t = 0; t < trials; t++) {
        Lockstep ls = lanes.make();
        ls.load(image);
        uint64_t when = rng() % 45;
        int lane = (int)(rng() & 1);
        bool reg = rng() % 100 < 60;
        uint64_t idx = reg ? 1 + rng() % 31 : rng() % (256 * 1024);
        unsigned bit = (unsigned)(rng() % (reg ? 64 : 8));
        bool fired = false;

        std::string out;
        RunStats st = ls.run(
            50'000'000, 4096, 8, [&](const std::string& s) { out += s; },
            [&](Lockstep& l, uint64_t q) {
                if (!fired && q == when) {
                    l.inject(lane, reg ? 0 : 1, idx, bit);
                    fired = true;
                }
            });

        if (!st.ok || st.exit_code != 0 || out != want) {
            wrong++;
            printf("  trial %d: WRONG OUTPUT (lane %d %s[%llu] bit %u): %s\n", t,
                   lane, reg ? "x" : "mem", (unsigned long long)idx, bit,
                   st.error.c_str());
            continue;
        }
        if (!fired) not_injected++;
        else if (st.divergences > 0) {
            detected++;
            for (uint64_t v : st.detect_latency) lat.push_back(v);
        } else masked++;
    }

    printf("  detected+recovered: %d\n", detected);
    printf("  masked (overwritten before the next sync point): %d\n", masked);
    printf("  run halted before injection slot: %d\n", not_injected);
    printf("  wrong output escaped: %d\n", wrong);
    if (!lat.empty()) {
        std::sort(lat.begin(), lat.end());
        printf("  detection latency (insns): p50=%llu p99=%llu max=%llu\n",
               (unsigned long long)lat[lat.size() / 2],
               (unsigned long long)lat[lat.size() * 99 / 100],
               (unsigned long long)lat.back());
    }
    if (wrong) {
        printf("CAMPAIGN FAIL\n");
        return 1;
    }
    printf("campaign OK: no corrupted output ever escaped a sync point\n");
    return 0;
}
