# Memory Manifest Layer

`dwrt-memory` is the boundary between reverse-engineered facts and public runtime APIs.

It does **not** scan memory or dereference pointers. It models facts that a loader/scanner can populate for a specific Deadlock build.

## Fact types

```txt
Interface      engine interface name/module
Signature      module + byte pattern
SchemaField    class + field + offset from schema system
Offset         versioned hard offset, internal by default
VTableIndex    class/method vtable index
```

Every fact is tied to:

```txt
BuildId
ServerSurface
FactKind
owner/name key
Access policy
Confidence level
Evidence list
```

## Public API safety rule

A fact is public-safe only when:

1. kind matches the value shape,
2. it does not request replace access,
3. confidence is one of:
   - engine interface,
   - schema validated,
   - multi-build validated.

Single-build hard offsets are allowed for internal routing/research but should not become plugin API.

## Why this matters

This lets us expose/mutate real server systems through curated wrappers:

```txt
PlayerController
PlayerPawn
GameRules
TraceSystem
NetMessageContext
UsercmdBatch
```

without exposing:

```txt
read(controller + 0x5A8)
write(pawn + arbitrary_offset)
call(vtable[index])
```

Those raw facts stay inside manifests and can be invalidated per game update.
