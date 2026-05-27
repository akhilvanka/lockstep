# lockstep

Software dual-modular redundancy: the same RV64IM program runs on two
independently written execution engines (a switch interpreter and a
predecoded micro-op engine), full architectural state is digested and
compared at every sync point, and no output byte is released until both
lanes agree. On divergence both lanes roll back to the last agreed
checkpoint and replay.

Two different engines instead of two copies: an implementation bug
shows up as a lane divergence instead of silent agreement. This caught
a real SRAI decode bug in lane B during development.

    campaign: 500 runs, one random architectural bit flip each
      detected+recovered: 483
      masked (overwritten before next sync): 17
      wrong output escaped: 0

## run

    cmake -B build && cmake --build build
    ./build/lockstep_tests        # 192 checks
    ./build/redundant_demo        # clean run + bit-flip campaign

Lane B can run on another machine (different CPU/OS/compiler):

    ./build/lane_worker --engine uop --port 9000      # second machine
    ./build/redundant_demo --remote <host> 9000       # first machine

The guest is a prime sieve built with the in-tree RV64IM encoder — no
external toolchain needed — checked against a native reimplementation.

## layout

    include/lockstep/rv64.h      hart state, MMIO, engine interface, digest
    include/lockstep/encoder.h   RV64IM encoders + program builder
    include/lockstep/harness.h   lane interface, lockstep manager, wire format
    src/lane_switch.cpp          lane A
    src/lane_uop.cpp             lane B
    src/harness.cpp              sync/compare/commit/checkpoint/replay
    src/remote.cpp               TCP lane transport
    tools/lane_worker.cpp        serve a lane remotely
    demo/redundant_demo.cpp      sieve guest + fault campaign
    tests/test_main.cpp          conformance + differential fuzz + harness

RV64IM only; two lanes detect but cannot vote — a third would buy
attribution.
