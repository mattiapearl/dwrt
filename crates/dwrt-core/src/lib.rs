#![deny(unsafe_op_in_unsafe_fn)]

//! Minimal Rust runtime core for a Deadworks-style Deadlock server extension.
//!
//! The core rule is simple: hot hooks must be able to decide "no work" with a
//! small number of atomic loads and no allocation.

pub mod interest;
pub mod net;
pub mod usercmd;

pub use dwrt_ffi as ffi;
