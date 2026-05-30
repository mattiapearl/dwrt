# Hook Registry Model

`dwrt-hooks` models hook boundaries before any hook is installed.

It answers:

- what hook boundary exists,
- whether it is read-only visitor, mutating hook, or both,
- what server surface it belongs to,
- how frequently it runs,
- which memory/signature/schema facts are required,
- which runtime features/capabilities must be enabled,
- whether it is disabled, shadow, resolved, or installed.

## Key concepts

```txt
HookName              stable registry key, e.g. usercmd.process.visitor
HookPurpose           Visitor | Hook | VisitorAndHook
HookFrequency         Cold | MapLifecycle | PerClient | PerTick | PerPacket | PerUsercmd | EventStorm
Module                expected engine/native module boundary, e.g. server.dll
DiscoveryRequirement  required/optional dwrt-memory FactKey
FeatureDependency     required/optional capability string
HookRunMode           Disabled | Shadow | Active
HookHealth            Declared | Resolved | Installed | Disabled(reason)
```

## Visitor vs hook rule

Visitors are read-only. A descriptor with `HookPurpose::Visitor` cannot request:

```txt
BLOCK
MUTATE
CALL
REPLACE
```

Low-level boundaries can be `VisitorAndHook` if they support both read-only visitors and scoped intervention contexts, but the public subscriptions must stay separated.

## Shadow mode

Default run mode is `Shadow`.

Shadow mode means the Rust runtime may resolve and route decisions, compare with existing behavior, and emit diagnostics, but must not change engine behavior. This is the mode to use for first integration into the current Deadworks hook chassis.

## Install rule

A hook may be installed only when:

- run mode is not disabled,
- all required discovery facts exist in the memory manifest,
- all required features are enabled,
- descriptor shape is valid.

Hook installation remains a future C++/Rust shim concern; `dwrt-hooks` only models and validates.

## Default descriptor catalog

The initial default catalog is deliberately small and shadow-first:

| Hook name | Module | Surface | Frequency | Purpose |
| --- | --- | --- | --- | --- |
| `usercmd.process` | `server.dll` | usercmd pipeline | per usercmd | visitor + hook |
| `net.incoming.filter_message` | `engine2.dll` | net-message pipeline | per packet | visitor + hook |
| `net.outgoing.post_event` | `networksystem.dll` | net-message pipeline | per packet | visitor + hook |
| `game.event.post_event` | `server.dll` | game events | event storm | visitor |
| `game.frame` | `server.dll` | game rules | per tick | visitor |
| `client.lifecycle` | `engine2.dll` | Steam/auth/client lifecycle | per client | visitor |
| `entity.lifecycle` | `server.dll` | entity simulation | event storm | visitor |

Each descriptor requires at least one manifest fact and one feature group. Missing facts or disabled feature groups resolve to a disabled status instead of a partial install.
