# FFA in-game test matrix

Date: 2026-06-01

Purpose: prove, in a real Deadlock server with real clients, which DWRT ideas can produce a true FFA-like mode.

This is **not** a code/unit-test list. These are gameplay tests that require a connected client and, for several tests, at least two players or practice bots.

## What must be proven

A true FFA-like experience needs all of these to work together:

1. **Targeting / hit existence**: can the attacker even hit/select the target?
2. **Damage creation**: does a `CTakeDamageInfo` get created?
3. **Damage application**: does health/objective HP actually change?
4. **Attribution**: does kill/objective/damage credit point to the right attacker?
5. **Scoring / rewards**: does the desired FFA score policy happen even if engine teams remain 2/3?
6. **Map/objective logic**: do Guardians/Walkers/Patron, lanes, neutrals, MidBoss, spawns, and win conditions remain coherent?
7. **Bot/AI behavior**: can bots target FFA enemies if needed?
8. **Client/UI/network safety**: no client/server crash, broken respawn, broken snapshots, or unusable UI.
9. **Performance/safety**: hooks stay bounded, restores happen, no recursive runaway.

## Evidence to collect for every in-game test

For each test, save or note:

```txt
run directory
server build timestamp / FNV
scenario id
map
players / heroes / teams
server args / ConVars
DWRT experiment switches
exact action performed
visible result
snapshot before/action/after
pass/fail/blocked
notes/video/screenshot if available
```

Minimum DWRT artifacts:

```txt
dwrt-manual-probe-session.json
dwrt-live-injector.json
dwrt-manual-probe-snapshots.jsonl
manual-probe-session.log
manual-probe-session.err.log
```

Key counters to inspect:

```txt
probe.damageSeen
probe.entityOutputSeen
targetProbe.filterCalls
targetProbe.filterAllowed
targetProbe.filterDenied
targetProbe.filterSameTeamCalls
targetProbe.filterSameTeamAllowed
targetProbe.filterSameTeamDenied
targetProbe.filterObjectiveTargetCalls
targetProbe.filterObjectiveTargetAllowed
targetProbe.filterObjectiveTargetDenied
targetProbe.callerCalls
targetProbe.callerAllowed
targetProbe.callerDenied
targetProbe.callerUnit0x1aBypassCandidates
targetProbe.sourceSpoofAttempts
targetProbe.sourceSpoofApplied
targetProbe.sourceSpoofRestored
friendlyFire.objectiveCandidates
friendlyFire.teamSpoofAttempts
friendlyFire.teamSpoofApplied
friendlyFire.teamSpoofRestored
snapshot.callbackRecursiveEntries
snapshot.callbackMaxDepth
```

## Session profiles

Use `scripts/start-dwrt-ffa-ingame-test-session.ps1` to start these.

| Profile | Purpose |
| --- | --- |
| `Baseline` | Stock-ish DWRT probes only, no FFA mutation. |
| `MpFriendlyFire0` | Explicit `mp_friendlyfire 0` baseline. |
| `MpFriendlyFire1` | Prove what the engine ConVar alone changes. |
| `SourceTeamSpoof` | Gated source-team spoof at target filter. |
| `ObjectiveVictimSpoofTeam2` | Current objective-victim spoof scoped to team 2. |
| `ObjectiveVictimSpoofTeam3` | Same scoped spoof for team 3 ambiguity. |
| `BotPractice1v1` | Shipped practice bot harness. |
| `BotPractice2v2Guided` | Shipped guided bot harness. |
| `SourceTeamSpoofBots` | Source spoof + guided bots. |

## Actor notation

```txt
P1 = local human player
P2 = same-engine-team human player or bot
P3 = opposite-engine-team human player or bot
OBJ-SAME = P1 team's Guardian/Walker/base objective
OBJ-ENEMY = enemy team's Guardian/Walker/base objective
TROOPER-SAME = P1 team's lane trooper
TROOPER-ENEMY = enemy lane trooper
NEUTRAL = jungle/neutral camp entity
MIDBOSS = MidBoss entity/objective
DEPLOYABLE = player-created deployable/minion if available
```

## Phase A — target and hit existence

These answer: “does the hit/selection exist before damage?”

| ID | Profile(s) | Actors | Action | Pass evidence | Failure meaning |
| --- | --- | --- | --- | --- | --- |
| FFA-A01 | Baseline, MpFriendlyFire0, MpFriendlyFire1 | P1 -> P3 | Shoot enemy player. | Damage reaches target; `damageSeen` increases; target filter allows or is bypassed. | Hook/probe issue or wrong setup. |
| FFA-A02 | Baseline, MpFriendlyFire0, MpFriendlyFire1 | P1 -> P2 | Shoot same-team player. | If playable FFA works, hit exists and damage reaches target. If not, counters show where denied. | If no target counters and no damage, rejection is earlier than current filter. |
| FFA-A03 | SourceTeamSpoof | P1 -> P2 | Shoot same-team player. | `sourceSpoofApplied == sourceSpoofRestored`; same-team allowed/damage improves vs baseline. | If no improvement, target masks/trace earlier than source-team predicate. |
| FFA-A04 | Baseline, MpFriendlyFire1 | P1 -> OBJ-ENEMY | Shoot enemy Guardian/Walker. | Enemy objective remains hittable and damage reaches `TakeDamageOld`. | Regression; do not ship mutation. |
| FFA-A05 | Baseline, MpFriendlyFire1 | P1 -> OBJ-SAME | Shoot own Guardian/Walker. | Either damage reaches hook, or target filter counters show denial. | If no counters/no damage, need earlier ability/trace/mask hook. |
| FFA-A06 | SourceTeamSpoof | P1 -> OBJ-SAME | Shoot own Guardian/Walker. | Source spoof increases allow/damage if source-team predicate is live gate. | If no effect, objective mask/category rejection is elsewhere. |
| FFA-A07 | ObjectiveVictimSpoofTeam2/3 | P1 -> OBJ-SAME | Shoot own objective. | Own-objective damage reaches hook only if accepted path exists; restore counters match. | Victim spoof is too late if own hits never reach damage. |
| FFA-A08 | Baseline | P1 -> TROOPER-SAME | Shoot same-team lane trooper. | Proves whether friendly troopers use hero/objective-like rejection. | Helps classify target categories. |
| FFA-A09 | Baseline | P1 -> TROOPER-ENEMY | Shoot enemy trooper. | Enemy trooper stays normal. | Regression. |
| FFA-A10 | Baseline | P1 -> NEUTRAL | Shoot neutral camp. | `NEUTRAL` behavior stays distinct; target unit type/counters recorded. | Neutral is not just team 2/3. |
| FFA-A11 | Baseline | P1 -> MIDBOSS | Shoot MidBoss. | MidBoss target/damage behavior recorded; `callerUnit0x1aBypassCandidates` checked. | Needed before “midboss-like player” idea. |
| FFA-A12 | SourceTeamSpoof | P1 -> MIDBOSS/NEUTRAL | Shoot MidBoss/neutral. | Source spoof must not break neutral/MidBoss. | Source spoof too broad/unsafe. |

## Phase B — damage application and health/HP changes

These answer: “if hit exists, does real damage happen?”

| ID | Profile(s) | Actors | Action | Pass evidence |
| --- | --- | --- | --- | --- |
| FFA-B01 | Baseline | P1 -> P3 | Record enemy player HP before/after burst. | HP decreases; `damageSeen` increases. |
| FFA-B02 | MpFriendlyFire1 | P1 -> P2 | Record same-team player HP before/after burst. | HP decreases if ConVar is enough; otherwise no HP change plus rejection evidence. |
| FFA-B03 | SourceTeamSpoof | P1 -> P2 | Repeat same-team burst. | HP change improves vs B02, or proves source spoof insufficient. |
| FFA-B04 | Baseline | P1 -> OBJ-ENEMY | Record enemy objective HP/visible state. | HP/state changes and `friendlyFire.objectiveCandidates` increments. |
| FFA-B05 | MpFriendlyFire1 | P1 -> OBJ-SAME | Record own objective HP/visible state. | If HP unchanged, identify target/damage layer that blocked. |
| FFA-B06 | SourceTeamSpoof | P1 -> OBJ-SAME | Repeat own objective burst. | HP change only if source-team predicate was the gate. |
| FFA-B07 | ObjectiveVictimSpoofTeam2/3 | P1 -> OBJ-ENEMY and OBJ-SAME | Compare both objective paths. | Enemy objective damage must not regress; own objective result documented. |
| FFA-B08 | Baseline | P1 -> NEUTRAL | Damage neutral camp. | Neutral HP/aggro changes normally. |
| FFA-B09 | Baseline | P1 -> MIDBOSS | Damage MidBoss with and without required conditions/modifiers. | Identifies MidBoss-specific gates. |
| FFA-B10 | Any mutation profile | P1 self damage if possible | Try self-damage ability/item/environment. | Self policy documented; no accidental self-as-enemy unless intended. |

## Phase C — kill/death/attribution

These answer: “who gets credit?”

Instrumentation gap: DWRT currently needs better game-event/score probes for full automation. Until then, collect visible UI/scoreboard/video plus server events if available.

| ID | Profile(s) | Actors | Action | Pass evidence | Required future instrumentation |
| --- | --- | --- | --- | --- | --- |
| FFA-C01 | Baseline | P1 kills P3 | Enemy-team kill. | Stock kill credit works. | game event kill/death probe. |
| FFA-C02 | MpFriendlyFire1, SourceTeamSpoof | P1 kills P2 | Same-team kill. | Attacker credited, victim dies/respawns, no crash. | kill attribution event, attacker handle resolver. |
| FFA-C03 | SourceTeamSpoof | P2 kills P1 | Reverse same-team kill. | Symmetric credit. | same as above. |
| FFA-C04 | SourceTeamSpoof | P1 damages P2, P3 kills P2 | Assist attribution. | Assist policy understood or DWRT-owned score planned. | assist/game stats probe. |
| FFA-C05 | Any FFA path | P1 damages self/environment death | Self/teamkill attribution. | Not credited as invalid enemy unless policy says. | death reason probe. |
| FFA-C06 | Objective enabled profile | P1 destroys OBJ-SAME | Own objective attribution. | Credit/reward either correct or deliberately suppressed/remapped. | objective event/reward probe. |
| FFA-C07 | Objective enabled profile | P1 damages OBJ-ENEMY | Enemy objective attribution. | No regression from stock. | objective event/reward probe. |
| FFA-C08 | SourceTeamSpoof | simultaneous same-team kills | Two same-team fights at once. | Attribution does not cross-contaminate attackers. | per-hit attacker handle log. |

## Phase D — scoring, economy, and rewards

These answer: “does the match economy become nonsense?”

| ID | Profile(s) | Action | Pass evidence |
| --- | --- | --- | --- |
| FFA-D01 | Baseline | P1 kills P3. | Stock souls/gold/score change understood. |
| FFA-D02 | SourceTeamSpoof | P1 kills P2. | Rewards either occur correctly or are blocked and DWRT ScorePolicy requirement is proven. |
| FFA-D03 | SourceTeamSpoof | P1 repeatedly kills P2. | No negative team penalty or escalating invalid state unless documented. |
| FFA-D04 | Objective enabled | P1 damages/destroys OBJ-SAME. | Reward/win-condition effects understood; no accidental instant loss unless expected. |
| FFA-D05 | Objective enabled | P1 damages/destroys OBJ-ENEMY. | Stock objective reward unchanged. |
| FFA-D06 | Neutral/MidBoss | P1 takes neutral/MidBoss reward. | Neutral/MidBoss rewards remain coherent. |
| FFA-D07 | Multi-player FFA | all players kill each other for 5 minutes. | Scoreboard can be interpreted or DWRT-owned scoring requirement documented. |

## Phase E — map, objectives, lanes, and win condition

These answer: “does the map still function?”

| ID | Profile(s) | Action | Pass evidence |
| --- | --- | --- | --- |
| FFA-E01 | Baseline | Observe lane troops fighting. | Normal lane behavior unaffected by probes. |
| FFA-E02 | SourceTeamSpoof | Same observation during player FFA. | Lane AI does not become globally hostile to allies accidentally. |
| FFA-E03 | Objective enabled | Damage/destroy Guardian/T1. | Lane/objective state transitions occur or are explicitly blocked. |
| FFA-E04 | Objective enabled | Damage/destroy Walker/T2. | Walker state, pathing, minimap, damage phases remain coherent. |
| FFA-E05 | Objective enabled | Damage shrine/base/Patron if possible. | Win condition behavior observed; no server crash. |
| FFA-E06 | Any FFA profile | Enter enemy/own base areas. | Spawn protection/base triggers do not softlock players. |
| FFA-E07 | Any FFA profile | Use zipline/objective adjacent movement. | Movement systems not broken by team policy. |
| FFA-E08 | Any FFA profile | Break props/crates. | Breakable target masks unaffected. |
| FFA-E09 | Any FFA profile | MidBoss spawn/death. | MidBoss special modifiers/triggers unaffected. |
| FFA-E10 | Any FFA profile | Neutral camps spawn/respawn. | Neutral camp timers/aggro unaffected. |

## Phase F — bot and AI policy

These answer: “can bots participate in FFA?”

Use shipped configs first:

```txt
citadel_botmatch_practice_1v1.cfg
citadel_botmatch_practice_2v2_guided.cfg
```

| ID | Profile(s) | Action | Pass evidence |
| --- | --- | --- | --- |
| FFA-F01 | BotPractice1v1 | Spawn bots and observe combat. | Bots spawn and attack stock enemies. |
| FFA-F02 | BotPractice2v2Guided | Observe guided bot lanes. | Bots follow normal lane/objective behavior. |
| FFA-F03 | SourceTeamSpoofBots | Put bot and player on same engine team if possible. | Bot target selection either attacks virtual enemy or proves BotPolicy hook needed. |
| FFA-F04 | SourceTeamSpoofBots | Bot damages same-team player. | Damage path counters explain whether bot attack uses same filters. |
| FFA-F05 | BotPractice2v2Guided | Bot attacks objectives. | Objective targeting not regressed. |
| FFA-F06 | BotPractice2v2Guided | Bot interacts with neutral/MidBoss. | Neutral/MidBoss AI remains coherent. |
| FFA-F07 | Any bot FFA | 10 minute bot session. | No AI crash, no runaway recursion/callbacks. |

## Phase G — client UI and map information

These answer: “can humans understand and play it?”

| ID | Profile(s) | Action | Pass evidence |
| --- | --- | --- | --- |
| FFA-G01 | Baseline | Look at P2/P3. | Stock ally/enemy healthbar/color/crosshair behavior recorded. |
| FFA-G02 | SourceTeamSpoof | Look at same-team FFA target P2. | UI may still show ally; damage behavior documented. |
| FFA-G03 | SourceTeamSpoof | Damage P2. | Hit feedback/damage numbers/indicators documented. |
| FFA-G04 | SourceTeamSpoof | P2 damages P1. | Incoming damage indicator works or limitation documented. |
| FFA-G05 | SourceTeamSpoof | Kill P2. | Killfeed/scoreboard display documented. |
| FFA-G06 | Objective enabled | Damage own objective. | Objective UI/minimap state documented. |
| FFA-G07 | Any FFA profile | Use pings/comms near ally-as-enemy. | Ping/team UI assumptions documented. |
| FFA-G08 | Any FFA profile | Spectator/replay if available. | Spectator does not crash; team coloring limitation documented. |
| FFA-G09 | Any FFA profile | Disconnect/reconnect. | Reconnected client sees coherent state. |
| FFA-G10 | Any FFA profile | Late join if possible. | Late join state coherent. |

## Phase H — networking, snapshot, and session safety

These answer: “did we accidentally rely on server replacement behavior?”

| ID | Profile(s) | Action | Pass evidence |
| --- | --- | --- | --- |
| FFA-H01 | Baseline | Connect/disconnect single client. | No server crash, snapshots normal. |
| FFA-H02 | SourceTeamSpoof | Two clients fight 5 minutes. | No client/server crash; net stable. |
| FFA-H03 | Objective enabled | Objective fights 5 minutes. | No snapshot/entity assert. |
| FFA-H04 | BotPractice2v2Guided | Bots + human 10 minutes. | No process exit; counters bounded. |
| FFA-H05 | Any mutation profile | Stop session by stop-file. | Server process tree cleaned up. |
| FFA-H06 | Any mutation profile | Rebuild after stop. | No locked `dwrt_host.dll`. |

## Phase I — stress and regression

These answer: “is this safe enough to iterate?”

| ID | Profile(s) | Action | Pass evidence |
| --- | --- | --- | --- |
| FFA-I01 | Baseline | No player, 5 minute idle. | Hook counters low; no target/damage spam. |
| FFA-I02 | SourceTeamSpoof | Two players spam same-team shots. | Applied/restored counts match; no crash. |
| FFA-I03 | SourceTeamSpoof | P1/P2/P3 all fight. | Attribution/counters do not cross-contaminate. |
| FFA-I04 | Objective enabled | Fight near multiple objectives. | Enemy objective damage not regressed. |
| FFA-I05 | BotPractice2v2Guided | Bot + player chaos. | Callback recursion/timing acceptable. |
| FFA-I06 | Any FFA profile | Profiled 10+ minute run. | ETW/profile reviewed; no hot-path stall. |

## Idea-specific verdict tests

These map directly to the ideas we are considering.

### Idea 1: `mp_friendlyfire 1` only

Required tests:

```txt
FFA-A01, A02, A04, A05
FFA-B01, B02, B04, B05
FFA-C01, C02 if same-team damage works
```

Verdict:

- Works for player FFA only if same-team player hits create damage and attribution is acceptable.
- Does not solve objectives if own objectives still do not reach damage.

### Idea 2: source-team spoof in target filter

Required tests:

```txt
FFA-A03, A06, A12
FFA-B03, B06
FFA-C02, C03, C08
FFA-I02, I03
```

Verdict:

- Candidate for targeted player FFA if same-team player hits become allowed and restore counts match.
- Unsafe if it leaks to neutral/MidBoss/objectives or attribution breaks.

### Idea 3: victim objective-team spoof in `TakeDamageOld`

Required tests:

```txt
FFA-A04, A05, A07
FFA-B04, B05, B07
FFA-D04, D05
FFA-E03, E04, E05
```

Verdict:

- Only useful for damage that already reaches `TakeDamageOld`.
- Not a solution for own objectives if same-team objective shots never create damage.

### Idea 4: hero mask translation (`HERO_FRIENDLY -> HERO_ENEMY` virtual policy)

Required tests after implementation:

```txt
FFA-A02, A03
FFA-B02, B03
FFA-C02, C03, C04, C08
FFA-G01 through G05
FFA-H02
```

Verdict:

- Preferred player-FFA direction if it can be applied at the correct target/hit boundary.
- Keeps real engine teams intact.

### Idea 5: objective mask translation (`BOSS/BUILDING_FRIENDLY -> *_ENEMY` virtual policy)

Required tests after implementation:

```txt
FFA-A05, A06
FFA-B05, B06
FFA-D04, D05
FFA-E03, E04, E05
```

Verdict:

- Must be separate from player FFA.
- Do not enable by default unless own-objective gameplay is intended.

### Idea 6: make players “midboss-like”

Safe version only: virtual target overlay, not class/entity mutation.

Required tests after implementation:

```txt
FFA-A11
FFA-B09
FFA-C02/C03 under overlay
FFA-E09
FFA-G01 through G05
```

Verdict:

- Acceptable only if players remain `CCitadelPlayerPawn`/hero/controller-owned entities.
- Rejected if it requires mutating player class, vtable, pawn identity, networking class, or true NPC/MidBoss systems.

### Idea 7: attacker/team spoof inside `TakeDamageOld`

Required tests after safe handle resolver:

```txt
FFA-B01 through B07
FFA-C01 through C08
FFA-D01 through D07
```

Verdict:

- Too late to create rejected hits.
- May still be useful for attribution/score experiments on accepted damage, but must account for cached source metadata in `CTakeDamageInfo`.

## Decision tree after a test run

```txt
No hit feedback, no target counters, no damage:
  -> earlier ability/trace/target-mask boundary needed.

Target counters show same-team denied:
  -> current target filter can be policy point.

Source spoof changes denied -> allowed:
  -> source/team predicate is live gate.

Source spoof no effect but target counters move:
  -> mask/category bitset likely gate.

Damage reaches TakeDamageOld but HP does not change:
  -> damage application/game-rules policy gate.

HP changes but kill/score wrong:
  -> ScorePolicy/attribution hook needed.

Gameplay works but UI misleading:
  -> UI/map information policy or documentation needed.

Bots do not attack virtual enemies:
  -> BotPolicy hook needed; target/damage alone is insufficient.
```

## Minimum evidence before implementing real FFA mode

Do not proceed to public FFA APIs until current-build artifacts exist for:

```txt
FFA-A01 through A12
FFA-B01 through B10
FFA-C01 through C08, even if some are marked “requires new instrumentation”
FFA-D01 through D07
FFA-E01 through E10
FFA-F01 through F07 if bots are part of the mode
FFA-G01 through G10
FFA-H01 through H06
FFA-I01 through I06
```

A partial experimental mode may proceed earlier only if its scope is explicitly narrower, e.g.:

```txt
player damage only, no own objectives, DWRT scoring only, bots unsupported
```
