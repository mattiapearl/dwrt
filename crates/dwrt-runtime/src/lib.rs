#![deny(unsafe_op_in_unsafe_fn)]

//! Opaque Rust runtime object and minimal C ABI entrypoints.
//!
//! The C++ hook shim should treat `DwrtRuntime` as opaque and call exported
//! functions from control paths/hook paths.

mod codes;
mod exports;
mod probe;
mod runtime;

pub use codes::*;
pub use runtime::DwrtRuntime;

#[cfg(test)]
mod tests;
