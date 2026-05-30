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
        .insert(
            MemoryFact::new(
                process_usercmds_fact_key(),
                FactValue::Signature {
                    module: "server.dll".into(),
                    pattern: "48 89 ??".into(),
                },
                Access::OBSERVE,
                Confidence::SingleBuildValidated,
            )
            .with_evidence(Evidence::new("unit-test", "synthetic signature")),
        )
        .unwrap();
    manifest
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
