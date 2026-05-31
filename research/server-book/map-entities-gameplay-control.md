# Map Entities and Gameplay Control Research

Date: 2026-05-30

This note maps where Deadlock map/NPC/objective behavior lives relative to DWRT's current surfaces, and what level of control is realistic without replacing the map or Source 2 systems.

See also the deeper static RE pass: [Map/NPC/Pulse/FFA RE pass](map-npc-ffa-re-20260530.md).

## Summary

Map entities are not a separate public DWRT surface yet. In the current model they are split across:

- `MapLoading`: the baked `.vmap_c`/spawn-group/resource load path.
- `EntitySimulation`: instantiated `CEntityInstance` / `CBaseEntity` objects after the map creates them.
- `EntityIo`: Source-style entity input/output, trigger, touch, and output wiring.
- `GameRules`: Citadel mode state, match clock, objective/team rules, and Pulse/game-mode controllers.
- `EntitySchema`: schema fields on spawned entities, NPCs, game rules, controllers, and modifiers.

For public API clarity, DWRT should probably add an explicit `MapEntities` or `MapLogic` surface later. Internally it would still use the same primitives: entity lifecycle, schema, entity I/O hooks, damage hooks, convars, and curated engine calls.

## Evidence from current artifacts

### Runtime entity access already exists in Deadworks

Deadworks has native callbacks for runtime entity operations:

- `GetEntityFromHandle`
- `GetEntityByIndex`
- `FindEntityByName`
- `GetEntityHandle`
- `GetEntityDesignerName`
- `GetEntityClassname`
- `CreateEntityByName`
- `QueueSpawnEntity`
- `ExecuteQueuedCreation`
- `RemoveEntity` / `UTIL_Remove`
- `AcceptInput`
- schema field lookup and network-state notification

Deadworks also registers an `IEntityListener` and observes:

- `OnEntityCreated`
- `OnEntitySpawned`
- `OnEntityDeleted`

The active entity list is reachable through the entity system identity chain (`m_pFirstActiveEntity -> m_pNext`).

### Entity I/O and touch are hookable

Current Deadworks hook points:

- `CEntityInstance::AcceptInput`
- `CEntityIOOutput::FireOutputInternal`
- `CBaseEntity::StartTouch`
- `CBaseEntity::EndTouch`

These are the correct low-level interception points for much of map logic: triggers, buttons, outputs, scripted chains, teleporters, shops, damage volumes, etc.

### Damage and modifiers are hookable

Current Deadworks hook points:

- `CBaseEntity::TakeDamageOld`
- `CModifierProperty::AddModifier`
- `CCitadelPlayerPawn::AbilityThink`
- `CCitadelPlayerPawn::ModifyCurrency`

Damage info is schema-backed enough to read/write:

- `CTakeDamageInfo::m_hInflictor`
- `CTakeDamageInfo::m_hAttacker`
- `CTakeDamageInfo::m_hAbility`
- `CTakeDamageInfo::m_flDamage`
- `CTakeDamageInfo::m_flTotalledDamage`
- `CTakeDamageInfo::m_bitsDamageType`
- `CTakeDamageInfo::m_nDamageFlags`

### Existing plugins already change map/NPC behavior without map reinstall

The Deadworks `TagPlugin` and `DeathmatchPlugin` already do a limited form of runtime map re-authoring:

- disable trooper/NPC spawns via convars:
  - `citadel_trooper_spawn_enabled`
  - `citadel_npc_spawn_enabled`
  - `citadel_active_lane`
- remove objective/NPC entities on spawn by designer name:
  - `npc_boss_tier3`
  - `npc_boss_tier2`
  - `npc_boss_tier1`
  - `npc_barrack_boss`
  - `npc_base_defense_sentry`
  - `npc_trooper_boss`
  - `npc_trooper`
- block damage through `OnTakeDamage`
- assign teams/heroes through controller calls
- manage spawn positions and mode rules in plugin code

This is strong evidence that many mode changes do **not** require recompiling/reinstalling the map. The map remains loaded, but the runtime can suppress or override selected systems.

### Map packages contain baked entities/resources

`dl_midtown.vpk` contains:

- `maps/dl_midtown.vmap_c`
- many `maps/dl_midtown/entities/*trigger*.vmdl_c` resources, including shop, teleport, bounce pad, trooper detector, and interior trigger resources.

Examples from VPK listing:

```txt
maps/dl_midtown.vmap_c
maps/dl_midtown/entities/amber_trooper_detector_14781_2206.vmdl_c
maps/dl_midtown/entities/sapphire_trooper_detector_14781_2208.vmdl_c
maps/dl_midtown/entities/*bounce_pad_trigger*.vmdl_c
maps/dl_midtown/entities/*teleport_trigger*.vmdl_c
maps/dl_midtown/entities/*shop_item_trigger*.vmdl_c
```

These are baked resources, but once spawned they are normal runtime entities or components reachable through entity lifecycle/I/O/touch hooks.

### Server strings show Citadel-specific NPC classes and Pulse APIs

`server.dll` strings identify relevant NPC classes and systems:

- `CNPC_Trooper`
- `CNPC_TrooperBoss`
- `CNPC_TrooperNeutral`
- `CNPC_Boss_Tier2`
- `CNPC_Boss_Tier3`
- `CNPC_BarrackBoss`
- `CNPC_BaseDefenseSentry`
- `CNPC_FieldSentry`
- `CNPC_ShieldedSentry`
- `CNPC_MortarSentry`
- `CNPC_MidBoss`

And Pulse/game-mode related systems:

- `CCitadelPointPulseSystem`
- `CCitadelPointPulseAPI`
- `CPointPulse`
- `CPulseGraphInstance_ServerEntity`
- `CBasePulseGraphInstance`
- `CCitadel_InfoTrooperSpawnAPI::GetIsSpawningEnabled`
- `CCitadel_InfoTrooperSpawnAPI::SetSpawningEnabled`

This suggests Pulse is real and map/game-mode logic can call into Citadel-specific APIs. For DWRT, the lower-risk route is to intercept the resulting entity/API behavior, not patch arbitrary Pulse graphs first.

## Control layers

### Layer 1: convars / server commands

Lowest risk. Good for broad enable/disable knobs:

- trooper spawning
- NPC spawning
- active lane
- duplicate heroes
- starting gold
- respawn timing
- voice/all-talk
- `mp_teamplay`, `mp_friendlyfire`, and `sv_friendly_dmg_immune` exist as strings and should be tested, but Deadlock may not honor them for Citadel-specific damage/targeting.

Use as coarse policy only; verify in-game before relying on any convar.

### Layer 2: entity lifecycle policy

On map start and entity spawn:

- index every entity by handle, index, class name, designer name, targetname, team, position;
- remove unwanted map objectives/NPCs;
- retarget or mutate schema-backed fields;
- apply modifiers;
- spawn replacement entities.

This is enough for many custom modes.

### Layer 3: entity I/O and touch policy

Interpose on:

- `AcceptInput` pre/post;
- `FireOutputInternal` pre/post;
- `StartTouch` / `EndTouch`.

This can suppress or redirect map logic without editing the map resource. Examples:

- block a trigger's output;
- ignore a shop trigger;
- override a teleporter/bounce pad;
- translate a map output into a DWRT script event;
- call `AcceptInput` manually from a DWRT script.

### Layer 4: damage/modifier policy

Interpose on:

- `TakeDamageOld`;
- `AddModifier`;
- ability attempts/think where needed.

This is the first layer for “who can damage who”. It can block, scale, retarget, or rewrite damage info after a hit is already accepted by engine targeting/collision.

Limitation: if the engine rejects a same-team target before damage info is created, `TakeDamageOld` will not see the attempted hit. FFA needs a probe to determine where same-team attacks are rejected.

### Layer 5: ability targeting / NPC targeting / game rules

Needed for true mode rewrites. Candidate research areas:

- Citadel unit target masks (`CITADEL_UNIT_TARGET_*_FRIENDLY/ENEMY` strings exist);
- modifier state `MODIFIER_STATE_FRIENDLY_FIRE_ENABLED` exists;
- `mp_friendlyfire` and `mp_teamplay` strings exist;
- NPC target fields such as `CNPC_Trooper::m_hTargetedEnemy` appear in symbols;
- game rules kill/reward path appears in symbols (`CCitadelGameRules::GameEntityKilled` string context from templates);
- bot/NPC team-sensing systems and trooper lane storage systems exist.

This layer is high leverage but must be manifest-backed and profiled.

### Layer 6: Pulse runtime hooks

Highest risk. Server strings show Pulse graph instances and APIs, but patching Pulse bytecode/graphs is not the first choice.

Safer alternatives:

- expose Pulse-owned entities as normal entities after spawn;
- intercept inputs/outputs called by Pulse;
- intercept Citadel Pulse API functions such as `SetSpawningEnabled` after signatures are validated;
- run a DWRT “overlay map script” in parallel rather than replacing the baked Pulse graph.

## FFA feasibility

There are two distinct goals:

1. **Policy FFA**: custom scoring and damage rules while the engine still believes players are on Amber/Sapphire teams.
2. **True engine FFA**: every player is a separate team/faction for targeting, UI, bots, rewards, objectives, and damage.

Policy FFA is likely feasible first. True engine FFA is risky because Deadlock systems, UI, objectives, rewards, spawns, minimap, and GC metadata likely assume two primary teams.

### FFA implementation options

#### Option A: same engine teams + friendly fire enabled

Keep players on teams 2/3 or maybe one team, enable friendly fire if possible, and let DWRT own scoring.

Research steps:

- test `mp_friendlyfire 1`;
- test `mp_teamplay 0` if writable;
- test adding a modifier/state that sets `MODIFIER_STATE_FRIENDLY_FIRE_ENABLED`;
- verify whether same-team bullet/melee/ability hits reach `TakeDamageOld`;
- verify client hitmarkers/damage numbers and killfeed.

This is the cleanest if it works.

#### Option B: intercept after damage is accepted

Leave normal two-team targeting intact. Use `TakeDamageOld` to block/scale/retarget when damage occurs.

Good for custom team modes, duels, tag, anti-objective damage, NPC immunity, etc.

Not enough for true FFA if same-team attacks never create damage.

#### Option C: ability/projectile target-mask mutation

Patch ability VData or runtime target filters so same-team entities are considered valid targets.

Evidence strings:

```txt
CITADEL_UNIT_TARGET_HERO_FRIENDLY
CITADEL_UNIT_TARGET_HERO_ENEMY
CITADEL_UNIT_TARGET_ALL_FRIENDLY
CITADEL_UNIT_TARGET_ALL_ENEMY
MODIFIER_STATE_FRIENDLY_FIRE_ENABLED
```

This could unlock same-team targeting, but it is build-sensitive and may have broad side effects. It belongs behind curated function/schema facts, not public raw memory.

#### Option D: per-player engine teams

Assign each player to a unique `m_iTeamNum` value.

This is not recommended initially. Team numbers are likely used as indices into team arrays, objective tables, UI assumptions, match metadata, and GC signout. It may work for a few local tests and then corrupt unrelated systems.

#### Option E: full game-rules replacement

Not a DWRT MVP target. We should not replace Source 2/Deadlock game rules wholesale.

## NPC/trooper/guardian/walker control feasibility

### Easy

- Disable broad spawns with convars.
- Remove specific NPCs on spawn by designer/class.
- Change health/team/schema fields for spawned NPCs.
- Block damage to/from specific NPC classes.
- Apply/remove modifiers.
- Teleport or remove entities.
- Observe touch/input/output events for triggers and objectives.

### Medium

- Spawn selected NPCs by designer/subclass name using subclass registry + `CreateEntityByName` + `QueueSpawnEntity`.
- Mutate lane/team fields such as `m_iLane` if schema-validated.
- Retarget selected NPCs if `m_hTargetedEnemy` schema is validated.
- Block/modify `AddModifier` for NPC buffs/debuffs.
- Hook specific NPC think/graph-controller functions in shadow first.

### Hard

- Replace AI schedules/graph controllers.
- Rewrite trooper lane storage/pathing.
- Make NPCs obey entirely new faction rules.
- Patch Pulse graphs or replace map-owned Pulse script execution.
- True FFA with all UI/objective/reward systems coherent.

## Recommended DWRT architecture

Add explicit concepts, but keep raw memory internal:

```txt
MapEntityView       read-only handle/class/designer/name/team/position
MapEntityCommand    remove/spawn/accept_input/set_schema/apply_modifier
EntityLifecycle     created/spawned/deleted subscriptions with class/designer filters
EntityIoHook        input/output/touch hooks with class/name filters
DamagePolicy        TakeDamage hook, attacker/victim views, mutable damage info
TeamPolicy          runtime-owned virtual teams/scoring; does not blindly rewrite engine teams
NpcPolicy           class/designer filtered NPC behavior overlays
MapScript           declarative per-map overlay, not a replacement .vmap/.vpulse
```

A script should say:

```txt
on_spawn(designer = "npc_trooper") { remove(); }
on_damage(victim.class = "CCitadelPlayerPawn") { apply_ffa_policy(); }
on_input(class = "trigger_multiple", input = "Enable") { block(); }
```

It should not say:

```txt
write(entity + 0xABC, 3)
call(vtable[149])
patch_pulse_graph(ptr)
```

## Immediate research plan

1. Add a DWRT/Deadworks diagnostic probe that logs, throttled by interest filters:
   - entity spawn/designer/class/team/position for NPCs/triggers/objectives;
   - `TakeDamageOld` attacker/victim/team/class/designer/damage/flags;
   - same-team attack attempts under different convar/modifier settings;
   - entity input/output for selected classes only.
2. Test FFA gates in this order:
   - baseline enemy damage;
   - same-team damage with default settings;
   - same-team with `mp_friendlyfire 1`;
   - same-team with candidate friendly-fire modifier/state;
   - if still blocked, inspect ability target masks and target validation path in IDA.
3. Build the first DWRT public abstraction as an overlay `MapScript`/`DamagePolicy`, not raw memory scripting.
4. Treat Pulse patching as deferred unless entity I/O + lifecycle + damage policies cannot express the mode.
