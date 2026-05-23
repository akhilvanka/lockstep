#include "lockstep/harness.h"

#include <cstring>

namespace lockstep {

static void put_u64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; i++) v.push_back((uint8_t)(x >> (8 * i)));
}
static uint64_t get_u64(const uint8_t* p) {
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) x |= (uint64_t)p[i] << (8 * i);
    return x;
}

std::vector<uint8_t> serialize(const Hart& h) {
    std::vector<uint8_t> v;
    v.reserve(h.mem.size() + h.out.size() + 320);
    put_u64(v, h.pc);
    for (int i = 0; i < 32; i++) put_u64(v, h.x[i]);
    put_u64(v, h.instret);
    put_u64(v, h.mem.size());
    v.insert(v.end(), h.mem.begin(), h.mem.end());
    put_u64(v, h.out.size());
    v.insert(v.end(), h.out.begin(), h.out.end());
    v.push_back(h.halted);
    v.push_back((uint8_t)h.trap);
    put_u64(v, (uint64_t)(int64_t)h.exit_code);
    put_u64(v, h.trap_pc);
    return v;
}

bool deserialize(const std::vector<uint8_t>& buf, Hart& h) {
    const uint8_t* p = buf.data();
    const uint8_t* end = p + buf.size();
    auto need = [&](size_t n) { return (size_t)(end - p) >= n; };
    if (!need(8 * 35)) return false;
    h.pc = get_u64(p); p += 8;
    for (int i = 0; i < 32; i++) { h.x[i] = get_u64(p); p += 8; }
    h.instret = get_u64(p); p += 8;
    uint64_t msz = get_u64(p); p += 8;
    if (!need(msz)) return false;
    h.mem.assign(p, p + msz); p += msz;
    if (!need(8)) return false;
    uint64_t osz = get_u64(p); p += 8;
    if (!need(osz)) return false;
    h.out.assign((const char*)p, osz); p += osz;
    if (!need(2 + 16)) return false;
    h.halted = *p++ != 0;
    h.trap = (Trap)*p++;
    h.exit_code = (int)(int64_t)get_u64(p); p += 8;
    h.trap_pc = get_u64(p); p += 8;
    return p == end;
}

namespace {
class LocalLane final : public Lane {
  public:
    explicit LocalLane(std::unique_ptr<Engine> eng) : eng_(std::move(eng)) {}

    std::string name() override { return eng_->name(); }

    void load(const std::vector<uint32_t>& image, size_t mem_bytes) override {
        hart_ = Hart(mem_bytes);
        hart_.load_image(image);
    }

    StepReply step(uint64_t n) override {
        eng_->run(hart_, n);
        StepReply r;
        r.digest = digest(hart_);
        r.instret = hart_.instret;
        r.halted = hart_.halted;
        r.trap = (uint8_t)hart_.trap;
        r.exit_code = hart_.exit_code;
        return r;
    }

    std::string drain_output() override {
        std::string s;
        s.swap(hart_.out);
        return s;
    }

    std::vector<uint8_t> snapshot() override { return serialize(hart_); }

    void restore(const std::vector<uint8_t>& snap) override {
        deserialize(snap, hart_);
    }

    void inject(int kind, uint64_t idx, unsigned bit) override {
        if (kind == 0 && idx >= 1 && idx < 32) {
            hart_.x[idx] ^= 1ULL << (bit & 63);
        } else if (kind == 1 && idx < hart_.mem.size()) {
            hart_.mem[idx] ^= (uint8_t)(1u << (bit & 7));
        }
    }

  private:
    std::unique_ptr<Engine> eng_;
    Hart hart_{0};
};
}

std::unique_ptr<Lane> make_local_lane(std::unique_ptr<Engine> eng) {
    return std::make_unique<LocalLane>(std::move(eng));
}

void Lockstep::load(const std::vector<uint32_t>& image, size_t mem_bytes) {
    a_->load(image, mem_bytes);
    b_->load(image, mem_bytes);
}

void Lockstep::inject(int lane, int kind, uint64_t idx, unsigned bit) {
    (lane == 0 ? a_ : b_)->inject(kind, idx, bit);
    if (stats_) {
        stats_->faults_injected++;
        fault_outstanding_ = true;
    }
}

RunStats Lockstep::run(uint64_t max_insns, uint64_t quantum,
                       uint64_t checkpoint_every,
                       std::function<void(const std::string&)> on_output,
                       std::function<void(Lockstep&, uint64_t)> chaos) {
    RunStats st;
    stats_ = &st;
    fault_outstanding_ = false;

    std::vector<uint8_t> ckpt = a_->snapshot();
    st.checkpoints = 1;
    uint64_t agreed_insns = 0;
    int retries = 0;

    for (uint64_t q = 0; agreed_insns < max_insns; q++) {
        if (chaos) {

            uint64_t before = st.faults_injected;
            chaos(*this, q);
            if (st.faults_injected != before) pending_inject_at_ = agreed_insns;
        }

        StepReply ra = a_->step(quantum);
        StepReply rb = b_->step(quantum);

        if (ra.digest != rb.digest) {
            st.divergences++;
            if (fault_outstanding_) {
                uint64_t at = ra.instret > rb.instret ? ra.instret : rb.instret;
                st.detect_latency.push_back(at - pending_inject_at_);
                fault_outstanding_ = false;
            }
            if (++retries > kMaxRetries) {
                st.error = "hard divergence: lanes disagree after " +
                           std::to_string(kMaxRetries) + " replays (" +
                           a_->name() + " vs " + b_->name() + ")";
                break;
            }

            a_->restore(ckpt);
            b_->restore(ckpt);
            st.recoveries++;
            continue;
        }

        retries = 0;
        st.quanta++;
        st.instructions = ra.instret;
        agreed_insns = ra.instret;
        if (fault_outstanding_) {

            st.faults_masked++;
            fault_outstanding_ = false;
        }

        auto commit_output = [&] {
            std::string out = a_->drain_output();
            b_->drain_output();
            if (!out.empty() && on_output) on_output(out);
        };

        if (ra.halted) {
            commit_output();
            st.ok = true;
            st.trap = (Trap)ra.trap;
            st.exit_code = ra.exit_code;
            break;
        }

        if (checkpoint_every && st.quanta % checkpoint_every == 0) {
            commit_output();
            ckpt = a_->snapshot();
            st.checkpoints++;
        }
    }

    if (!st.ok && st.error.empty() && agreed_insns >= max_insns)
        st.error = "instruction budget exhausted";
    stats_ = nullptr;
    return st;
}

}
