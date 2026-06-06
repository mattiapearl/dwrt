# Server Book

Living notes for the Rust-native Deadworks rewrite.

This is not a full Deadlock server clone. It documents the real dedicated server boundaries we hook, the minimal Rust abstractions over those boundaries, and the evidence needed before exposing any low-level API.

## Pages

- [Hook inventory](hook-inventory.md)
- [Hook registry model](hook-registry.md)
- [Real server surface map](surface-map.md)
- [Memory manifest layer](memory-manifest.md)
- [Benchmarking and profiling](../benchmarks/README.md)
- [Trace and golden comparison layer](tracing.md)
- [Entity facades](entity-facades.md)
- [Map entities and gameplay control](map-entities-gameplay-control.md)
- [Map/NPC/Pulse/FFA RE pass](map-npc-ffa-re-20260530.md)
- [DWRT-native probe plan](dwrt-native-probe-plan.md)
- [Runtime testpoints and validation gates](runtime-testpoints.md)
- [Walker patrol write experiment](walker-patrol-write-experiment.md)
- [Friendly fire and practice bots](friendly-fire-and-bots.md)
- [Team, target, and damage layers](team-target-damage-layers.md)
- [FFA validation tests](ffa-validation-tests.md)
- [FFA in-game test matrix](ffa-ingame-test-matrix.md)
- [ProcessUsercmds model](process-usercmds.md)
- [Engine/modding patterns to compare](patterns.md)

## Rules

1. Read evidence before guessing: logs, traces, decompilation, runtime probes, generated protobufs, and Deadworks hook code.
2. Every hot callback must have a no-interest fast return.
3. Every public low-level/memory feature must be curated, versioned, and tested.
4. Keep C++ as the narrow hook/ABI shim until Rust can safely own a piece.
5. Deadlock may resemble CS2/Source 2, but do not assume CS2-exposed functions exist or are used here.
6. Profile every in-game smoke/benchmark run end-to-end; do not explain longframes from counters alone.
