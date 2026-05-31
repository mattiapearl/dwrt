# Map/NPC/Pulse/FFA reverse-engineering pass

Date: 2026-05-30

Target binary:

- `server.dll` from Deadlock `game/citadel/bin/win64`
- MD5: `a7936e6e286f51b51763475e9b2f4123`
- SHA-256: `299b6237b36a1b3ea15cac10b144f55603cb6c1020011ab122351695516a1897`

Local analysis artifacts, not intended for git:

- `target/analysis/re/server_re_strings_xrefs.tsv`
- `target/analysis/re/deadworks_signature_hits.tsv`
- `target/analysis/re/deadworks_signature_decompiled/`
- `target/analysis/re/target_decompiled/`
- `target/analysis/re/map/dl_midtown_vpk_list.txt`
- `target/analysis/re/map/dl_midtown.vmap_c`
- `target/analysis/re/map/dl_midtown.vpulse_c`

## Executive findings

1. Map entities are normal Source 2 runtime entities once spawned. The server handles them through `CEntityInstance` / `CBaseEntity`, the entity system, entity I/O, touch outputs, game rules, and Pulse graph instances.
2. DWRT does not need to replace `.vmap_c` or Pulse to control many map/objective/NPC behaviors. Runtime hooks over entity lifecycle, entity I/O, touch, damage, modifiers, and curated spawn APIs are enough for the first map-script overlay.
3. Troopers/objectives are server-owned NPC classes (`CNPC_Trooper`, `CNPC_TrooperBoss`, `CNPC_Boss_Tier*`, `CNPC_BarrackBoss`, sentries). Map resources provide triggers/spawn points/brushes; server systems create and schedule the NPCs.
4. Trooper/neutral spawning is gated by convars and entity-level spawn state. `citadel_npc_spawn_enabled`, `citadel_trooper_spawn_enabled`, and `citadel_neutral_spawn_enabled` are real server gates.
5. Friendly-fire/FFA has two layers:
   - weapon/hit filtering has a real `mp_friendlyfire` check that rejects same-team hits when disabled;
   - ability targeting has separate Citadel target masks (`CITADEL_UNIT_TARGET_*_FRIENDLY/ENEMY`) and may still reject same-team targets unless masks/state are changed.
6. `MODIFIER_STATE_FRIENDLY_FIRE_ENABLED` exists as modifier state ordinal `0x76` and is checked by server code in at least one combat/event path, but this alone is not proof that it unlocks all same-team hit paths.
7. True engine FFA is still risky. The promising first path is policy FFA: keep engine teams stable, turn on/force friendly fire, log whether hits reach `TakeDamageOld`, then apply DWRT scoring/rules in the damage hook.

## Important function addresses for this build

These are implementation facts for this exact build, not public API.

| Symbol / inferred role | VA | Evidence |
|---|---:|---|
| `CBaseEntity::TakeDamageOld` | `0x180c6ba60` | Deadworks signature hit |
| `CEntityInstance::AcceptInput` | `0x181f176c0` | Deadworks signature hit |
| `CEntityIOOutput::FireOutputInternal` | `0x181f1cee0` | Deadworks signature hit |
| `CModifierProperty::AddModifier` | `0x1814d5d30` | Deadworks signature hit |
| `CTakeDamageInfo::Ctor` | `0x181addd40` | Deadworks signature hit |
| `CEntitySystem::CreateEntityByName` | `0x1817c33e0` | Deadworks signature hit |
| `CEntitySystem::QueueSpawnEntity` | `0x181f0ddb0` | Deadworks signature hit |
| `CEntitySystem::ExecuteQueuedCreation` | `0x181f06120` | Deadworks signature hit |
| `CCitadelGameRules::BuildGameSessionManifest` | `0x1808f7410` | Deadworks signature hit |
| `CCitadelPlayerPawn::AbilityThink` | `0x180a2e060` | Deadworks signature hit |
| `CCitadelGameRules::PostSpawnGroupLoad` inferred | `0x18093ea10` | string `PostSpawnGroupLoad: Marking map entities as spawned` |
| `CPulseGraphInstance_ServerEntity::{BeforeGraphStart,GraphStart}` inferred | `0x181a55ea0` | strings and call shape |
| `CPulseGraphInstance_ServerEntity::Think` inferred | `0x181a44ca0` | string and call shape |
| `mp_friendlyfire` registration | `0x1801ba440` | convar registration string |
| `mp_teamplay` registration | `0x1801bcc30` | convar registration string |
| `sv_friendly_dmg_immune` registration | `0x18007cbb0` | convar registration string |
| same-team hit filter using `mp_friendlyfire` inferred | `0x1818d8f30` | reads `mp_friendlyfire`, calls same-team vfunc, returns false |
| `citadel_npc_spawn_enabled` registration | `0x1800b5e50` | convar registration string |
| `citadel_trooper_spawn_enabled` registration | `0x1800b9f50` | convar registration string |
| trooper spawn enabled gate inferred | `0x180b8ab60` | checks `citadel_npc_spawn_enabled`, `citadel_trooper_spawn_enabled`, entity spawn bool |
| neutral camp/spawn loop inferred | `0x180bbe8d0` | scans `info_neutral_trooper_camp` and `info_neutral_trooper_spawn` |
| neutral camp per-camp schedule inferred | `0x180bf8ff0` | checks `citadel_npc_spawn_enabled`, `citadel_neutral_spawn_enabled` |
| `CNPC_Trooper` schema registration | `0x180c16f30` | class string and schema fields |
| `CNPC_Boss_Tier2` schema registration | `0x180bdcdf0` | class string and schema fields |
| killfeed NPC type classifier inferred | `0x180ad14f0` | returns Guardian/Walker/BarracksBoss/Titan localization tokens |

## How the server handles map entities

### Map load / post-spawn

`0x18093ea10` is a game-rules post-spawn-group path. It:

- checks whether a spawn group/map load path is active;
- scans `citadel_minimap_boundary` entities and derives map bounds;
- logs `CCitadelGameRules::PostSpawnGroupLoad: Marking map entities as spawned`;
- sets a game-rules byte at `CCitadelGameRules + 0x15c7` to mark map entities as spawned;
- scans `npc_barrack_boss` entities and counts them by lane/team into game-rules arrays;
- handles tutorial-controller cleanup and map-specific setup for `start`/`dl_midtown`/mode conditions.

This is strong evidence that map-owned objects become regular entity-system objects and are then indexed/scanned by class/designer name.

### Entity creation/spawn queue

Deadworks signatures resolved these entity-system functions:

- `CEntitySystem::CreateEntityByName` at `0x1817c33e0` delegates to an internal create-by-class/name path.
- `QueueSpawnEntity` at `0x181f0ddb0` appends entity handles/KV into a queued spawn vector.
- `ExecuteQueuedCreation` at `0x181f06120` drains queued entity creation and runs post-create work.

DWRT can use this as a curated facade later:

```txt
MapEntityCommand::spawn(classname, keyvalues)
MapEntityCommand::remove(handle)
MapEntityCommand::accept_input(handle, input, activator, caller, value)
```

but should not expose raw pointers or class factory internals.

### Entity I/O

`CEntityInstance::AcceptInput` at `0x181f176c0`:

1. resolves the input-name string into an internal symbol/id;
2. dispatches through the entity identity/input-handler path.

`CEntityIOOutput::FireOutputInternal` at `0x181f1cee0`:

1. notifies registered output listeners;
2. iterates output action records;
3. enqueues/fires target entity inputs with delay/variant value;
4. removes exhausted output records.

This confirms that input/output is a central map-logic boundary. Hooking these remains a high-value DWRT surface because Pulse, triggers, shops, teleporters, and scripted map chains converge here.

### Touch / trigger outputs

Schema registration around `0x180c3e810` and `0x1800b0320` shows trigger/touch input and output fields:

- `InputStartTouch` / `StartTouch`
- `InputEndTouch` / `EndTouch`
- `m_OnStartTouch`
- `m_OnStartTouchAll`
- `m_OnEndTouch`
- `m_OnEndTouchAll`
- `m_OnTouching`
- `m_OnNotTouching`

Example schema offsets seen in this build:

- one trigger class has `m_OnStartTouch` at `0x7f8`, `m_OnStartTouchAll` at `0x810`, `m_OnEndTouch` at `0x828`, `m_OnEndTouchAll` at `0x840`;
- another interact/trigger class has `m_OnStartTouch` at `0xce0`, `m_OnStartTouchAll` at `0xcf8`, `m_OnEndTouch` at `0xd10`, `m_OnEndTouchAll` at `0xd28`.

These offsets are internal facts only. The public DWRT API should expose `EntityTouchEvent` / `EntityOutputEvent`, not offset access.

## Map/Pulse resources

`dl_midtown.vmap_c` and VPK listing show map resources for runtime map logic:

- `668` resources under `maps/dl_midtown/entities/`;
- `131` resources containing `trigger`;
- `20` bounce-pad trigger resources;
- `4` teleport-trigger resources;
- `10` shop-trigger resources;
- `2` trooper detector resources;
- Amber/Sapphire spawn-block brushes;
- many interior/tiny/zap triggers.

Examples:

```txt
maps/dl_midtown/entities/amber_trooper_detector_14781_2206.vmdl_c
maps/dl_midtown/entities/sapphire_trooper_detector_14781_2208.vmdl_c
maps/dl_midtown/entities/center_shop_north_trigger_14781_2845.vmdl_c
maps/dl_midtown/entities/center_shop_south_trigger_14781_2842.vmdl_c
maps/dl_midtown/entities/*bounce_pad_trigger*.vmdl_c
maps/dl_midtown/entities/*teleport_trigger*.vmdl_c
maps/dl_midtown/entities/*shop_item_trigger*.vmdl_c
maps/dl_midtown/entities/amber_spawn_block_brush_*.vmdl_c
maps/dl_midtown/entities/sapphire_spawn_block_brush_*.vmdl_c
```

`pak01_dir.vpk` contains compiled Pulse resources:

```txt
pulse/maps/dl_midtown.vpulse_c
pulse/maps/prefabs/shop.vpulse_c
pulse/maps/prefabs/gameplay/small_route.vpulse_c
pulse/maps/dev/trooper_path_test.vpulse_c
```

Strings from `dl_midtown.vpulse_c` show:

- `CPulse`
- `Instance_ServerEntity`
- `Cell_Step_EntFire`
- `CCitadelPointPulseAPI::OnStreetBrawlEnterState`
- `amber_spawn_block_brush`
- `sapphire...`

Strings from `shop.vpulse_c` show `Step_EntFire` and `InputDisable`; `small_route.vpulse_c` shows `InputOpen`; `trooper_path_test.vpulse_c` shows `OnDamaged`, `spawn`, and `info_trooper_`.

Server strings and decompilation confirm `CPulseGraphInstance_ServerEntity` runs graph hooks:

- `BeforeGraphStart`
- `GraphStart`
- `Think`

Conclusion: Pulse is real map/game-mode logic, but it is still feeding the same engine entity/input/output systems. DWRT should first hook the entity/Pulse API boundaries, not patch Pulse bytecode.

## How the server handles troopers and objective NPCs

### NPC class inventory

Relevant server class strings include:

- `CNPC_Trooper`
- `CNPC_TrooperBoss`
- `CNPC_TrooperNeutral`
- `CNPC_Boss_Tier1`
- `CNPC_Boss_Tier2`
- `CNPC_Boss_Tier3`
- `CNPC_BarrackBoss`
- `CNPC_BaseDefenseSentry`
- `CNPC_FieldSentry`
- `CNPC_MortarSentry`
- `CNPC_MidBoss`

Killfeed classifier `0x180ad14f0` maps internal unit type returns to public labels:

| Unit type return | Killfeed token |
|---:|---|
| `4` | `#Citadel_Hud_KillFeedTrooper` |
| `5` | `#Citadel_Hud_KillFeedGuardian` |
| `6` | `#Citadel_Hud_KillFeedNeutral` |
| `0x16` / `0x17` | `#Citadel_Hud_KillFeedSentry` |
| `0x1d` | `#Citadel_Hud_KillFeedWalker` |
| `0x1e` | `#Citadel_Hud_KillFeedBarracksBoss` |
| `0x1f` | `#Citadel_Hud_KillFeedTitan` |

This supports the likely gameplay mapping:

- `npc_boss_tier1` / `CNPC_Boss_Tier1`: Guardian-class lane objective;
- `npc_boss_tier2` / `CNPC_Boss_Tier2`: Walker-class lane objective;
- `npc_boss_tier3` / `CNPC_Boss_Tier3`: Titan/Patron-class objective;
- `npc_barrack_boss` / `CNPC_BarrackBoss`: barracks boss.

The exact class-to-unit-type mapping should still be verified dynamically by logging `vfunc +0x200`/unit type for spawned objective entities.

### Trooper schema

`CNPC_Trooper` schema registration at `0x180c16f30` declares:

| Field | Type | Offset in this build |
|---|---|---:|
| `m_iLane` | int | `0x17a8` |
| `m_hTargetedEnemy` | `EHANDLE` | `0x1828` |
| `m_flHealingChargeParticlePct` | float | `0x182c` |

`CNPC_Boss_Tier2` schema registration at `0x180bdcdf0` declares:

| Field | Type | Offset in this build |
|---|---|---:|
| `m_iLane` | int | `0x17cc` |
| `m_hTargetedEnemy` | `EHANDLE` | `0x17d8` |
| `m_flFadeOutStart` | `GameTime_t` | `0x17dc` |
| `m_flFadeOutEnd` | `GameTime_t` | `0x17e0` |
| `m_flLastWeakpointHitTime` | `GameTime_t` | `0x17e4` |
| `m_vecElectricBeamLookTarget` | `VectorWS` | `0x1834` |
| `m_nElectricBeamCasts` | int | `0x1840` |

Again: these offsets should back internal schema facts only. Public API should expose typed fields after schema validation.

### Trooper spawn gates

Convars:

- `citadel_npc_spawn_enabled`: “set to false to prevent any NPC from spawning”
- `citadel_trooper_spawn_enabled`: “set to false to prevent any troopers from spawning”
- `citadel_neutral_spawn_enabled`: found in neutral camp code

`0x180b8ab60` is a strong candidate for `CCitadel_InfoTrooperSpawnAPI::GetIsSpawningEnabled` / spawn-enable evaluation. It checks:

- team-specific enable gates;
- global `citadel_npc_spawn_enabled`;
- global `citadel_trooper_spawn_enabled`;
- game state/mode conditions;
- entity-local spawn flag at `param_1 + 0x4c8`.

The Pulse API table references:

```txt
CCitadel_InfoTrooperSpawnAPI::GetIsSpawningEnabled
CCitadel_InfoTrooperSpawnAPI::SetSpawningEnabled
bSpawningEnabled
```

and points to Pulse binding records around `0x182fbf920` / `0x182fbf970`.

### Neutral camps

`0x180bbe8d0` scans:

- `info_neutral_trooper_camp`
- `info_neutral_trooper_spawn`

It links spawns to camps by string/name, writes spawn handles into camp vectors, schedules camp think, checks `citadel_npc_spawn_enabled` and `citadel_neutral_spawn_enabled`, then calls a spawn routine for each stored spawn handle.

`0x180bf8ff0` is the per-camp scheduling path. It:

- reads `citadel_npc_spawn_enabled`;
- reads `citadel_neutral_spawn_enabled`;
- computes current/next camp spawn state/time;
- schedules itself again with game time.

### Boss/objective spawn points

`0x180bb8b70` spawns boss-tier1 entities from a spawn-point object. It selects classname based on team:

```txt
team == 3 -> alt_npc_boss_tier1
else      -> npc_boss_tier1
```

It writes keyvalues before entity creation:

- `classname`
- `targetname`
- `origin`
- `angles`
- `teamnumber`
- `LagCompensate`
- `LaneNum`
- `CoverGroupID`
- `spawnflags`
- `SpawnPoint`
- `squadname` like `boss_%d_%d`

Then it calls the entity spawn path and stores the spawned handle back into the spawn object.

This is a strong pattern for DWRT: map/objective NPCs are controlled by spawn-point entities and server spawn routines, not only by baked map resources.

## Damage and team/FFA handling

### `TakeDamageOld` flow

`CBaseEntity::TakeDamageOld` at `0x180c6ba60` does **not** appear to be the earliest hit-validation point. It assumes a `CTakeDamageInfo` already exists and then:

1. checks whether target can receive damage / has valid state;
2. initializes damage force/position defaults;
3. calls global pre-damage listeners (`DAT_183a68920` stack-like listener path);
4. resolves attacker/inflictor handles from `CTakeDamageInfo`;
5. calls an entity virtual at `+0x3c8` with the damage info;
6. calls a game-rules virtual at `+0x2e0`;
7. calls another entity virtual at `+0x3e8` if needed;
8. builds/copies `CTakeDamageResult`;
9. applies damage to health/life state;
10. calls entity post-damage virtuals and global post listeners.

Implication: DWRT's `TakeDamageOld` hook is excellent for blocking/scaling/relabeling damage that reaches damage application, but same-team bullet/ability attempts may be rejected before this point.

### `mp_friendlyfire` is a real hit-filter gate

`mp_friendlyfire` is registered at `0x1801ba440` with description:

```txt
Allows team members to injure other members of their team
```

A usage function at `0x1818d8f30` reads `mp_friendlyfire`. If the convar is false and a same-team predicate virtual (`vfunc +0xcb0`) reports true, it returns false. A caller at `0x1807838d0` bypasses this check for entity type `0x1a` and otherwise calls the filter.

Interpretation:

- same-team weapon/hit validation is gated before `TakeDamageOld`;
- setting `mp_friendlyfire 1` is likely necessary for policy FFA;
- runtime probe still required to verify bullets, melee, and each ability type.

### `mp_teamplay` is weaker evidence

`mp_teamplay` is registered at `0x1801bcc30`. A game-frame path at `0x181953a70` reads it and copies the byte into a global server/game state (`PTR_DAT_1830e2668[0x74]`).

No direct evidence yet that `mp_teamplay 0` rewrites Deadlock/Citadel team semantics. Treat it as a legacy/general Source 2 flag until runtime-proven.

### Modifier state `FRIENDLY_FIRE_ENABLED`

The enum table contains:

```txt
MODIFIER_STATE_FRIENDLY_FIRE_ENABLED
```

It is ordinal `0x76` in this build, surrounded by:

```txt
0x75 MODIFIER_STATE_FADE_ALPHA_TO_ZERO
0x76 MODIFIER_STATE_FRIENDLY_FIRE_ENABLED
0x77 MODIFIER_STATE_FLYING
```

A combat/event path at `0x181cb9e70` checks:

```txt
FUN_180c58ba0(entity, 0x76)
```

where `FUN_180c58ba0` is an inferred modifier-state query. In that path, the friendly-fire state changes how team/recipient context is selected for a combat-related record.

This proves the state is not dead data, but it does **not** prove it bypasses every target/hit validator. It should be tested as a second FFA lever after `mp_friendlyfire`.

### Ability target masks

Ability/VData load paths reference:

- `m_nAbilityTargetTypes`
- `m_nAbilityTargetFlags`
- `m_nRequiredDamageFlags`
- `CITADEL_UNIT_TARGET_TYPE`
- `CITADEL_UNIT_TARGET_FLAGS`

The target-type enum table is bitmask-like and explicitly splits friendly/enemy categories. Extracted values for this build:

| Value | Name |
|---:|---|
| `0x00001` | `CITADEL_UNIT_TARGET_HERO_FRIENDLY` |
| `0x00002` | `CITADEL_UNIT_TARGET_TROOPER_FRIENDLY` |
| `0x00004` | `CITADEL_UNIT_TARGET_BOSS_FRIENDLY` |
| `0x00008` | `CITADEL_UNIT_TARGET_BUILDING_FRIENDLY` |
| `0x00010` | `CITADEL_UNIT_TARGET_PROP_FRIENDLY` |
| `0x00020` | `CITADEL_UNIT_TARGET_MINION_FRIENDLY` |
| `0x00100` | `CITADEL_UNIT_TARGET_HERO_ENEMY` |
| `0x00200` | `CITADEL_UNIT_TARGET_TROOPER_ENEMY` |
| `0x00400` | `CITADEL_UNIT_TARGET_BOSS_ENEMY` |
| `0x00800` | `CITADEL_UNIT_TARGET_BUILDING_ENEMY` |
| `0x010000` | `CITADEL_UNIT_TARGET_NEUTRAL` |
| `0x000101` | `CITADEL_UNIT_TARGET_HERO` = friendly + enemy hero |
| `0x000202` | `CITADEL_UNIT_TARGET_TROOPER` = friendly + enemy trooper |
| `0x00003f` | `CITADEL_UNIT_TARGET_ALL_FRIENDLY` |
| `0x013f00` | `CITADEL_UNIT_TARGET_ALL_ENEMY` |
| `0x013f3f` | `CITADEL_UNIT_TARGET_ALL` |

Target flags extracted:

| Value | Name |
|---:|---|
| `0x0` | `CITADEL_UNIT_TARGET_FLAG_NONE` |
| `0x2` | `CITADEL_UNIT_TARGET_FLAG_PENETRATE_INVULNERABLE` |
| `0x4` | `CITADEL_UNIT_TARGET_FLAG_NO_INVIS` |
| `0x8` | `CITADEL_UNIT_TARGET_FLAG_NO_DORMANT_NEUTRALS` |
| `0x10` | `CITADEL_UNIT_TARGET_FLAG_ALLOW_BREAKABLES` |
| `0x20` | `CITADEL_UNIT_TARGET_FLAG_ALLOW_SMALL_DEPLOYABLES` |

Implication: ability targeting is not only `mp_friendlyfire`. If an ability's VData has `HERO_ENEMY` (`0x100`) but not `HERO_FRIENDLY` (`0x1`) / `HERO` (`0x101`), same-team unit targeting can fail before damage is created. True FFA or same-team ability combat may need curated per-ability target-mask overlays or a friendly-fire modifier state, not just damage-hook policy.

### Team number facts

Many server paths compare a byte at entity offset `0x33c`, and Deadworks resolves `CBaseEntity::m_iTeamNum` through schema. Treat `entity + 0x33c` as the current-build implementation of `m_iTeamNum`, but public DWRT must use schema-backed `Entity.team()` only.

Team values observed in code and plugins:

- `2`: one Citadel team, usually Amber/Rebel in plugin convention;
- `3`: the other team, usually Sapphire/Combine in plugin convention.

Per-player unique team numbers remain high risk because game rules, arrays, minimap, objectives, reward paths, and GC metadata assume Citadel teams.

## FFA feasibility after RE

### Most likely first-working path: policy FFA

1. Keep engine teams at normal values (`2`/`3`) or a controlled two-team split.
2. Set `mp_friendlyfire 1`.
3. Optionally apply a curated modifier/state that sets `MODIFIER_STATE_FRIENDLY_FIRE_ENABLED` if runtime probes show it matters.
4. Let same-team hits reach `TakeDamageOld`.
5. DWRT enforces virtual teams/scoring in `DamagePolicy` and blocks/scales damage as needed.

This avoids rewriting Citadel team infrastructure.

### What must be runtime-probed

For each of bullet, melee, projectile ability, direct unit-target ability, AoE ability:

| Probe | Expected signal |
|---|---|
| enemy baseline | `TakeDamageOld` fires |
| same-team default | likely no `TakeDamageOld` for some paths |
| same-team + `mp_friendlyfire 1` | should fire for weapon/hit paths if filter evidence holds |
| same-team + friendly-fire modifier state | may affect ability/combat event paths |
| same-team + target-mask overlay | needed only if ability target validation rejects before damage |

Do not claim true FFA until these are measured.

### What not to do first

- Do not assign each player a unique engine team.
- Do not patch VData target masks globally without per-ability scope.
- Do not expose raw team offsets or modifier bitsets to scripts.
- Do not patch Pulse graphs for FFA before hit/target filters are understood.

## DWRT surface recommendations

Add a public conceptual surface later, even if it composes existing internals:

```txt
MapEntities / MapLogic
```

Back it with these internal hooks/facts:

| DWRT concept | Backing engine boundary |
|---|---|
| `MapLifecycle` | `CCitadelGameRules::PostSpawnGroupLoad`, map start/end events |
| `MapEntityView` | entity lifecycle + schema + classname/designer name |
| `MapEntityCommand::remove` | `UTIL_Remove` / entity system |
| `MapEntityCommand::spawn` | `CreateEntityByName` + `QueueSpawnEntity` + `ExecuteQueuedCreation` |
| `MapEntityCommand::accept_input` | `CEntityInstance::AcceptInput` |
| `EntityIoHook` | `AcceptInput`, `FireOutputInternal`, touch hooks |
| `NpcSpawnPolicy` | spawn-point entity lifecycle + convars + curated spawn APIs |
| `DamagePolicy` | `TakeDamageOld` + target/hit probes |
| `TeamPolicy` | virtual teams over stable engine teams |
| `PulseOverlay` | observe Pulse graph start/think and Pulse API calls; do not patch graphs initially |

Public script shape should be semantic:

```txt
on_spawn(class = "CNPC_Trooper") { remove(); }
on_spawn(designer = "npc_boss_tier2") { set_health_scale(0.5); }
on_output(class = "trigger_multiple", output = "OnStartTouch") { block_if(...); }
on_damage(victim.kind = Hero) { apply_virtual_ffa(); }
set_trooper_spawning(enabled = false)
```

Not:

```txt
write(entity + 0x33c, 7)
call(vtable[0xcb0])
patch(vpulse_bytecode)
```

## Next concrete work

1. Add a throttled in-game probe for:
   - entity spawn class/designer/team/lane for NPCs/triggers/objectives;
   - `TakeDamageOld` attacker/victim/team/class/damage flags;
   - same-team hit attempts under `mp_friendlyfire` and modifier-state variants;
   - filtered `AcceptInput` / `FireOutputInternal` for trigger/shop/teleport/bounce/trooper/objective classes.
2. Add DWRT internal fact names for the RE-confirmed boundaries:
   - `CBaseEntity::TakeDamageOld`
   - `CEntityInstance::AcceptInput`
   - `CEntityIOOutput::FireOutputInternal`
   - `CModifierProperty::AddModifier`
   - `CCitadelGameRules::PostSpawnGroupLoad`
   - `CPulseGraphInstance_ServerEntity::{BeforeGraphStart,GraphStart,Think}`
   - `CCitadel_InfoTrooperSpawnAPI::{GetIsSpawningEnabled,SetSpawningEnabled}`
3. Build a small `DamagePolicy`/`TeamPolicy` experiment over Deadworks first, then port the safe shape into DWRT.
4. Keep Pulse graph mutation deferred until entity I/O + spawn + damage policy cannot express a desired mode.
