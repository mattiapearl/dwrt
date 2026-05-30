# Trace and Golden Comparison Layer

DWRT should validate runtime behavior with JSONL traces before replacing or mutating live server behavior.

## Current crate

`crates/dwrt-trace` provides:

- versioned JSONL trace records,
- runtime load events,
- hook status events,
- net route decision events,
- usercmd route decision events,
- subscription events,
- a bounded ring buffer with drop counters,
- ordered route-decision comparison helpers.

It does not hook the engine or parse protobufs. Hot hooks should enqueue compact records/counters and flush JSONL from a non-hot path.

## Initial record shape

```jsonl
{"seq":0,"type":"runtime","phase":"loaded","abi_version":1}
{"seq":1,"type":"net_route","direction":"outgoing","endpoint_slot":1,"msg_id":72,"recipient_mask":0,"user_message_type":null,"route":"no_interest"}
{"seq":2,"type":"usercmd_route","slot":1,"command_count":3,"mount_mask":0,"field_mask":15,"route":"count_only"}
```

## Comparison rule

Route comparisons ignore non-route records and compare ordered route decisions:

- route key: net direction/slot/msg id/recipient/user-message id or usercmd slot/count,
- route outcome: no-interest, fast-only, serialized-only, fast+serialized, count-only, fast-read, full-protobuf, etc.

This is intentionally narrow for the first shadow-mode smoke test. Later replay can add timestamps, tick ids, map/session ids, plugin ids, and richer diffing.

## In-game shadow target

The first shim should emit coarse trace records for:

- runtime loaded/unloaded,
- hook resolved/installed/disabled,
- usercmd route decisions,
- net route decisions.

No gameplay behavior should change during this phase.
