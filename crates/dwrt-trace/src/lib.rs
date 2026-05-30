#![deny(unsafe_op_in_unsafe_fn)]

//! JSONL trace records for DWRT shadow-mode validation.
//!
//! This crate is intentionally model/writer-only: it does not hook the engine,
//! parse protobufs, or allocate in net/usercmd routing code. Runtime code can
//! enqueue compact trace records into a bounded buffer and flush them from a
//! non-hot path.

mod compare;
mod event;
mod json;
mod ring;
mod writer;

pub use compare::{
    RouteComparison, RouteMismatch, compare_decisions, compare_route_decisions, route_decisions,
};
pub use event::{
    HookStatusTraceEvent, HookTraceMode, HookTraceStatus, NetRouteKind, NetRouteTraceEvent,
    NetTraceDirection, RouteDecision, RouteKey, RouteOutcome, RuntimePhase, RuntimeTraceEvent,
    SubscriptionAction, SubscriptionTraceEvent, TRACE_SCHEMA_VERSION, TraceEvent, TraceRecord,
    UsercmdRouteKind, UsercmdRouteTraceEvent,
};
pub use ring::TraceRingBuffer;
pub use writer::JsonlTraceWriter;

pub(crate) use event::push_json_header_fields;

#[cfg(test)]
mod tests;
