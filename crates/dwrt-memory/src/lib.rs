#![deny(unsafe_op_in_unsafe_fn)]

//! Versioned memory/surface facts for the Rust Deadworks rewrite.
//!
//! This crate intentionally does not scan process memory or dereference engine
//! pointers. It is the manifest/model layer that says which low-level facts are
//! known for a specific Deadlock build and which server surface they belong to.

mod build;
mod error;
mod fact;
mod manifest;

pub use build::{BuildId, ModuleId};
pub use error::ManifestError;
pub use fact::{Confidence, Evidence, FactKey, FactKind, FactValue, MemoryFact};
pub use manifest::MemoryManifest;
