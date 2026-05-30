use std::fmt;

use dwrt_engine::{Access, DiscoveryKind, ServerSurface};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum FactKind {
    Interface,
    Signature,
    SchemaField,
    Offset,
    VTableIndex,
}

impl FactKind {
    #[must_use]
    pub const fn discovery_kind(self) -> DiscoveryKind {
        match self {
            Self::Interface => DiscoveryKind::EngineInterface,
            Self::Signature => DiscoveryKind::Signature,
            Self::SchemaField => DiscoveryKind::Schema,
            Self::Offset => DiscoveryKind::VersionedOffset,
            Self::VTableIndex => DiscoveryKind::VTableIndex,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct FactKey {
    pub surface: ServerSurface,
    pub kind: FactKind,
    pub owner: String,
    pub name: String,
}

impl FactKey {
    #[must_use]
    pub fn new(
        surface: ServerSurface,
        kind: FactKind,
        owner: impl Into<String>,
        name: impl Into<String>,
    ) -> Self {
        Self {
            surface,
            kind,
            owner: owner.into(),
            name: name.into(),
        }
    }
}

impl fmt::Display for FactKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{:?}/{:?}/{}::{}",
            self.surface, self.kind, self.owner, self.name
        )
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Confidence {
    TraceOnly,
    SingleBuildValidated,
    MultiBuildValidated,
    SchemaValidated,
    EngineInterface,
}

impl Confidence {
    #[must_use]
    pub const fn allows_public_api(self) -> bool {
        matches!(
            self,
            Self::MultiBuildValidated | Self::SchemaValidated | Self::EngineInterface
        )
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Evidence {
    pub source: String,
    pub note: String,
}

impl Evidence {
    #[must_use]
    pub fn new(source: impl Into<String>, note: impl Into<String>) -> Self {
        Self {
            source: source.into(),
            note: note.into(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum FactValue {
    InterfaceName {
        module: String,
        interface: String,
    },
    Signature {
        module: String,
        pattern: String,
    },
    SchemaField {
        class_name: String,
        field_name: String,
        offset: u32,
    },
    Offset {
        offset: u32,
        size: Option<u32>,
    },
    VTableIndex {
        index: u32,
    },
}

impl FactValue {
    #[must_use]
    pub const fn kind(&self) -> FactKind {
        match self {
            Self::InterfaceName { .. } => FactKind::Interface,
            Self::Signature { .. } => FactKind::Signature,
            Self::SchemaField { .. } => FactKind::SchemaField,
            Self::Offset { .. } => FactKind::Offset,
            Self::VTableIndex { .. } => FactKind::VTableIndex,
        }
    }

    #[must_use]
    pub const fn is_schema_backed(&self) -> bool {
        matches!(self, Self::SchemaField { .. })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryFact {
    pub key: FactKey,
    pub value: FactValue,
    pub access: Access,
    pub confidence: Confidence,
    pub evidence: Vec<Evidence>,
}

impl MemoryFact {
    #[must_use]
    pub fn new(key: FactKey, value: FactValue, access: Access, confidence: Confidence) -> Self {
        Self {
            key,
            value,
            access,
            confidence,
            evidence: Vec::new(),
        }
    }

    #[must_use]
    pub fn with_evidence(mut self, evidence: Evidence) -> Self {
        self.evidence.push(evidence);
        self
    }

    #[must_use]
    pub fn is_public_safe(&self) -> bool {
        self.confidence.allows_public_api()
            && self.key.kind == self.value.kind()
            && !self.access.contains(Access::REPLACE)
    }
}
