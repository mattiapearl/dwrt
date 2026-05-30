# Entity Facades

`dwrt-entity` is the first typed facade layer over real server objects.

It does **not** dereference engine pointers or call engine functions. It provides:

- `EngineRef<T>`: typed non-owning engine address, `!Send`/`!Sync` by design.
- `PlayerSlot`, `EntityIndex`, `EntityHandle` value types.
- `PlayerControllerRef` and `PlayerPawnRef` typed server-thread handles.
- `EntitySchemaCatalog`: required/optional schema field plans loaded from `dwrt-memory` manifests.

## Read/write separation

Schema field plans expose access separately:

```rust
field.can_read();
field.can_write();
```

Visitors should only receive read-capable views. Hooks/commands need explicit mutation capability and a field with `MUTATE` access.

## Initial required facts

For a controller/pawn facade to load, the manifest currently requires:

```txt
CBasePlayerController::m_hPawn   read
CBaseEntity::m_iHealth           read + mutate
```

Optional fields:

```txt
CBasePlayerController::m_iszPlayerName
CBaseEntity::m_iMaxHealth
CBaseEntity::m_iTeamNum
```

The exact class/field names may need adjustment after schema validation against current Deadlock builds. The important part is that the facade is backed by schema facts, not raw public offsets.

## Safety rules

- `EngineRef<T>` can only be created from an unsafe boundary and does not dereference.
- Engine refs are not sendable to worker threads.
- Single-build offsets are not public-safe.
- Schema fields must be `SchemaValidated` or better to back public facades.
