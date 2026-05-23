#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lockstep/rv64.h"

namespace lockstep {

std::vector<uint8_t> serialize(const Hart& h);
bool deserialize(const std::vector<uint8_t>& buf, Hart& out);

struct StepReply {
    uint64_t digest = 0;
    uint64_t instret = 0;
    uint8_t halted = 0;
    uint8_t trap = 0;
    int32_t exit_code = 0;
};

class Lane {
  public:
    virtual ~Lane() = default;
    virtual std::string name() = 0;
    virtual void load(const std::vector<uint32_t>& image, size_t mem_bytes) = 0;
    virtual StepReply step(uint64_t n) = 0;
    virtual std::string drain_output() = 0;
    virtual std::vector<uint8_t> snapshot() = 0;
    virtual void restore(const std::vector<uint8_t>& snap) = 0;

    virtual void inject(int kind, uint64_t idx, unsigned bit) = 0;
};

std::unique_ptr<Lane> make_local_lane(std::unique_ptr<Engine> eng);
std::unique_ptr<Lane> make_remote_lane(const std::string& host, uint16_t port);

int serve_lane(Engine& eng, uint16_t port, const volatile bool* stop = nullptr);

void shutdown_lane(const std::string& host, uint16_t port);

struct RunStats {
    uint64_t quanta = 0;
    uint64_t instructions = 0;
    uint64_t checkpoints = 0;
    uint64_t divergences = 0;
    uint64_t recoveries = 0;
    uint64_t faults_injected = 0;
    uint64_t faults_masked = 0;
    std::vector<uint64_t> detect_latency;
    bool ok = false;
    Trap trap = Trap::None;
    int exit_code = 0;
    std::string error;
};

class Lockstep {
  public:
    Lockstep(std::unique_ptr<Lane> a, std::unique_ptr<Lane> b)
        : a_(std::move(a)), b_(std::move(b)) {}

    void load(const std::vector<uint32_t>& image, size_t mem_bytes = 256 * 1024);

    void inject(int lane, int kind, uint64_t idx, unsigned bit);

    RunStats run(uint64_t max_insns, uint64_t quantum = 4096,
                 uint64_t checkpoint_every = 8,
                 std::function<void(const std::string&)> on_output = {},
                 std::function<void(Lockstep&, uint64_t)> chaos = {});

    static constexpr int kMaxRetries = 3;

  private:
    std::unique_ptr<Lane> a_, b_;
    uint64_t pending_inject_at_ = 0;
    bool fault_outstanding_ = false;
    RunStats* stats_ = nullptr;
};

}
