# ProcessUsercmds Model

Evidence read:

```txt
C:\Users\User\Downloads\ProcessUserCmds (1).cpp
../deadworks-tournament/deadworks/src/Core/Hooks/ProcessUsercmds.*
../deadworks-tournament/deadworks/src/Core/UsercmdVisitorRuntime.*
```

## Current hook boundary

Deadworks hook signature:

```cpp
void* Hook_ProcessUsercmds(void* controller, void* cmds, int numcmds, unsigned char paused, float margin)
```

Current Rust-facing compact ABI mirrors Deadworks `FastUsercmdNative`:

```txt
sizeof(FastUsercmdNative) = 48
CUserCmd stride           = 0xA8
CUserCmd protobuf offset  = 0x10
observed cmd number       = cmd + 0x8
```

## Behavior summary

The server batch processor appears to:

1. Resolve controller pawn and determine whether input can be processed.
2. Read last processed sequence from a controller command queue if present.
3. For each incoming command:
   - compute queued command count,
   - clamp against max usercmds per sample,
   - derive command simulation time from margin/start time and tick interval,
   - fill missing command numbers in a 16-slot history ring as skipped,
   - mark non-final commands late,
   - quantize the final command sub-tick/time status,
   - send stale commands to queue history,
   - append new commands to execution/prediction state.
4. Finalize controller paused/final-tick state and pawn prediction flags.
5. Notify command queue and call `PhysicsSimulate` on final tick.

## Open facts to validate in IDA/runtime probes

- Exact `CBasePlayerController` offsets for command queue/history.
- Exact `CPlayerCommandQueue` vtable methods:
  - `OnCommandsProcessed` around vtable +8
  - `GetLastProcessedSequence` around vtable +32
  - `AddCommandToHistory` around vtable +40
  - `LogNetworkMargin` around vtable +56
- `AppendUserCommand` address and side effects.
- `QuantizeSubTickTime(float)` behavior.
- `PhysicsSimulate` vtable index (~159 from current artifact).
- Whether Deadlock server actually has any subtick-equivalent state despite CS2 similarities.

## Rust implementation rule

Do not expose arbitrary controller offsets to plugins. The Rust API should expose stable concepts:

```rust
UsercmdBatch
UsercmdView
UsercmdHistoryStatus
PlayerController
PlayerPawn
```

Offset-backed access remains internal to `dwrt-memory` and versioned by a memory manifest.
