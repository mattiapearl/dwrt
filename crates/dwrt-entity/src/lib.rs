#![deny(unsafe_op_in_unsafe_fn)]

//! Typed entity/controller/pawn facades.
//!
//! This crate deliberately does not dereference engine pointers. It provides
//! typed, non-`Send` engine handles and manifest-backed schema field plans.

mod engine_ref;
mod error;
mod ids;
mod markers;
mod refs;
mod schema;

pub use engine_ref::EngineRef;
pub use error::EntityError;
pub use ids::{EntityHandle, EntityIndex, MAX_PLAYER_SLOTS, PlayerSlot};
pub use markers::{
    CBaseEntity, CBasePlayerController, CBasePlayerPawn, CCitadelPlayerController,
    CCitadelPlayerPawn,
};
pub use refs::{PlayerControllerRef, PlayerPawnRef};
pub use schema::{EntitySchemaCatalog, PlayerControllerSchema, PlayerPawnSchema, SchemaFieldPlan};

#[cfg(test)]
mod tests;
