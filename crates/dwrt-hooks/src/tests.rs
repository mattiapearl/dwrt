use std::collections::BTreeSet;

use dwrt_engine::{Access, ServerSurface};
use dwrt_memory::{
    BuildId, Confidence, Evidence, FactKey, FactKind, FactValue, MemoryFact, MemoryManifest,
};

use crate::*;

fn process_usercmds_fact_key() -> FactKey {
    FactKey::new(
        ServerSurface::UsercmdPipeline,
        FactKind::Signature,
        "CBasePlayerController",
        "ProcessUsercmds",
    )
}

fn manifest_with_process_usercmds() -> MemoryManifest {
    let mut manifest = MemoryManifest::new(BuildId::new("DeadlockDedicatedServer"));
    manifest
        .insert(fact_for_key(process_usercmds_fact_key()))
        .unwrap();
    manifest
}

fn manifest_with_default_hook_facts() -> MemoryManifest {
    let mut manifest = MemoryManifest::new(BuildId::new("DeadlockDedicatedServer"));
    for key in [
        process_usercmds_fact(),
        filter_message_fact(),
        post_event_abstract_fact(),
        game_event_post_fact(),
        game_frame_fact(),
        client_lifecycle_fact(),
        entity_lifecycle_fact(),
    ] {
        manifest.insert(fact_for_key(key)).unwrap();
    }
    manifest
}

fn fact_for_key(key: FactKey) -> MemoryFact {
    let value = match key.kind {
        FactKind::Interface => FactValue::InterfaceName {
            module: key.owner.clone(),
            interface: key.name.clone(),
        },
        FactKind::Signature => FactValue::Signature {
            module: key.owner.clone(),
            pattern: "synthetic".into(),
        },
        FactKind::SchemaField => FactValue::SchemaField {
            class_name: key.owner.clone(),
            field_name: key.name.clone(),
            offset: 0,
        },
        FactKind::Offset => FactValue::Offset {
            offset: 0,
            size: None,
        },
        FactKind::VTableIndex => FactValue::VTableIndex { index: 0 },
    };

    MemoryFact::new(
        key,
        value,
        Access::OBSERVE,
        Confidence::SingleBuildValidated,
    )
    .with_evidence(Evidence::new("unit-test", "synthetic fact"))
}

fn usercmd_visitor_descriptor() -> HookDescriptor {
    HookDescriptor::new(
        HookName::new("usercmd.process.visitor").unwrap(),
        ServerSurface::UsercmdPipeline,
        HookPurpose::Visitor,
        HookFrequency::PerUsercmd,
        Access::OBSERVE.union(Access::READ),
    )
    .unwrap()
    .with_module("server.dll")
    .require_fact(DiscoveryRequirement::required(process_usercmds_fact_key()))
    .require_feature(FeatureDependency::required("usercmd.fast_read"))
}

#[test]
fn hook_names_are_validated() {
    assert!(HookName::new("usercmd.process.visitor").is_ok());
    assert!(HookName::new("").is_err());
    assert!(HookName::new("bad hook name").is_err());
}

#[test]
fn visitor_descriptors_cannot_request_write_access() {
    let err = HookDescriptor::new(
        HookName::new("net.outgoing.visitor").unwrap(),
        ServerSurface::NetMessagePipeline,
        HookPurpose::Visitor,
        HookFrequency::PerPacket,
        Access::OBSERVE.union(Access::MUTATE),
    )
    .unwrap_err();
    assert!(matches!(
        err,
        HookRegistryError::VisitorRequestsWriteAccess(_)
    ));
}

#[test]
fn duplicate_hooks_are_rejected() {
    let descriptor = usercmd_visitor_descriptor();
    let mut registry = HookRegistry::new();
    registry.register(descriptor.clone()).unwrap();
    assert_eq!(
        registry.register(descriptor),
        Err(HookRegistryError::DuplicateHook(
            HookName::new("usercmd.process.visitor").unwrap()
        ))
    );
}

#[test]
fn missing_discovery_fact_disables_hook() {
    let mut registry = HookRegistry::new();
    let descriptor = usercmd_visitor_descriptor();
    let name = descriptor.name().clone();
    registry.register(descriptor).unwrap();
    let mut features = FeatureSet::new();
    features.enable("usercmd.fast_read");

    let status = registry
        .evaluate(
            &name,
            &MemoryManifest::new(BuildId::new("DeadlockDedicatedServer")),
            &features,
        )
        .unwrap();
    assert!(!status.is_usable());
    assert_eq!(status.missing_facts, vec![process_usercmds_fact_key()]);
}

#[test]
fn missing_feature_disables_hook() {
    let mut registry = HookRegistry::new();
    let descriptor = usercmd_visitor_descriptor();
    let name = descriptor.name().clone();
    registry.register(descriptor).unwrap();

    let status = registry
        .evaluate(&name, &manifest_with_process_usercmds(), &FeatureSet::new())
        .unwrap();
    assert!(!status.is_usable());
    assert_eq!(
        status.missing_features,
        vec!["usercmd.fast_read".to_string()]
    );
}

#[test]
fn hook_resolves_in_shadow_mode_when_requirements_are_present() {
    let mut registry = HookRegistry::new();
    let descriptor = usercmd_visitor_descriptor();
    let name = descriptor.name().clone();
    registry.register(descriptor).unwrap();
    let mut features = FeatureSet::new();
    features.enable("usercmd.fast_read");

    let status = registry
        .evaluate(&name, &manifest_with_process_usercmds(), &features)
        .unwrap();
    assert!(status.is_usable());
    assert_eq!(status.mode, HookRunMode::Shadow);
    assert_eq!(status.health, HookHealth::Resolved);
}

#[test]
fn active_hook_can_be_marked_installed() {
    let mut registry = HookRegistry::new();
    let descriptor = usercmd_visitor_descriptor().with_run_mode(HookRunMode::Active);
    let name = descriptor.name().clone();
    registry.register(descriptor).unwrap();
    let mut features = FeatureSet::new();
    features.enable("usercmd.fast_read");

    let installed = registry
        .mark_installed(&name, &manifest_with_process_usercmds(), &features)
        .unwrap();
    assert_eq!(installed.mode, HookRunMode::Active);
    assert_eq!(installed.health, HookHealth::Installed);

    let status = registry
        .evaluate(&name, &manifest_with_process_usercmds(), &features)
        .unwrap();
    assert_eq!(status.health, HookHealth::Installed);
}

#[test]
fn disabled_hooks_cannot_be_installed() {
    let mut registry = HookRegistry::new();
    let descriptor = usercmd_visitor_descriptor().with_run_mode(HookRunMode::Disabled);
    let name = descriptor.name().clone();
    registry.register(descriptor).unwrap();
    let mut features = FeatureSet::new();
    features.enable("usercmd.fast_read");

    assert_eq!(
        registry.mark_installed(&name, &manifest_with_process_usercmds(), &features),
        Err(HookRegistryError::CannotInstallDisabledHook(name))
    );
}

#[test]
fn default_hook_descriptors_cover_initial_runtime_surfaces() {
    let descriptors = default_hook_descriptors().unwrap();
    let names = descriptors
        .iter()
        .map(|descriptor| descriptor.name().as_str().to_owned())
        .collect::<BTreeSet<_>>();

    assert_eq!(descriptors.len(), 7);
    assert!(names.contains("usercmd.process"));
    assert!(names.contains("net.incoming.filter_message"));
    assert!(names.contains("net.outgoing.post_event"));
    assert!(names.contains("game.event.post_event"));
    assert!(names.contains("game.frame"));
    assert!(names.contains("client.lifecycle"));
    assert!(names.contains("entity.lifecycle"));

    for descriptor in &descriptors {
        assert_ne!(descriptor.module(), "unknown");
        assert_eq!(descriptor.run_mode(), HookRunMode::Shadow);
        assert!(!descriptor.discovery().is_empty());
        assert!(!descriptor.features().is_empty());
    }
}

#[test]
fn default_hook_registry_resolves_with_default_facts_and_features() {
    let registry = default_hook_registry().unwrap();
    let statuses =
        registry.evaluate_all(&manifest_with_default_hook_facts(), &default_feature_set());

    assert_eq!(statuses.len(), 7);
    assert!(statuses.values().all(HookStatus::is_usable));
    assert!(
        statuses
            .values()
            .all(|status| status.health == HookHealth::Resolved)
    );
}

#[test]
fn default_hook_registry_disables_missing_feature_group() {
    let registry = default_hook_registry().unwrap();
    let mut features = default_feature_set();
    features.disable(FEATURE_NET);

    let status = registry
        .evaluate(
            &HookName::new("net.incoming.filter_message").unwrap(),
            &manifest_with_default_hook_facts(),
            &features,
        )
        .unwrap();

    assert!(!status.is_usable());
    assert_eq!(status.missing_features, vec![FEATURE_NET.to_string()]);
}
