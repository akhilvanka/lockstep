#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace lockstep {

constexpr uint64_t kMemBase = 0x10000;
constexpr uint64_t kMmioPutc = 0x40000000;
constexpr uint64_t kMmioExit = 0x40000008;
constexpr uint64_t kMmioInstret = 0x40000010;

enum class Trap : uint8_t {
    None = 0,
    BadInsn,
    BadFetch,
    BadLoad,
    BadStore,
    Ecall,
    Ebreak,
};

const char* trap_name(Trap t);

struct Hart {
    static constexpr size_t kPageBytes = 4096;

    uint64_t pc = kMemBase;
    uint64_t x[32] = {};
    uint64_t instret = 0;
    std::vector<uint8_t> mem;
    std::string out;
    bool halted = false;
    int exit_code = 0;
    Trap trap = Trap::None;
    uint64_t trap_pc = 0;
    mutable std::vector<uint64_t> page_hash;
    mutable std::vector<uint8_t> page_dirty;

    explicit Hart(size_t mem_bytes = 256 * 1024) : mem(mem_bytes, 0) {}

    bool in_ram(uint64_t addr, unsigned bytes) const {
        return addr >= kMemBase && addr - kMemBase + bytes <= mem.size();
    }

    void mark(uint64_t addr, unsigned bytes) {
        if (page_dirty.empty()) return;
        size_t p0 = (addr - kMemBase) / kPageBytes;
        size_t p1 = (addr - kMemBase + bytes - 1) / kPageBytes;
        for (size_t p = p0; p <= p1 && p < page_dirty.size(); p++)
            page_dirty[p] = 1;
    }
    void mark_all() {
        page_hash.clear();
        page_dirty.clear();
    }

    uint64_t load_ram(uint64_t addr, unsigned bytes) const {
        uint64_t v = 0;
        std::memcpy(&v, mem.data() + (addr - kMemBase), bytes);
        return v;
    }
    void store_ram(uint64_t addr, uint64_t v, unsigned bytes) {
        std::memcpy(mem.data() + (addr - kMemBase), &v, bytes);
        mark(addr, bytes);
    }

    void load_image(const std::vector<uint32_t>& words, uint64_t addr = kMemBase) {
        for (size_t i = 0; i < words.size(); i++)
            store_ram(addr + 4 * i, words[i], 4);
        pc = addr;
    }
};

uint64_t digest(const Hart& h);

class Engine {
  public:
    virtual ~Engine() = default;
    virtual const char* name() const = 0;

    virtual void run(Hart& h, uint64_t n) = 0;
};

std::unique_ptr<Engine> make_switch_engine();
std::unique_ptr<Engine> make_uop_engine();

}
