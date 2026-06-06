# Walker patrol write experiment

Date: 2026-05-31

## Goal

Start the DWRT write/mutation track with a narrowly gated experiment for Walker/T2 objectives.

The desired gameplay idea is "make walkers move around the map." The first implementation started smaller: when a damaged Walker entity is observed in the server-thread damage hook, DWRT can apply either a velocity-only `CBaseEntity::Teleport` call or an explicit origin-nudge `Teleport` using a rotating vector.

## Runtime result

Manual session:

```txt
research/benchmarks/runs/20260531-144829-dwrt-manual-probe-session/
```

Configuration:

```txt
DWRT_WALKER_PATROL_EXPERIMENT=1
DWRT_WALKER_PATROL_MODE=origin-nudge
DWRT_WALKER_PATROL_STRIDE=1
DWRT_WALKER_PATROL_VELOCITIES=500,0,0;0,500,0;-500,0,0;0,-500,0
```

Final counters observed after manual hits:

```txt
damageCallbacks=10196
candidateWalkers=22
teleportAttempts=22
teleportCalls=22
bodyComponentMissing=0
sceneNodeMissing=0
originReadAttempts=22
originReadSuccesses=22
originReadFailures=0
```

Visual result: the Walker/T2 objective visibly jumps to different positions when hit. This is a teleport/nudge effect, not AI path-walking. That is acceptable as an early curated write primitive, but it should not be presented as route/pathing control.

Earlier velocity-only sessions called `Teleport(entity, null, null, velocity)` successfully, but the Walker appeared normal. Conclusion: Walker AI/physics immediately ignores or overwrites velocity-only control for this objective class.

## Why velocity/origin-nudge first

A full walker route rewrite would require one or more of:

- validated entity lifecycle/entity-list discovery;
- validated position/schema access (`m_CBodyComponent -> m_pSceneNode -> m_vecAbsOrigin`);
- AI schedule/nav goal APIs;
- spawn-point/keyvalue rewrite before objective creation;
- server-thread scheduling outside hot damage callbacks.

Those are not all proven yet. Teleport is a curated engine call already used in the Deadworks reference path and can be scoped to a designer-name filter.

## Implementation

Native module:

```txt
native/dwrt-host/dwrt_walker_patrol.hpp
native/dwrt-host/dwrt_walker_patrol.cpp
```

Activation is explicit via environment inherited by the launched server process:

```txt
DWRT_WALKER_PATROL_EXPERIMENT=1
DWRT_WALKER_PATROL_STRIDE=16
DWRT_WALKER_PATROL_MODE=velocity         # or origin-nudge
DWRT_WALKER_PATROL_ORIGIN_NUDGE=1        # alternate explicit origin-nudge gate
DWRT_WALKER_PATROL_VELOCITIES=900,0,0;0,900,0;-900,0,0;0,-900,0
```

PowerShell wrapper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/start-dwrt-manual-probe-session.ps1 `
  -Detached `
  -NoProfile `
  -WalkerPatrolExperiment `
  -WalkerPatrolStride 16
```

## Filters

The write path only runs when all are true:

- walker experiment is enabled;
- the callback is a `TakeDamageOld` hook callback;
- the callback is not recursive;
- the damaged victim has an entity identity;
- the entity designer name is one of:
  - `npc_boss_tier2`
  - `alt_npc_boss_tier2`
  - `CNPC_Boss_Tier2` fallback
- the candidate walker hit count reaches the configured stride.

## Counters

The host exports and summaries include `walkerPatrol` counters:

```txt
enabled
stride
waypointCount
damageCallbacks
candidateWalkers
nonWalkerVictims
skippedRecursive
missingIdentity
missingDesignerName
teleportAttempts
teleportCalls
bodyComponentMissing
sceneNodeMissing
originReadAttempts
originReadSuccesses
originReadFailures
```

These are written into injector summaries and manual snapshot JSONL rows off hot path.

## Safety notes

- No arbitrary public raw memory/write API is exposed.
- The write operation is an allowlisted vtable call (`CBaseEntity::Teleport`, vtable index 163 from reference evidence), not a user-provided pointer call.
- Origin-nudge mode currently reads current-build schema-backed implementation offsets (`CBaseEntity::m_CBodyComponent = 0x30`, `CBodyComponent::m_pSceneNode = 0x8`, `CGameSceneNode::m_vecAbsOrigin = 0xc8`) behind a gated experiment. These are implementation facts, not public API.
- The experiment is disabled by default.
- It should be used only in disposable local sessions.
- This is not yet a public `MapLogic` API and does not prove full route/pathing control.

## Next gates before stronger walker control

1. Add per-hook timing/max-duration counters around the mutation path.
2. Add a server-thread tick/scheduler hook so movement is not driven by damage callbacks.
3. Promote the offset-backed origin reads into versioned schema facts before any public facade.
4. Only then consider waypoint/route movement, spawn-point keyvalue mutation, or AI/nav goal control.
