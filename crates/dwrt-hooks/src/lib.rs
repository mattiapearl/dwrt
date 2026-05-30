#![deny(unsafe_op_in_unsafe_fn)]

//! Hook boundary registry model.
//!
//! This crate does not install hooks. It describes hook boundaries, their
//! discovery facts, feature dependencies, frequency class, and shadow/active
//! runtime mode so an installer can make validated decisions later.

mod descriptor;
mod discovery;
mod error;
mod feature;
mod registry;
mod status;

pub use descriptor::{HookDescriptor, HookName, HookPurpose};
pub use discovery::DiscoveryRequirement;
pub use error::HookRegistryError;
pub use feature::{FeatureDependency, FeatureSet};
pub use registry::HookRegistry;
pub use status::{HookFrequency, HookHealth, HookRunMode, HookStatus};

#[cfg(test)]
mod tests;
