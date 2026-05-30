use super::*;
use dwrt_engine::{Access, ServerSurface};
use dwrt_memory::{
    BuildId, Confidence, Evidence, FactKey, FactKind, FactValue, MemoryFact, MemoryManifest,
};

fn manifest_with_core_entity_fields() -> MemoryManifest {
    let mut manifest = MemoryManifest::new(BuildId::new("DeadlockDedicatedServer"));
    insert_schema(
        &mut manifest,
        "CBasePlayerController",
        "m_hPawn",
        0x62c,
        Access::OBSERVE.union(Access::READ),
    );
    insert_schema(
        &mut manifest,
        "CBaseEntity",
        "m_iHealth",
        0x34c,
        Access::OBSERVE.union(Access::READ).union(Access::MUTATE),
    );
    insert_schema(
        &mut manifest,
        "CBaseEntity",
        "m_iMaxHealth",
        0x350,
        Access::OBSERVE.union(Access::READ),
    );
    manifest
}

fn insert_schema(
    manifest: &mut MemoryManifest,
    class_name: &'static str,
    field_name: &'static str,
    offset: u32,
    access: Access,
) {
    let key = FactKey::new(
        ServerSurface::ControllerPawn,
        FactKind::SchemaField,
        class_name,
        field_name,
    );
    manifest
        .insert(
            MemoryFact::new(
                key,
                FactValue::SchemaField {
                    class_name: class_name.into(),
                    field_name: field_name.into(),
                    offset,
                },
                access,
                Confidence::SchemaValidated,
            )
            .with_evidence(Evidence::new("unit-test schema", "synthetic schema field")),
        )
        .unwrap();
}

#[test]
fn player_slot_validates_range() {
    assert_eq!(PlayerSlot::new(0).unwrap().get(), 0);
    assert_eq!(PlayerSlot::new(63).unwrap().get(), 63);
    assert_eq!(PlayerSlot::new(-1), Err(EntityError::InvalidPlayerSlot(-1)));
    assert_eq!(PlayerSlot::new(64), Err(EntityError::InvalidPlayerSlot(64)));
}

#[test]
fn entity_handles_track_validity_without_deref() {
    assert!(EntityHandle::from_raw(1).is_valid());
    assert!(!EntityHandle::from_raw(EntityHandle::INVALID_RAW).is_valid());
}

#[test]
fn engine_ref_rejects_null_and_preserves_address() {
    unsafe {
        assert!(EngineRef::<CCitadelPlayerPawn>::from_addr(0).is_none());
        let ptr = EngineRef::<CCitadelPlayerPawn>::from_addr(0x1234).unwrap();
        assert_eq!(ptr.addr(), 0x1234);
    }
}

#[test]
fn controller_and_pawn_refs_are_typed_handles() {
    unsafe {
        let controller = PlayerControllerRef::new(
            EngineRef::from_addr(0x1000).unwrap(),
            PlayerSlot::new(2).unwrap(),
            Some(EntityHandle::from_raw(77)),
        );
        let pawn = PlayerPawnRef::new(
            EngineRef::from_addr(0x2000).unwrap(),
            EntityIndex::new(88),
            Some(EntityHandle::from_raw(99)),
            Some(controller.slot()),
        );
        assert_eq!(controller.slot().get(), 2);
        assert_eq!(controller.handle().unwrap().raw(), 77);
        assert_eq!(pawn.owner_slot().unwrap(), controller.slot());
        assert_eq!(pawn.entity_index().get(), 88);
    }
}

#[test]
fn schema_catalog_loads_manifest_backed_fields() {
    let manifest = manifest_with_core_entity_fields();
    let catalog = EntitySchemaCatalog::from_manifest(&manifest).unwrap();
    assert_eq!(catalog.controller.pawn_handle.offset, 0x62c);
    assert!(catalog.controller.pawn_handle.can_read());
    assert!(!catalog.controller.pawn_handle.can_write());
    assert_eq!(catalog.pawn.health.offset, 0x34c);
    assert!(catalog.pawn.health.can_write());
    assert_eq!(catalog.pawn.max_health.unwrap().offset, 0x350);
    assert!(catalog.pawn.team_num.is_none());
}

#[test]
fn required_fields_must_exist() {
    let manifest = MemoryManifest::new(BuildId::new("DeadlockDedicatedServer"));
    assert_eq!(
        EntitySchemaCatalog::from_manifest(&manifest),
        Err(EntityError::MissingSchemaField {
            class_name: "CBasePlayerController",
            field_name: "m_hPawn"
        })
    );
}

#[test]
fn writable_fields_require_mutate_access() {
    let mut manifest = MemoryManifest::new(BuildId::new("DeadlockDedicatedServer"));
    insert_schema(
        &mut manifest,
        "CBasePlayerController",
        "m_hPawn",
        0x62c,
        Access::OBSERVE.union(Access::READ),
    );
    insert_schema(
        &mut manifest,
        "CBaseEntity",
        "m_iHealth",
        0x34c,
        Access::OBSERVE.union(Access::READ),
    );
    assert_eq!(
        EntitySchemaCatalog::from_manifest(&manifest),
        Err(EntityError::FieldNotWritable {
            class_name: "CBaseEntity",
            field_name: "m_iHealth"
        })
    );
}

#[test]
fn low_confidence_schema_fields_are_not_exposed() {
    let mut manifest = MemoryManifest::new(BuildId::new("DeadlockDedicatedServer"));
    insert_schema(
        &mut manifest,
        "CBasePlayerController",
        "m_hPawn",
        0x62c,
        Access::OBSERVE.union(Access::READ),
    );
    let key = FactKey::new(
        ServerSurface::ControllerPawn,
        FactKind::SchemaField,
        "CBaseEntity",
        "m_iHealth",
    );
    manifest
        .insert(
            MemoryFact::new(
                key,
                FactValue::SchemaField {
                    class_name: "CBaseEntity".into(),
                    field_name: "m_iHealth".into(),
                    offset: 0x34c,
                },
                Access::OBSERVE.union(Access::READ).union(Access::MUTATE),
                Confidence::SingleBuildValidated,
            )
            .with_evidence(Evidence::new("unit-test", "low confidence")),
        )
        .unwrap();
    assert_eq!(
        EntitySchemaCatalog::from_manifest(&manifest),
        Err(EntityError::FieldNotPublicSafe {
            class_name: "CBaseEntity",
            field_name: "m_iHealth"
        })
    );
}
