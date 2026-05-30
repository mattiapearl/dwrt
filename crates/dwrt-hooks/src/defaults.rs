use dwrt_engine::{Access, ServerSurface};
use dwrt_memory::{FactKey, FactKind};

use crate::{
    DiscoveryRequirement, FeatureDependency, FeatureSet, HookDescriptor, HookFrequency, HookName,
    HookPurpose, HookRegistry, HookRegistryError,
};

pub const FEATURE_USERCMD: &str = "usercmd";
pub const FEATURE_NET: &str = "net";
pub const FEATURE_GAME_EVENTS: &str = "game_events";
pub const FEATURE_GAME_FRAME: &str = "game_frame";
pub const FEATURE_CLIENT_LIFECYCLE: &str = "client_lifecycle";
pub const FEATURE_ENTITY_LIFECYCLE: &str = "entity_lifecycle";

#[must_use]
pub fn default_feature_set() -> FeatureSet {
    let mut features = FeatureSet::new();
    for feature in DEFAULT_FEATURES {
        features.enable(*feature);
    }
    features
}

pub fn default_hook_registry() -> Result<HookRegistry, HookRegistryError> {
    let mut registry = HookRegistry::new();
    for descriptor in default_hook_descriptors()? {
        registry.register(descriptor)?;
    }
    Ok(registry)
}

pub fn default_hook_descriptors() -> Result<Vec<HookDescriptor>, HookRegistryError> {
    Ok(vec![
        usercmd_process()?,
        net_incoming_filter_message()?,
        net_outgoing_post_event()?,
        game_event_post_event()?,
        game_frame()?,
        client_lifecycle()?,
        entity_lifecycle()?,
    ])
}

#[must_use]
pub fn process_usercmds_fact() -> FactKey {
    FactKey::new(
        ServerSurface::UsercmdPipeline,
        FactKind::Signature,
        "server.dll",
        "ProcessUsercmds",
    )
}

#[must_use]
pub fn filter_message_fact() -> FactKey {
    FactKey::new(
        ServerSurface::NetMessagePipeline,
        FactKind::Signature,
        "engine2.dll",
        "CServerSideClientBase::FilterMessage",
    )
}

#[must_use]
pub fn post_event_abstract_fact() -> FactKey {
    FactKey::new(
        ServerSurface::NetMessagePipeline,
        FactKind::Signature,
        "networksystem.dll",
        "IGameEventSystem::PostEventAbstract",
    )
}

#[must_use]
pub fn game_event_post_fact() -> FactKey {
    FactKey::new(
        ServerSurface::GameEvents,
        FactKind::Signature,
        "server.dll",
        "game_event_post",
    )
}

#[must_use]
pub fn game_frame_fact() -> FactKey {
    FactKey::new(
        ServerSurface::GameRules,
        FactKind::Interface,
        "server.dll",
        "game_frame",
    )
}

#[must_use]
pub fn client_lifecycle_fact() -> FactKey {
    FactKey::new(
        ServerSurface::SteamAuth,
        FactKind::Interface,
        "engine2.dll",
        "client_lifecycle",
    )
}

#[must_use]
pub fn entity_lifecycle_fact() -> FactKey {
    FactKey::new(
        ServerSurface::EntitySimulation,
        FactKind::Signature,
        "server.dll",
        "entity_lifecycle",
    )
}

const DEFAULT_FEATURES: &[&str] = &[
    FEATURE_USERCMD,
    FEATURE_NET,
    FEATURE_GAME_EVENTS,
    FEATURE_GAME_FRAME,
    FEATURE_CLIENT_LIFECYCLE,
    FEATURE_ENTITY_LIFECYCLE,
];

fn usercmd_process() -> Result<HookDescriptor, HookRegistryError> {
    HookDescriptor::new(
        HookName::new("usercmd.process")?,
        ServerSurface::UsercmdPipeline,
        HookPurpose::VisitorAndHook,
        HookFrequency::PerUsercmd,
        observe_read().union(Access::MUTATE),
    )
    .map(|descriptor| {
        descriptor
            .with_module("server.dll")
            .require_fact(DiscoveryRequirement::required(process_usercmds_fact()))
            .require_feature(FeatureDependency::required(FEATURE_USERCMD))
    })
}

fn net_incoming_filter_message() -> Result<HookDescriptor, HookRegistryError> {
    HookDescriptor::new(
        HookName::new("net.incoming.filter_message")?,
        ServerSurface::NetMessagePipeline,
        HookPurpose::VisitorAndHook,
        HookFrequency::PerPacket,
        observe_read().union(Access::BLOCK).union(Access::MUTATE),
    )
    .map(|descriptor| {
        descriptor
            .with_module("engine2.dll")
            .require_fact(DiscoveryRequirement::required(filter_message_fact()))
            .require_feature(FeatureDependency::required(FEATURE_NET))
    })
}

fn net_outgoing_post_event() -> Result<HookDescriptor, HookRegistryError> {
    HookDescriptor::new(
        HookName::new("net.outgoing.post_event")?,
        ServerSurface::NetMessagePipeline,
        HookPurpose::VisitorAndHook,
        HookFrequency::PerPacket,
        observe_read().union(Access::BLOCK).union(Access::MUTATE),
    )
    .map(|descriptor| {
        descriptor
            .with_module("networksystem.dll")
            .require_fact(DiscoveryRequirement::required(post_event_abstract_fact()))
            .require_feature(FeatureDependency::required(FEATURE_NET))
    })
}

fn game_event_post_event() -> Result<HookDescriptor, HookRegistryError> {
    HookDescriptor::new(
        HookName::new("game.event.post_event")?,
        ServerSurface::GameEvents,
        HookPurpose::Visitor,
        HookFrequency::EventStorm,
        observe_read(),
    )
    .map(|descriptor| {
        descriptor
            .with_module("server.dll")
            .require_fact(DiscoveryRequirement::required(game_event_post_fact()))
            .require_feature(FeatureDependency::required(FEATURE_GAME_EVENTS))
    })
}

fn game_frame() -> Result<HookDescriptor, HookRegistryError> {
    HookDescriptor::new(
        HookName::new("game.frame")?,
        ServerSurface::GameRules,
        HookPurpose::Visitor,
        HookFrequency::PerTick,
        Access::OBSERVE,
    )
    .map(|descriptor| {
        descriptor
            .with_module("server.dll")
            .require_fact(DiscoveryRequirement::required(game_frame_fact()))
            .require_feature(FeatureDependency::required(FEATURE_GAME_FRAME))
    })
}

fn client_lifecycle() -> Result<HookDescriptor, HookRegistryError> {
    HookDescriptor::new(
        HookName::new("client.lifecycle")?,
        ServerSurface::SteamAuth,
        HookPurpose::Visitor,
        HookFrequency::PerClient,
        observe_read(),
    )
    .map(|descriptor| {
        descriptor
            .with_module("engine2.dll")
            .require_fact(DiscoveryRequirement::required(client_lifecycle_fact()))
            .require_feature(FeatureDependency::required(FEATURE_CLIENT_LIFECYCLE))
    })
}

fn entity_lifecycle() -> Result<HookDescriptor, HookRegistryError> {
    HookDescriptor::new(
        HookName::new("entity.lifecycle")?,
        ServerSurface::EntitySimulation,
        HookPurpose::Visitor,
        HookFrequency::EventStorm,
        observe_read(),
    )
    .map(|descriptor| {
        descriptor
            .with_module("server.dll")
            .require_fact(DiscoveryRequirement::required(entity_lifecycle_fact()))
            .require_feature(FeatureDependency::required(FEATURE_ENTITY_LIFECYCLE))
    })
}

const fn observe_read() -> Access {
    Access::OBSERVE.union(Access::READ)
}
