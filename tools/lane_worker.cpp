#include <cstdio>
#include <cstring>

#include "lockstep/harness.h"

int main(int argc, char** argv) {
    const char* engine = "uop";
    uint16_t port = 9000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--engine") && i + 1 < argc) engine = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = (uint16_t)atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--engine switch|uop] [--port N]\n", argv[0]);
            return 2;
        }
    }
    auto eng = strcmp(engine, "switch") == 0 ? lockstep::make_switch_engine()
                                             : lockstep::make_uop_engine();
    fprintf(stderr, "lane_worker: engine=%s port=%u\n", eng->name(), port);
    if (lockstep::serve_lane(*eng, port) != 0) {
        perror("serve_lane");
        return 1;
    }
    return 0;
}
