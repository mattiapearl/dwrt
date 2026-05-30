use crate::*;
use dwrt_engine::{Access, ServerSurface};

fn test_build() -> BuildId {
    BuildId::new("DeadlockDedicatedServer").with_product_version("test")
}

#[test]
fn manifest_inserts_and_finds_schema_fact() {
    let mut manifest = MemoryManifest::new(test_build());
    let key = FactKey::new(
        ServerSurface::ControllerPawn,
        FactKind::SchemaField,
        "CCitadelPlayerPawn",
        "m_iHealth",
    );
    let fact = MemoryFact::new(
        key.clone(),
        FactValue::SchemaField {
            class_name: "CCitadelPlayerPawn".into(),
            field_name: "m_iHealth".into(),
            offset: 0x34c,
        },
        Access::OBSERVE.union(Access::READ).union(Access::MUTATE),
        Confidence::SchemaValidated,
    )
    .with_evidence(Evidence::new(
        "schema dump",
        "field resolved by schema system",
    ));

    manifest.insert(fact).unwrap();
    let found = manifest.get(&key).unwrap();
    assert_eq!(found.value.kind(), FactKind::SchemaField);
    assert!(found.is_public_safe());
    assert_eq!(
        manifest
            .facts_for_surface(ServerSurface::ControllerPawn)
            .count(),
        1
    );
    assert!(manifest.validate().is_ok());
}

#[test]
fn duplicate_facts_are_rejected() {
    let mut manifest = MemoryManifest::new(test_build());
    let key = FactKey::new(
        ServerSurface::UsercmdPipeline,
        FactKind::Offset,
        "CUserCmd",
        "protobuf",
    );
    let fact = MemoryFact::new(
        key.clone(),
        FactValue::Offset {
            offset: 0x10,
            size: None,
        },
        Access::OBSERVE.union(Access::READ),
        Confidence::SingleBuildValidated,
    );
    manifest.insert(fact.clone()).unwrap();
    assert_eq!(
        manifest.insert(fact),
        Err(ManifestError::DuplicateFact(key))
    );
}

#[test]
fn mismatched_fact_kind_is_rejected() {
    let mut manifest = MemoryManifest::new(test_build());
    let key = FactKey::new(
        ServerSurface::Physics,
        FactKind::VTableIndex,
        "Trace",
        "Ray",
    );
    let fact = MemoryFact::new(
        key.clone(),
        FactValue::Signature {
            module: "server.dll".into(),
            pattern: "48 8B ??".into(),
        },
        Access::OBSERVE.union(Access::CALL),
        Confidence::SingleBuildValidated,
    );
    assert_eq!(
        manifest.insert(fact),
        Err(ManifestError::KindMismatch {
            key,
            value_kind: FactKind::Signature
        })
    );
}

#[test]
fn replace_access_is_never_manifest_safe() {
    let mut manifest = MemoryManifest::new(test_build());
    let key = FactKey::new(
        ServerSurface::SnapshotGeneration,
        FactKind::Signature,
        "Snapshot",
        "Write",
    );
    let fact = MemoryFact::new(
        key.clone(),
        FactValue::Signature {
            module: "server.dll".into(),
            pattern: "40 55 ??".into(),
        },
        Access::OBSERVE.union(Access::REPLACE),
        Confidence::SingleBuildValidated,
    );
    assert_eq!(
        manifest.insert(fact),
        Err(ManifestError::ReplaceAccessForbidden(key))
    );
}

#[test]
fn validation_requires_evidence() {
    let mut manifest = MemoryManifest::new(test_build());
    let key = FactKey::new(
        ServerSurface::UsercmdPipeline,
        FactKind::Offset,
        "CUserCmd",
        "command_number",
    );
    manifest
        .insert(MemoryFact::new(
            key.clone(),
            FactValue::Offset {
                offset: 0x8,
                size: Some(4),
            },
            Access::OBSERVE.union(Access::READ),
            Confidence::SingleBuildValidated,
        ))
        .unwrap();
    assert_eq!(
        manifest.validate(),
        Err(ManifestError::MissingEvidence(key))
    );
}

#[test]
fn low_confidence_offsets_are_not_public_safe() {
    let fact = MemoryFact::new(
        FactKey::new(
            ServerSurface::UsercmdPipeline,
            FactKind::Offset,
            "CUserCmd",
            "command_number",
        ),
        FactValue::Offset {
            offset: 0x8,
            size: Some(4),
        },
        Access::OBSERVE.union(Access::READ),
        Confidence::SingleBuildValidated,
    );
    assert!(!fact.is_public_safe());
}
