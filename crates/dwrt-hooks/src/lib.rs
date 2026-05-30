#![deny(unsafe_op_in_unsafe_fn)]

//! Hook boundary registry model.
//!
//! This crate does not install hooks. It describes hook boundaries, their
//! discovery facts, feature dependencies, frequency class, and shadow/active
//! runtime mode so an installer can make validated decisions later.

mod defaults;
mod descriptor;
mod discovery;
mod error;
mod feature;
mod registry;
mod status;

pub use defaults::{
    FEATURE_CLIENT_LIFECYCLE, FEATURE_ENTITY_LIFECYCLE, FEATURE_GAME_EVENTS, FEATURE_GAME_FRAME,
    FEATURE_NET, FEATURE_USERCMD, client_lifecycle_fact, default_feature_set,
    default_hook_descriptors, default_hook_registry, entity_lifecycle_fact, filter_message_fact,
    game_event_post_fact, game_frame_fact, post_event_abstract_fact, process_usercmds_fact,
};
pub use descriptor::{HookDescriptor, HookName, HookPurpose};
pub use discovery::DiscoveryRequirement;
pub use error::HookRegistryError;
pub use feature::{FeatureDependency, FeatureSet};
pub use registry::HookRegistry;
pub use status::{HookFrequency, HookHealth, HookRunMode, HookStatus};

#[cfg(test)]
mod tests;
