# Hook Inventory

Initial inventory from Deadworks + current decompiled/usercmd evidence.

| Hook / boundary | Current source | Frequency | Rust layer target | Hot-path policy | Evidence needed next |
|---|---|---:|---|---|---|
| `CBasePlayerController::ProcessUsercmds` | Deadworks `Hooks/ProcessUsercmds.*`, `UsercmdVisitorRuntime.*`; `ProcessUserCmds (1).cpp` | Very high | `dwrt-usercmd` | Count-only by default; fast/full/trigger mounts only | confirm command queue/history offsets and `AppendUserCommand` side effects |
| `CServerSideClientBase::FilterMessage` | Deadworks incoming net hook | High | `dwrt-net` | msg id and user-message gates before protobuf bytes | confirm incoming serializer/id path and block return semantics |
| `IGameEventSystem::PostEventAbstract` | Deadworks outgoing net hook | High | `dwrt-net` | recipient-mask available; serialize only on mounted handlers | confirm direct `svc_UserMessage` envelope fields across builds |
| Game event dispatch | Deadworks game event hook | Medium | `dwrt-events` | named subscription table | event payload wrapper/lifetime tests |
| Entity created/spawned/deleted | Deadworks entity callbacks | Medium | `dwrt-entity` | mounted class/name filters eventually | entity handle validity rules |
| Entity touch/input/output | Deadworks touch/entity IO hooks | Potentially high | `dwrt-entity` | class/event interest filters; no default logging | vtable indices and event storm throttling |
| Client connect/put/full/disconnect | Deadworks client lifecycle | Low | `dwrt-client` | always safe to dispatch to runtime core; plugin interest optional | slot/xuid/name lifetime and full-connect polling semantics |
| Game frame | Deadworks Source2Server frame callback | Tick rate | `dwrt-core` scheduler | off for plugins unless mounted; runtime timers only | tick ordering relative to usercmd/net/entity callbacks |
| Commands/chat | Deadworks command/chat system | Low/medium | `dwrt-command` | command trie; chat protobuf mounted only when needed | chat ids and say/team chat handling |
| Schema/entity memory | Deadworks schema wrappers | Variable | `dwrt-memory`/`dwrt-entity` | curated fields only; versioned manifests | schema dump validation per game update |

## CLoopModeGame / CS2-vibesignatures note

The LobeHub/CS2 workflow points at a useful pattern: find a stable anchor string, walk xrefs to a central registration function, and recover event handler maps from repeated registration calls.

Use that as an IDA search tactic only. Deadlock does not necessarily expose or use the same CS2 client functions, and server-side functions may differ. Treat any CS2 name as a hypothesis until proven in Deadlock binaries.
