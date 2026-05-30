use dwrt_engine::{Access, ServerSurface};
use dwrt_memory::{FactKey, FactKind, FactValue, MemoryFact, MemoryManifest};

use crate::EntityError;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SchemaFieldPlan {
    pub class_name: &'static str,
    pub field_name: &'static str,
    pub offset: u32,
    pub access: Access,
}

impl SchemaFieldPlan {
    #[must_use]
    pub const fn can_read(self) -> bool {
        self.access.contains(Access::READ)
    }

    #[must_use]
    pub const fn can_write(self) -> bool {
        self.access.contains(Access::MUTATE)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlayerControllerSchema {
    pub pawn_handle: SchemaFieldPlan,
    pub player_name: Option<SchemaFieldPlan>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlayerPawnSchema {
    pub health: SchemaFieldPlan,
    pub max_health: Option<SchemaFieldPlan>,
    pub team_num: Option<SchemaFieldPlan>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EntitySchemaCatalog {
    pub controller: PlayerControllerSchema,
    pub pawn: PlayerPawnSchema,
}

impl EntitySchemaCatalog {
    pub fn from_manifest(manifest: &MemoryManifest) -> Result<Self, EntityError> {
        Ok(Self {
            controller: PlayerControllerSchema {
                pawn_handle: require_schema_field(
                    manifest,
                    ServerSurface::ControllerPawn,
                    "CBasePlayerController",
                    "m_hPawn",
                    RequiredAccess::Read,
                )?,
                player_name: optional_schema_field(
                    manifest,
                    ServerSurface::ControllerPawn,
                    "CBasePlayerController",
                    "m_iszPlayerName",
                    RequiredAccess::Read,
                )?,
            },
            pawn: PlayerPawnSchema {
                health: require_schema_field(
                    manifest,
                    ServerSurface::ControllerPawn,
                    "CBaseEntity",
                    "m_iHealth",
                    RequiredAccess::Write,
                )?,
                max_health: optional_schema_field(
                    manifest,
                    ServerSurface::ControllerPawn,
                    "CBaseEntity",
                    "m_iMaxHealth",
                    RequiredAccess::Read,
                )?,
                team_num: optional_schema_field(
                    manifest,
                    ServerSurface::ControllerPawn,
                    "CBaseEntity",
                    "m_iTeamNum",
                    RequiredAccess::Read,
                )?,
            },
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum RequiredAccess {
    Read,
    Write,
}

fn require_schema_field(
    manifest: &MemoryManifest,
    surface: ServerSurface,
    class_name: &'static str,
    field_name: &'static str,
    required_access: RequiredAccess,
) -> Result<SchemaFieldPlan, EntityError> {
    optional_schema_field(manifest, surface, class_name, field_name, required_access)?.ok_or(
        EntityError::MissingSchemaField {
            class_name,
            field_name,
        },
    )
}

fn optional_schema_field(
    manifest: &MemoryManifest,
    surface: ServerSurface,
    class_name: &'static str,
    field_name: &'static str,
    required_access: RequiredAccess,
) -> Result<Option<SchemaFieldPlan>, EntityError> {
    let key = FactKey::new(surface, FactKind::SchemaField, class_name, field_name);
    let Some(fact) = manifest.get(&key) else {
        return Ok(None);
    };
    schema_plan_from_fact(fact, class_name, field_name, required_access).map(Some)
}

fn schema_plan_from_fact(
    fact: &MemoryFact,
    class_name: &'static str,
    field_name: &'static str,
    required_access: RequiredAccess,
) -> Result<SchemaFieldPlan, EntityError> {
    if !fact.is_public_safe() {
        return Err(EntityError::FieldNotPublicSafe {
            class_name,
            field_name,
        });
    }

    let FactValue::SchemaField {
        class_name: fact_class,
        field_name: fact_field,
        offset,
    } = &fact.value
    else {
        return Err(EntityError::FactShapeMismatch {
            class_name,
            field_name,
        });
    };

    if fact_class != class_name || fact_field != field_name {
        return Err(EntityError::FactShapeMismatch {
            class_name,
            field_name,
        });
    }

    if !fact.access.contains(Access::READ) {
        return Err(EntityError::FieldNotReadable {
            class_name,
            field_name,
        });
    }

    if required_access == RequiredAccess::Write && !fact.access.contains(Access::MUTATE) {
        return Err(EntityError::FieldNotWritable {
            class_name,
            field_name,
        });
    }

    Ok(SchemaFieldPlan {
        class_name,
        field_name,
        offset: *offset,
        access: fact.access,
    })
}
