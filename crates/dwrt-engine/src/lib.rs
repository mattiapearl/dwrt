#![deny(unsafe_op_in_unsafe_fn)]

//! Declarative map of real Deadlock server surfaces.
//!
//! This crate does not hook or dereference anything. It records what a runtime
//! layer may expose, how the fact should be discovered, and whether mutation is
//! allowed by default.

mod access;
mod catalog;
mod surface;

pub use access::{Access, Authority, DiscoveryKind, HotPathPolicy};
pub use catalog::{SurfaceCatalog, default_surfaces};
pub use surface::{ServerSurface, SurfaceDescriptor};
