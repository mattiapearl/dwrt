# Team, target, and damage layers

Date: 2026-05-31

## Why this note exists

Friendly-fire experiments showed that `CBaseEntity::TakeDamageOld` is not enough to understand or override all "can I hit this thing?" decisions.

Runtime observations:

- `mp_friendlyfire 1` alone did not allow same-side objective damage in the tested local session.
- A blind objective victim-team spoof inside `TakeDamageOld` broke normal enemy-objective damage because enemy objectives were temporarily flipped to the shooter's team.
- A local-team-scoped objective victim-team spoof restored enemy-objective damage but still did not allow own-objective damage.
- In the scoped run, objective candidates reached `TakeDamageOld`, but `teamSpoofApplied=0` with `localTeam=2`, meaning the damage reaching the hook was not on local-team-2 objectives. Own-objective attempts appear to be filtered before this hook.

Conclusion: some same-team/friendly rejection happens before `TakeDamageOld`, at target/hit/ability validation layers.

2026-05-31 proof update:

- Static RE identified a concrete pre-damage friendly-fire target filter at old-build `0x1818d8f30` and an immediate caller at old-build `0x1807838d0`.
- Current build `server.dll` timestamp `0x6a1a1a1d`, FNV-1a64 `0x9724af8730be14e3`, shifts those unique signatures to:
  - `CitadelTargetFilter::FriendlyFire` expected RVA `0x018d9180`.
  - `CitadelTargetFilter::FriendlyFireCaller` expected RVA `0x007839d0`.
- DWRT host resolves and can install count-only detours for both functions under required signatures.
- Resolver proof: `scripts/smoke-dwrt-host.ps1 -NoProfile -MappedModuleCheck` must pass with both target-filter signatures unique and expected-RVA-correct.
- Live install proof: `scripts/smoke-dwrt-live-server.ps1 -NoProfile -InstallProbeHooks -HoldSeconds 5 -ProbeMountMask 7` must pass with `hookInstallAttempts=5`, `hooksInstalled=5`, `hookInstallFailures=0`, and target-probe snapshot export working.

## Layer model

### 1. Engine team/state fields

Current-build evidence:

```txt
CBaseEntity::m_iTeamNum = entity + 0x33c
```

Observed/expected Citadel teams:

```txt
2 = Amber/Rebel style team
3 = Sapphire/Combine style team
```

These are implementation facts. Public DWRT APIs should use schema-backed `Entity.team()` style access, not raw offsets.

Teams are not just a damage concern. They likely feed:

- target filters;
- objective ownership;
- minimap/UI;
- spawn/lane systems;
- rewards and assist credit;
- bot perception and objective assignment;
- game-rules arrays and GC/signout metadata.

Do not start FFA by assigning arbitrary unique engine teams per player.

### 2. Target categories and masks

Citadel has explicit target categories separate from raw team numbers.

Static enum evidence includes `target/analysis/re/citadel_unit_target_type_values.tsv`:

```txt
CITADEL_UNIT_TARGET_HERO_FRIENDLY      0x00001
CITADEL_UNIT_TARGET_TROOPER_FRIENDLY   0x00002
CITADEL_UNIT_TARGET_BOSS_FRIENDLY      0x00004
CITADEL_UNIT_TARGET_BUILDING_FRIENDLY  0x00008
CITADEL_UNIT_TARGET_PROP_FRIENDLY      0x00010
CITADEL_UNIT_TARGET_MINION_FRIENDLY    0x00020
CITADEL_UNIT_TARGET_HERO_ENEMY         0x00100
CITADEL_UNIT_TARGET_TROOPER_ENEMY      0x00200
CITADEL_UNIT_TARGET_BOSS_ENEMY         0x00400
CITADEL_UNIT_TARGET_BUILDING_ENEMY     0x00800
CITADEL_UNIT_TARGET_NEUTRAL            0x010000
CITADEL_UNIT_TARGET_ALL_FRIENDLY       0x00003f
CITADEL_UNIT_TARGET_ALL_ENEMY          0x013f00
CITADEL_UNIT_TARGET_ALL                0x013f3f
```

Target flags evidence includes `target/analysis/re/citadel_unit_target_flag_values.tsv`:

```txt
CITADEL_UNIT_TARGET_FLAG_PENETRATE_INVULNERABLE 0x2
CITADEL_UNIT_TARGET_FLAG_NO_INVIS               0x4
CITADEL_UNIT_TARGET_FLAG_NO_DORMANT_NEUTRALS    0x8
CITADEL_UNIT_TARGET_FLAG_ALLOW_BREAKABLES       0x10
CITADEL_UNIT_TARGET_FLAG_ALLOW_SMALL_DEPLOYABLES 0x20
```

Loader/schema xrefs show these are not dead strings: ability/VData loaders read target type and flags at offsets such as `+0x348/+0x368`, `+0x1c8/+0x1e8`, `+0x88/+0xa8`, and `m_nAbilityTargetTypes` / flags at `+0x754/+0x758` in the current decompiled artifacts.

This means ability/targeting logic can reject same-team entities before damage exists, even if a later damage policy would allow it.

### 3. Hit/trace/weapon validation

Static RE found `mp_friendlyfire` usage in a pre-`TakeDamageOld` hit-filter style function:

```txt
mp_friendlyfire registration: 0x1801ba440
mp_friendlyfire usage:       0x1818d8f30
caller candidate:            0x1807838d0
secondary usage candidate:   0x181aed8c0
```

Direct decompile proof:

- `target/analysis/re/target_decompiled/1801ba440_FUN_1801ba440.c` registers `mp_friendlyfire` with description `Allows team members to injure other members of their team`.
- `target/analysis/re/convar_usage_decompiled/1818d8f30_FUN_1818d8f30.c` reads `DAT_183a31338` (`mp_friendlyfire`); when the ConVar byte is false and source vfunc `+0xcb0(source, target)` returns true, it returns `false` before any damage application.
- The same function also checks a target bitset argument and rejects when the target identity bit is not present.
- `target/analysis/re/caller_mpff_decompiled/1807838d0_FUN_1807838d0.c` calls target vfunc `+0x200`; if the returned unit type is `0x1a`, it bypasses and returns allow, otherwise it calls `0x1818d8f30`.
- `target/analysis/re/convar_usage_decompiled/181aed8c0_FUN_181aed8c0.c` is another friendly-fire gate candidate: for a vfunc `+0x260` result of `1`, friendly-fire disabled, and `param_3 != param_2`, it returns a false-ish value before falling through to `FUN_1819d4200`.

Inferred behavior: if friendly-fire is disabled and a same-team predicate reports true, the filter rejects. Runtime showed that just setting `mp_friendlyfire 1` did not solve own-objective/team damage, so either:

- not all paths use that convar;
- objective/player same-team rejection happens in another target filter;
- the ConVar does not affect the relevant Citadel-specific path in this server mode;
- ability/weapon VData target masks reject earlier.

### 4. Damage creation / `CTakeDamageInfo`

`TakeDamageOld` assumes a `CTakeDamageInfo` already exists. Current static schema metadata indicates offsets:

```txt
CTakeDamageInfo::m_hInflictor      0x38
CTakeDamageInfo::m_hAttacker       0x3c
CTakeDamageInfo::m_flTotalledDamage 0x48
CTakeDamageInfo::m_bitsDamageType  0x4c
```

These are CEntityHandle-style handles, not direct pointers. Rewriting attacker/issuer safely requires resolving or constructing valid handles and understanding whether the specific filter already happened.

Handle resolver proof from current RE:

```txt
invalid handle = 0xffffffff
entry index    = handle & 0x7fff
chunk index    = (handle & 0x7fff) >> 9
slot index     = handle & 0x1ff
chunk table    = DAT_1832b3280
identity       = DAT_1832b3280[chunk] + slot * 0x70
valid iff *(uint32*)(identity + 0x10) == handle
entity pointer = *(void**)identity
```

`target/analysis/re/deadworks_signature_decompiled/180c6ba60_FUN_180c6ba60.c` demonstrates this resolver pattern while reading `param_2 + 0x38` in `TakeDamageOld`. The damage-info constructor/helper also writes source metadata before `TakeDamageOld`; static disassembly observed attacker/inflictor team cached around `info + 0xd8`, so late attacker-team flips inside `TakeDamageOld` do not necessarily update all already-cached source policy fields.

Attacker spoofing inside `TakeDamageOld` can only affect damage that already reached damage application. It cannot make a rejected friendly hit exist.

### 5. Damage application / `TakeDamageOld`

`CBaseEntity::TakeDamageOld` remains useful for:

- blocking/scaling accepted damage;
- adjusting damage info after accepted hit creation;
- custom score/virtual team policies;
- objective/NPC immunities;
- telemetry.

It is not sufficient for all same-team/friendly target acceptance.

### 6. Game rules, rewards, bots, UI

Even if targeting and damage are made to work, true FFA needs more than damage:

- kill credit and assist logic;
- objective ownership/reward logic;
- bot enemy selection;
- UI/minimap/team color assumptions;
- spawn/lane assignment;
- match-end logic.

This is why policy FFA should precede true engine FFA.

## Midboss / neutral model

Static evidence shows MidBoss and neutral entities are first-class categories, not merely ordinary team-2/team-3 objectives.

MidBoss strings/classes/modifiers include:

```txt
CNPC_MidBoss
CInfoMidBossSpawn
CTriggerMidBossShield
CCitadel_Modifier_CanDamageMidBoss
CCitadel_Modifier_MidBossAggroEnemy
midboss_modifier_damage_resistance
```

Neutral evidence includes:

```txt
CITADEL_UNIT_TARGET_NEUTRAL = 0x010000
info_neutral_trooper_camp
info_neutral_trooper_spawn
citadel_neutral_spawn_enabled
CNPC_TrooperNeutralVData
CNPC_Neutral_* classes
modifier_neutral_shield
modifier_neutral_aggro
```

Interpretation: neutral/midboss damage is likely controlled by target category, special modifiers/triggers, aggro state, and damage-resistance logic, not just by `m_iTeamNum`.

## Does DWRT need to rewrite game logic?

Not wholesale.

DWRT should not replace Deadlock game rules, networking, prediction, snapshots, or full entity simulation.

But FFA/same-team combat likely requires a multi-layer policy overlay:

```txt
TargetPolicy   -> make intended targets selectable/hittable
DamagePolicy   -> block/scale/relabel accepted damage
TeamPolicy     -> virtual teams and same-team/enemy interpretation
ScorePolicy    -> custom kill/objective scoring
BotPolicy      -> bot target selection only if bots are part of the mode
```

The right approach is curated hooks/facades at specific engine boundaries, not arbitrary memory writes.

## DWRT target-filter probe added

Files:

```txt
native/dwrt-host/dwrt_target_probe.hpp
native/dwrt-host/dwrt_target_probe.cpp
native/dwrt-host/dwrt_probe_manifest.cpp
native/dwrt-host/dwrt_host.cpp
native/dwrt-host/inject_smoke.cpp
```

The probe is count-only by default. It snapshots:

```txt
filterCalls/filterAllowed/filterDenied
filterSameTeamCalls/filterSameTeamAllowed/filterSameTeamDenied
filterObjectiveTargetCalls/filterObjectiveTargetAllowed/filterObjectiveTargetDenied
filterNeutralTargetCalls/filterMidbossTargetCalls
callerCalls/callerAllowed/callerDenied/callerUnit0x1aBypassCandidates
```

Optional gated experiment:

```txt
DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF=1
```

This only tries a temporary source-team flip while inside `CitadelTargetFilter::FriendlyFire`, then restores immediately after the original filter returns. It is not a public API and is only for disposable proof sessions.

## Next research hooks

1. Run a manual connected gameplay session with the target-filter probe enabled and compare:
   - own-objective/team shots;
   - enemy-objective shots;
   - `mp_friendlyfire 0` vs `mp_friendlyfire 1`;
   - optional `DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF=1`.
2. Validate a CEntityHandle-to-entity resolver for `CTakeDamageInfo::m_hAttacker` and `m_hInflictor` in DWRT host code before any attacker mutation experiment.
3. Add an attacker-team spoof experiment only for accepted damage paths, and account for cached source team at damage-info construction.
4. Find the target/trace validation boundary for bullet hits against players/objectives.
5. Find the ability target-mask validation boundary separately; do not globally patch VData.
6. Probe neutral/midboss damage path by logging class/designer/team/target category when MidBoss damage reaches `TakeDamageOld`.
