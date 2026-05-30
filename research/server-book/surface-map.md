# Real Server Surface Map

The Rust runtime should not replace these systems. It should map them, expose safe handles/views, and allow mutation only where the access path is understood and versioned.

| Surface | Engine remains authoritative? | How to map/expose | Easy/safe access | Mutation stance |
|---|---:|---|---|---|
| Dedicated server executable | Yes | module/build hash, signatures, trace facts | build info, module ranges | never replace |
| Source 2 networking | Yes | engine interfaces + `FilterMessage`/`PostEventAbstract` hooks | message ids, sender/recipient, selected fields | block/mutate only through mounted net contexts |
| Steam auth | Yes | client lifecycle/auth observations | xuid/slot/auth status facts | observe only; do not spoof/replace |
| Snapshot generation | Yes | trace/signature later | diagnostics, visibility research | observe first; no replacement |
| Entity simulation | Yes | lifecycle hooks + schema | entity handles, class names, curated fields | curated schema setters only |
| Physics | Yes | trace interface, vtable/signature facts | ray traces, collision queries | direct physics mutation requires separate research |
| Prediction | Yes | usercmd/prediction hooks + offsets | status traces, command history facts | observe first |
| Game rules | Yes | schema/signature | match state/time/team fields | curated setters after schema validation |
| Map loading | Yes | lifecycle hooks + server commands/interfaces | map start/end, controlled changelevel | call engine, do not implement loader |
| Resources/string tables | Yes | filesystem/resource interfaces + hooks | precache, addon/search path, read-only table diagnostics | controlled engine calls only |
| Protobuf serializers | Yes | network serializer interfaces + generated protos | parse/serialize exact mounted ids | mutation only in full protobuf contexts |
| Controller/pawn internals | Yes | schema + versioned offsets + vtable facts | `PlayerController`, `PlayerPawn`, health/team/hero/state | curated setters; raw offsets internal |
| Usercmd pipeline | Yes | `ProcessUsercmds` hook + protobuf/offset facts | compact fields, button triggers, command status | explicit full protobuf/mutation mount only |
| Net-message pipeline | Yes | incoming/outgoing hooks + protobuf ids | fast fields, nested user-message type gates | explicit full protobuf/mutation mount only |
| Game events | Yes | event hook/listeners | named event subscriptions | block/modify only where engine supports it |
| Entity IO/touch | Yes | vtable hooks + class filters | touch/input/output event views | block low-frequency inputs; touch is observe/filter only initially |
| Console/chat commands | Mixed | runtime registry backed by engine commands/protobuf | command handlers, permission checks | runtime-owned policy |

## Exposure design

Each public API should be one of these forms:

```txt
View       read-only snapshot or wrapper; cannot mutate engine
Handle     engine object identity with validity/lifetime checks
Context    scoped hook object; may block/mutate only in that hook
Command    controlled call into engine, audited and permissioned
Manifest   versioned memory/schema/signature fact
```

Avoid public APIs like:

```txt
read(ptr + offset)
write(ptr + offset, bytes)
call(vtable[index])
```

Those belong inside `dwrt-memory` behind versioned manifests and tests.

## Low-level discovery order

1. Existing engine interface or Source 2 SDK interface.
2. Schema system field lookup.
3. Generated protobuf descriptor / serializer id.
4. Signature-resolved function with stable tests.
5. Vtable index if validated per build.
6. Hard offset only if versioned by build/hash and not exposed raw.

## CS2 / CLoopModeGame note

CS2 public function lists and vibe-signature workflows are useful as search strategies, not facts. Deadlock may not expose the same functions and does not necessarily use CS2 subtick paths. Use anchor strings/xrefs/event registration patterns in IDA, then validate in Deadlock runtime traces.
