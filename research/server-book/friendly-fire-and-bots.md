# Friendly fire and practice bots

Date: 2026-05-31

## Goal

Learn the smallest reliable path for:

- damaging own/team objectives;
- receiving damage from another hero-like actor;
- eventually allowing same-team player-vs-player damage for DWRT policy FFA.

## Prior RE facts

- `mp_friendlyfire` exists in the current `server.dll` and is referenced from damage/team code.
- `MODIFIER_STATE_FRIENDLY_FIRE_ENABLED` exists with ordinal `0x76`, but it should be treated as a second lever after testing `mp_friendlyfire`.
- Ability target masks split friendly/enemy target categories, so some abilities may fail before `TakeDamageOld` even if `mp_friendlyfire` is enabled.
- Do not start by assigning unique engine teams per player.

## Native bot cfgs

The shipped game cfg directory contains practice bot configs:

```txt
C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\cfg\citadel_botmatch_practice_1v1.cfg
C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\cfg\citadel_botmatch_player_vs_bot.cfg
C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\cfg\citadel_botmatch_2v2_test.cfg
C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\cfg\citadel_botmatch_practice_2v2_guided.cfg
```

Useful config contents observed:

```txt
citadel_solo_bot_match 1
citadel_spawn_practice_bots 1
citadel_spawn_practice_bots_count 1
citadel_active_lane 4
```

The guided 2v2 cfg additionally sets:

```txt
citadel_guided_bot_match 1
citadel_spawn_practice_bots_count 4
citadel_bot_attack_enemies 1
citadel_bot_practice_opponent "hero_inferno,hero_orion,hero_haze"
citadel_bot_practice_teammate "hero_kelvin,hero_forge"
```

Relevant bot commands/ConVars found in strings:

```txt
bot_kick_all
bot_command <bot name> <command string...>
bot_mimic
bot_mimic_target
bot_puppet_target
citadel_bot_attack_enemies
citadel_bot_brain_disable_attacks
citadel_bot_shoot
citadel_bot_melee
citadel_bot_move_random
citadel_bot_list_ents
citadel_bot_list_objectives_ent
```

## DWRT objective team-spoof experiment

After `mp_friendlyfire 1` alone did not make same-side objectives visibly take damage, DWRT added a gated damage-hook experiment:

```txt
DWRT_FRIENDLY_FIRE_EXPERIMENT=1
DWRT_FRIENDLY_FIRE_OBJECTIVE_TEAM_SPOOF=1
```

Implementation:

```txt
native/dwrt-host/dwrt_friendly_fire.hpp
native/dwrt-host/dwrt_friendly_fire.cpp
```

Behavior:

- only runs inside `CBaseEntity::TakeDamageOld`;
- only non-recursive callbacks;
- only objective-like victim designer names (`npc_boss_tier1/2/3`, `alt_npc_boss_tier1/2/3`, and boss fallback class names);
- reads current-build `CBaseEntity::m_iTeamNum` at offset `0x33c`;
- only spoofs objectives whose team matches `DWRT_FRIENDLY_FIRE_LOCAL_TEAM` (default `2`);
- temporarily flips that own objective team `2 <-> 3` before calling the original `TakeDamageOld`;
- restores the original team immediately after the original returns.

A blind first version flipped every objective victim and therefore could break normal enemy-objective damage: if an enemy team-3 Guardian was flipped to team 2 before the original damage code, it could become friendly to a team-2 player and be rejected. The current experiment is deliberately local-team-scoped to avoid touching enemy objectives.

This is not a public API and is not a permanent team assignment. It is a minimal test for whether the same-team objective damage rejection is inside/after `TakeDamageOld`. If same-side objective damage still does not apply and `objectiveCandidates` remains zero, the rejection likely happens before the current damage hook.

This is close to "make the damage issuer enemy" semantically, but it does not yet rewrite `CTakeDamageInfo::m_hAttacker`. Direct issuer/player-team spoofing is the better general design for player-vs-player, but it needs a validated `CTakeDamageInfo::m_hAttacker` handle resolver or attacker pointer accessor. If same-team player targeting is rejected before `TakeDamageOld`, attacker spoofing inside this hook will still be too late and DWRT will need an earlier target/trace/usercmd boundary.

Counters:

```txt
enabled
mode
scope
localTeam
damageCallbacks
skippedRecursive
missingIdentity
missingDesignerName
nonObjectiveVictims
objectiveCandidates
invalidTeam
teamSpoofAttempts
teamSpoofApplied
teamSpoofRestored
```

## First runtime test plan

Launch a disposable manual probe session with:

```txt
+mp_friendlyfire 1
+exec citadel_botmatch_practice_1v1.cfg
+citadel_bot_attack_enemies 1
```

Then manually test:

1. Shoot an enemy objective: verify baseline objective damage still creates `TakeDamageOld` counters.
2. Shoot an own objective: verify whether it visibly takes damage and whether `TakeDamageOld` counters rise.
3. Let the practice bot attack the player: verify incoming damage/health loss.
4. If a teammate bot exists in a later guided config, test same-team hero hits with `mp_friendlyfire 1`.

## Interpretation gates

- If own objective damage works with only `mp_friendlyfire 1`, prefer this for objective-friendly-fire experiments.
- If own objective targeting creates no `TakeDamageOld` event, the rejection happens before the current damage hook and likely needs target-mask or filter research.
- If hero same-team bullet damage works but abilities do not, do not patch globally; add per-ability/targeting evidence first.
- If bots spawn but do not attack, try `citadel_bot_attack_enemies 1`, `citadel_bot_brain_disable_attacks 0`, and/or a guided botmatch cfg.

## Current public-surface guidance

Expose this later as a policy concept, not as raw ConVar/memory mutation:

```txt
TeamPolicy.enable_same_team_damage(scope = objectives | heroes | abilities)
DamagePolicy.on_damage(...)
BotTestHarness.spawn_practice_bot(...)
```

Raw team offsets, modifier ordinals, and target masks remain internal versioned facts.
