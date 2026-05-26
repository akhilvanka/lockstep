#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "lockstep/harness.h"

namespace lockstep {
namespace {

constexpr uint8_t kLoad = 'L', kStep = 'S', kDrain = 'D', kSnap = 'P',
                  kRestore = 'R', kInject = 'I', kName = 'N', kBye = 'B',
                  kQuit = 'Q';

bool send_all(int fd, const void* p, size_t n) {
    const char* c = (const char*)p;
    while (n) {
        ssize_t k = ::send(fd, c, n, 0);
        if (k <= 0) return false;
        c += k;
        n -= (size_t)k;
    }
    return true;
}
bool recv_all(int fd, void* p, size_t n) {
    char* c = (char*)p;
    while (n) {
        ssize_t k = ::recv(fd, c, n, 0);
        if (k <= 0) return false;
        c += k;
        n -= (size_t)k;
    }
    return true;
}
bool send_frame(int fd, const std::vector<uint8_t>& v) {
    uint32_t len = (uint32_t)v.size();
    return send_all(fd, &len, 4) && (v.empty() || send_all(fd, v.data(), v.size()));
}
bool recv_frame(int fd, std::vector<uint8_t>& v) {
    uint32_t len = 0;
    if (!recv_all(fd, &len, 4) || len > (1u << 28)) return false;
    v.resize(len);
    return len == 0 || recv_all(fd, v.data(), len);
}

void put_u64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; i++) v.push_back((uint8_t)(x >> (8 * i)));
}
uint64_t get_u64(const uint8_t* p) {
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) x |= (uint64_t)p[i] << (8 * i);
    return x;
}

class RemoteLane final : public Lane {
  public:
    RemoteLane(const std::string& host, uint16_t port) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char ps[8];
        snprintf(ps, sizeof ps, "%u", port);
        if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res)
            throw std::runtime_error("lockstep: cannot resolve " + host);
        fd_ = ::socket(res->ai_family, res->ai_socktype, 0);
        if (fd_ < 0 || ::connect(fd_, res->ai_addr, res->ai_addrlen) != 0) {
            freeaddrinfo(res);
            throw std::runtime_error("lockstep: cannot connect to " + host);
        }
        freeaddrinfo(res);
        int one = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    ~RemoteLane() override {
        if (fd_ >= 0) {
            send_frame(fd_, {kBye});
            ::close(fd_);
        }
    }

    std::string name() override {
        auto r = rpc({kName});
        return std::string(r.begin(), r.end()) + "@remote";
    }

    void load(const std::vector<uint32_t>& image, size_t mem_bytes) override {
        std::vector<uint8_t> m{kLoad};
        put_u64(m, mem_bytes);
        put_u64(m, image.size());
        for (uint32_t w : image)
            for (int i = 0; i < 4; i++) m.push_back((uint8_t)(w >> (8 * i)));
        rpc(m);
    }

    StepReply step(uint64_t n) override {
        std::vector<uint8_t> m{kStep};
        put_u64(m, n);
        auto r = rpc(m);
        if (r.size() != 26) throw std::runtime_error("lockstep: bad step reply");
        StepReply s;
        s.digest = get_u64(&r[0]);
        s.instret = get_u64(&r[8]);
        s.halted = r[16];
        s.trap = r[17];
        s.exit_code = (int32_t)(uint32_t)get_u64(&r[18]) ;
        return s;
    }

    std::string drain_output() override {
        auto r = rpc({kDrain});
        return std::string(r.begin(), r.end());
    }

    std::vector<uint8_t> snapshot() override { return rpc({kSnap}); }

    void restore(const std::vector<uint8_t>& snap) override {
        std::vector<uint8_t> m{kRestore};
        m.insert(m.end(), snap.begin(), snap.end());
        rpc(m);
    }

    void inject(int kind, uint64_t idx, unsigned bit) override {
        std::vector<uint8_t> m{kInject, (uint8_t)kind};
        put_u64(m, idx);
        m.push_back((uint8_t)bit);
        rpc(m);
    }

  private:
    std::vector<uint8_t> rpc(const std::vector<uint8_t>& req) {
        std::vector<uint8_t> reply;
        if (!send_frame(fd_, req) || !recv_frame(fd_, reply))
            throw std::runtime_error("lockstep: lane connection lost");
        return reply;
    }
    int fd_ = -1;
};

}

std::unique_ptr<Lane> make_remote_lane(const std::string& host, uint16_t port) {
    return std::make_unique<RemoteLane>(host, port);
}

void shutdown_lane(const std::string& host, uint16_t port) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res) return;
    int fd = ::socket(res->ai_family, res->ai_socktype, 0);
    if (fd >= 0 && ::connect(fd, res->ai_addr, res->ai_addrlen) == 0)
        send_frame(fd, {kQuit});
    if (fd >= 0) ::close(fd);
    freeaddrinfo(res);
}

int serve_lane(Engine& eng, uint16_t port, const volatile bool* stop) {
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return -1;
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (::bind(lfd, (sockaddr*)&sa, sizeof sa) != 0 || ::listen(lfd, 1) != 0) {
        ::close(lfd);
        return -1;
    }

    Hart hart{0};
    bool quit = false;
    while (!quit && !(stop && *stop)) {
        int fd = ::accept(lfd, nullptr, nullptr);
        if (fd < 0) break;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

        bool bye = false;
        std::vector<uint8_t> req;
        while (!bye && recv_frame(fd, req)) {
            if (req.empty()) break;
            std::vector<uint8_t> rep;
            switch (req[0]) {
                case kName: {
                    const char* n = eng.name();
                    rep.assign(n, n + strlen(n));
                    break;
                }
                case kLoad: {
                    if (req.size() < 17) break;
                    uint64_t mem_bytes = get_u64(&req[1]);
                    uint64_t nw = get_u64(&req[9]);
                    if (req.size() != 17 + nw * 4 || mem_bytes > (1u << 28)) break;
                    std::vector<uint32_t> img(nw);
                    for (uint64_t i = 0; i < nw; i++) {
                        uint32_t w = 0;
                        for (int k = 0; k < 4; k++)
                            w |= (uint32_t)req[17 + 4 * i + k] << (8 * k);
                        img[i] = w;
                    }
                    hart = Hart(mem_bytes);
                    hart.load_image(img);
                    break;
                }
                case kStep: {
                    if (req.size() != 9) break;
                    eng.run(hart, get_u64(&req[1]));
                    put_u64(rep, digest(hart));
                    put_u64(rep, hart.instret);
                    rep.push_back(hart.halted);
                    rep.push_back((uint8_t)hart.trap);
                    put_u64(rep, (uint64_t)(int64_t)hart.exit_code);
                    break;
                }
                case kDrain:
                    rep.assign(hart.out.begin(), hart.out.end());
                    hart.out.clear();
                    break;
                case kSnap:
                    rep = serialize(hart);
                    break;
                case kRestore:
                    deserialize({req.begin() + 1, req.end()}, hart);
                    break;
                case kInject:
                    if (req.size() == 11) {
                        uint64_t idx = get_u64(&req[2]);
                        unsigned bit = req[10];
                        if (req[1] == 0 && idx >= 1 && idx < 32)
                            hart.x[idx] ^= 1ULL << (bit & 63);
                        else if (req[1] == 1 && idx < hart.mem.size())
                            hart.mem[idx] ^= (uint8_t)(1u << (bit & 7));
                    }
                    break;
                case kBye:
                    bye = true;
                    break;
                case kQuit:
                    bye = quit = true;
                    break;
                default: break;
            }
            if (bye) break;
            if (!send_frame(fd, rep)) break;
        }
        ::close(fd);
    }
    ::close(lfd);
    return 0;
}

}
