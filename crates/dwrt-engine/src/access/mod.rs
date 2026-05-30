#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum Authority {
    /// Deadlock/Source 2 remains authoritative. Runtime may observe or call
    /// curated functions but does not replace the subsystem.
    Engine,
    /// Runtime-owned control-plane feature layered above the engine.
    Runtime,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum DiscoveryKind {
    EngineInterface,
    Hook,
    Protobuf,
    Schema,
    Signature,
    VersionedOffset,
    VTableIndex,
    TraceOnly,
    RuntimeOwned,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Access(u32);

impl Access {
    pub const NONE: Self = Self(0);
    pub const OBSERVE: Self = Self(1 << 0);
    pub const READ: Self = Self(1 << 1);
    pub const CALL: Self = Self(1 << 2);
    pub const MUTATE: Self = Self(1 << 3);
    pub const BLOCK: Self = Self(1 << 4);
    pub const REPLACE: Self = Self(1 << 5);

    #[must_use]
    pub const fn bits(self) -> u32 {
        self.0
    }

    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        (self.0 & other.0) == other.0
    }

    #[must_use]
    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    #[must_use]
    pub const fn without(self, other: Self) -> Self {
        Self(self.0 & !other.0)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HotPathPolicy {
    NotHot,
    InterestGated,
    Mounted,
    RuntimeInternalOnly,
}
