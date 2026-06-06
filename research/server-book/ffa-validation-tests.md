# FFA validation tests

Date: 2026-05-31

This page defines the test suite required before DWRT can claim a true FFA-like Deadlock mode.

The suite is intentionally layered. A passing `TakeDamageOld` hook is not enough: FFA needs proof at target selection, hit filtering, damage creation/application, scoring, bot policy, UI-facing state, and performance/safety gates.

## Test principles

- Keep normal engine teams unless a test explicitly proves a safer alternative.
- Do not expose raw offsets/signatures/vtables as public API.
- Prefer virtual DWRT policy overlays over mutating entity class/type.
- Count-only probes first; no synchronous logging from hot callbacks.
- Every mutation test must be gated, disposable, reversible, and artifact-counted.
- Same-team hits rejected before `CTakeDamageInfo` must be fixed at target/hit policy, not at `TakeDamageOld`.

## Automated policy tests

Implemented in `crates/dwrt-engine/src/ffa.rs`.

Run:

```powershell
cargo test -p dwrt-engine ffa
```

Current policy tests:

| ID | Test | Purpose |
| --- | --- | --- |
| FFA-POL-001 | `target_mask_constants_match_current_re_extract` | Locks current `CITADEL_UNIT_TARGET_*` values from RE artifacts. |
| FFA-POL-002 | `raw_team_relation_is_relative_and_preserves_normal_teams` | Proves same/enemy is relative to source/target team, not a global label. |
| FFA-POL-003 | `stock_enemy_mask_rejects_same_team_player` | Captures baseline failure that FFA must override earlier than damage. |
| FFA-POL-004 | `ffa_same_team_players_match_enemy_hero_mask_without_team_rewrite` | Desired player FFA behavior: same-team player virtually matches `HERO_ENEMY` while engine teams remain unchanged. |
| FFA-POL-005 | `enemy_players_remain_enemies_under_normal_engine_teams` | Existing enemy behavior must not regress. |
| FFA-POL-006 | `same_team_objectives_are_separate_from_player_ffa` | Player FFA must not automatically enable own-objective damage. |
| FFA-POL-007 | `objective_policy_covers_bosses_and_buildings_but_not_all_friendly_entities` | Objective FFA is its own explicit policy; does not globally convert all friendly targets. |
| FFA-POL-008 | `neutral_targets_are_category_based_not_team_two_or_three` | Neutral is a target category, not team 2/3. |
| FFA-POL-009 | `unknown_teams_fail_closed_for_hero_masks` | Unknown/invalid team state must not be silently treated as enemy. |
| FFA-POL-010 | `midboss_alike_players_are_virtual_overlay_not_class_mutation` | The safe version of “make players midboss-like”: add a virtual boss target bit, do not mutate the player class. |
| FFA-POL-011 | `midboss_overlay_does_not_make_players_neutral` | Boss-like overlay is not neutralization. |
| FFA-POL-012 | `ffa_does_not_use_unique_engine_teams` | Codifies that unique per-player engine teams are rejected. |
| FFA-POL-013 | `damage_policy_cannot_create_a_hit_rejected_by_target_filter` | `DamagePolicy` cannot compensate for pre-damage target rejection. |
| FFA-POL-014 | `every_current_side_unit_kind_has_friendly_and_enemy_bits` | Ensures all known side-relative target kinds have separate friendly/enemy masks. |

## Automated native readiness tests

### FFA-AUTO-001: workspace policy/build gate

Command:

```powershell
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
```

Pass criteria:

- all Rust tests pass;
- no clippy warnings;
- FFA policy tests included.

### FFA-AUTO-002: signature resolver gate

Command:

```powershell
scripts/smoke-dwrt-host.ps1 -NoProfile -MappedModuleCheck
```

Pass criteria:

- `ok=true`;
- `requiredFailures=0`;
- `mappedRequiredFailures=0`;
- target-filter signatures unique and expected-RVA-correct:
  - `CitadelTargetFilter::FriendlyFire` -> `0x18d9180` on current build `0x6a1a1a1d`;
  - `CitadelTargetFilter::FriendlyFireCaller` -> `0x7839d0` on current build `0x6a1a1a1d`.

Last passing artifact:

```txt
research/benchmarks/runs/20260601-1457-ffa-readiness-syntax/host-target-filter-signatures/
```

### FFA-AUTO-003: live hook install gate

Command:

```powershell
scripts/smoke-dwrt-live-server.ps1 -NoProfile -InstallProbeHooks -HoldSeconds 5 -ProbeMountMask 7
```

Pass criteria:

- `ok=true`;
- `hookInstallAttempts=5`;
- `hooksInstalled=5`;
- `hookInstallFailures=0`;
- `targetProbeSnapshotStatus=0`;
- no recursive callbacks in strict smoke.

Last passing artifact:

```txt
research/benchmarks/runs/20260601-1501-ffa-readiness-live-hooks-probe7/live-target-filter-hook-install/
```

### FFA-AUTO-004: native stack gate

Command:

```powershell
scripts/test-dwrt-native-stack.ps1 -NoProfile -IncludeLiveServer -IncludeHookInstall
```

Pass criteria:

- Rust tests pass;
- runtime C ABI smoke passes;
- host resolver/bootstrap pass;
- live server bootstrap passes;
- hook install pass reports five installed hooks.

## Runtime probe tests

These require a connected client unless explicitly noted.

Use the manual probe harness:

```powershell
scripts/start-dwrt-manual-probe-session.ps1 -NoProfile -Detached -PollSeconds 2 -ProbeMountMask 7
```

Connect in Deadlock:

```txt
connect 127.0.0.1:<port>
```

Stop by creating the run directory's `stop-session.flag`.

### FFA-PROBE-001: baseline enemy player/objective activity

Setup:

- target probe enabled;
- no target source spoof;
- default `mp_friendlyfire`.

Actions:

- shoot enemy player/bot if available;
- shoot enemy Guardian/Walker/objective.

Expected evidence:

- target filter/caller counters increase when the path uses the probed filter;
- accepted enemy damage reaches `TakeDamageOld` (`damageSeen > 0`);
- enemy objective damage remains possible.

Regression if:

- enemy objective damage stops reaching `TakeDamageOld`;
- `filterDenied` spikes for ordinary enemy shots.

### FFA-PROBE-002: same-team player rejection location

Setup:

- target probe enabled;
- `mp_friendlyfire 0` first, then repeat with `mp_friendlyfire 1`.

Actions:

- same-team player shoots same-team player.

Expected evidence:

- if `filterSameTeamDenied > 0`, the current-build `0x1818d9180` filter is a live rejection point;
- if target probe counters do not move and damage does not reach `TakeDamageOld`, rejection is earlier, likely ability/weapon target-mask/trace validation;
- if damage reaches `TakeDamageOld`, same-team player hits are not blocked before damage on that path.

### FFA-PROBE-003: same-team objective rejection location

Setup:

- target probe enabled;
- repeat with `mp_friendlyfire 0` and `mp_friendlyfire 1`.

Actions:

- local team player shoots own Guardian/Walker/base objective.

Expected evidence:

- `filterObjectiveTargetDenied > 0` proves rejection in the probed target filter;
- no target-filter counters and no `TakeDamageOld` objective candidates means rejection is earlier than the probed filter;
- `TakeDamageOld` objective candidates with same-team victim means the accepted-damage policy layer can participate.

### FFA-PROBE-004: target source-team spoof experiment

Setup:

```powershell
scripts/start-dwrt-manual-probe-session.ps1 -NoProfile -Detached -TargetSourceTeamSpoofExperiment -PollSeconds 2 -ProbeMountMask 7
```

Actions:

- repeat same-team player and objective shots.

Expected evidence:

- `sourceSpoofAttempts == sourceSpoofApplied == sourceSpoofRestored` for attempted paths;
- if `sourceSpoofAllowed > baselineAllowed`, source-team comparison is a live gate;
- if no change, the relevant path is controlled by target masks or an earlier filter.

Safety regression if:

- restore count is lower than applied count;
- enemy damage regresses;
- recursion/timing gates fail.

### FFA-PROBE-005: same-team objective victim spoof regression guard

Setup:

```powershell
scripts/start-dwrt-manual-probe-session.ps1 -NoProfile -Detached -FriendlyFireExperiment -FriendlyFireLocalTeam 2 -PollSeconds 2 -ProbeMountMask 7
```

Actions:

- shoot enemy objective;
- shoot own objective.

Expected evidence:

- enemy objective damage must still reach `TakeDamageOld`;
- scoped victim spoof must not reproduce the blind-spoof regression;
- if own objective never reaches `TakeDamageOld`, victim spoof is too late and must not be promoted.

Repeat with `-FriendlyFireLocalTeam 3` if local team ambiguity remains.

### FFA-PROBE-006: unit type histogram / `0x1a` bypass

Setup:

- target probe enabled.

Actions:

- shoot enemy player;
- shoot same-team player;
- shoot enemy/own objectives;
- shoot neutral camp;
- shoot MidBoss.

Expected evidence:

- `lastTargetUnitType` and future histograms identify which target categories hit the current-build `0x1807839d0` bypass;
- `callerUnit0x1aBypassCandidates > 0` only when target category actually uses the bypass.

Promotion gate:

- do not treat `0x1a` as MidBoss until runtime category evidence ties it to MidBoss/neutral/class names.

## Mutation policy tests to add before implementation

These are required before any FFA mutation/facade is exposed.

| ID | Required test | Pass criteria |
| --- | --- | --- |
| FFA-MUT-001 | TargetPolicy player hero mask translation | Same-team player can be treated as `HERO_ENEMY`; engine team unchanged; restore not needed because no team write. |
| FFA-MUT-002 | TargetPolicy objective mask translation | Same-team boss/building can be treated as `BOSS_ENEMY`/`BUILDING_ENEMY` only when objective policy is enabled. |
| FFA-MUT-003 | TargetPolicy neutral preservation | Neutral masks are preserved; FFA does not reclassify everyone as neutral. |
| FFA-MUT-004 | Player boss overlay | Player can virtually match `BOSS_ENEMY` for an experiment, but remains player/pawn/hero for controller, networking, and scoring. |
| FFA-MUT-005 | No player class mutation | Any attempt to mutate player entity class/vtable/unit type is rejected by tests/review. |
| FFA-MUT-006 | CEntityHandle attacker resolver | `m_hAttacker` and `m_hInflictor` resolve through `DAT_1832b3280` with serial validation; invalid handles fail closed. |
| FFA-MUT-007 | Cached source-team awareness | Tests prove late team spoof does not assume `CTakeDamageInfo + 0xd8` cached metadata updates. |
| FFA-MUT-008 | Restore guarantee | Every temporary write test must have applied/restored counters equal before pass. |
| FFA-MUT-009 | Recursion partitioning | Per-hook recursion counters distinguish target-filter recursion from damage/output recursion. |
| FFA-MUT-010 | Timing gate | Callback max/percentile timing stays under the agreed hot-path budget. |

## Gameplay acceptance tests

These are the tests that define “true FFA-like experience” from a player perspective.

### FFA-GAME-001: all players can damage each other

Scenario:

- at least three human players or controlled bots;
- players remain on normal engine teams 2/3;
- FFA player TargetPolicy enabled.

Pass criteria:

- same-engine-team player damage works;
- opposite-engine-team player damage still works;
- self damage behavior is explicit and documented;
- death/respawn does not break controller/pawn state.

### FFA-GAME-002: player kill credit uses virtual enemies

Pass criteria:

- same-engine-team kill is credited to attacker in DWRT score policy;
- assists/killfeed/game events do not crash or attribute to invalid handles;
- engine scoreboard quirks are documented if not yet overridden.

### FFA-GAME-003: own objectives policy is explicit

Run twice:

1. own objectives disabled;
2. own objectives enabled.

Pass criteria:

- disabled: own objectives remain protected even in player FFA;
- enabled: own objectives can be damaged without breaking enemy objective damage;
- rewards/objective events are either correct or DWRT-owned and documented.

### FFA-GAME-004: MidBoss/neutral remain coherent

Pass criteria:

- MidBoss damage rules still work;
- neutral camp damage/aggro still works;
- FFA player policy does not accidentally make players neutral entities;
- optional player boss overlay only affects target masks, not class/pawn identity.

### FFA-GAME-005: bots can select same-team player enemies if BotPolicy is enabled

Pass criteria:

- shipped bot configs spawn bots;
- bots attack policy-selected players regardless of engine team when enabled;
- bots do not attack invalid spawn/protected entities;
- disabling BotPolicy restores stock bot behavior.

### FFA-GAME-006: UI/minimap/team assumptions do not block gameplay

Pass criteria:

- players can connect, spawn, fight, die, and respawn;
- team-colored UI may remain team-colored, but FFA scoring must be clear;
- no client crash from virtual policy choices.

### FFA-GAME-007: networking/snapshots remain engine-owned

Pass criteria:

- no snapshot serialization errors;
- no Steam/auth/session spoofing;
- no custom network replacement;
- net counters remain stable under FFA combat.

## Performance and safety gates

| ID | Test | Pass criteria |
| --- | --- | --- |
| FFA-SAFE-001 | No-interest hook install | Hooks installed with no policy enabled have negligible counters/timing. |
| FFA-SAFE-002 | Count-only probe overhead | Target/damage/output probes do not introduce measurable frame stalls in a profiled session. |
| FFA-SAFE-003 | Mutation rollback | Every gated write has applied/restored equality and no process crash. |
| FFA-SAFE-004 | Strict recursion smoke | Strict live smoke passes with no recursive callbacks. |
| FFA-SAFE-005 | Gameplay recursion accounting | Manual sessions may recurse, but per-hook recursion counters explain where. |
| FFA-SAFE-006 | Artifact hygiene | No full live logs/ETL/generated run dirs committed; only curated redacted artifacts. |

## Minimum pass set before enabling an experimental FFA mode

1. FFA-POL-001 through FFA-POL-014 pass.
2. FFA-AUTO-001 through FFA-AUTO-004 pass.
3. FFA-PROBE-001 through FFA-PROBE-006 have current-build artifacts.
4. FFA-MUT-001 through FFA-MUT-010 are implemented and pass for the selected target boundary.
5. FFA-GAME-001 through FFA-GAME-004 pass in a manual session.
6. FFA-SAFE-001 through FFA-SAFE-006 pass.

Only after that should DWRT expose an experimental public surface such as:

```txt
TeamPolicy
TargetPolicy
DamagePolicy
ScorePolicy
BotPolicy
```
