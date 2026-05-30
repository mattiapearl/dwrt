use crate::{Access, Authority, DiscoveryKind, HotPathPolicy};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum ServerSurface {
    DedicatedServerExecutable,
    Source2Networking,
    SteamAuth,
    SnapshotGeneration,
    EntitySimulation,
    Physics,
    Prediction,
    GameRules,
    MapLoading,
    ResourcesAndStringTables,
    ProtobufSerializers,
    ControllerPawn,
    UsercmdPipeline,
    NetMessagePipeline,
    GameEvents,
    EntitySchema,
    EntityIo,
    ConsoleCommands,
    ChatCommands,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SurfaceDescriptor {
    pub surface: ServerSurface,
    pub authority: Authority,
    pub discovery: &'static [DiscoveryKind],
    pub default_access: Access,
    pub hot_path: HotPathPolicy,
    pub public_api: &'static str,
    pub notes: &'static str,
}

impl SurfaceDescriptor {
    #[must_use]
    pub const fn can_observe(&self) -> bool {
        self.default_access.contains(Access::OBSERVE)
    }

    #[must_use]
    pub const fn can_read(&self) -> bool {
        self.default_access.contains(Access::READ)
    }

    #[must_use]
    pub const fn can_mutate(&self) -> bool {
        self.default_access.contains(Access::MUTATE)
    }

    #[must_use]
    pub const fn can_replace(&self) -> bool {
        self.default_access.contains(Access::REPLACE)
    }

    #[must_use]
    pub const fn requires_interest_gate(&self) -> bool {
        matches!(
            self.hot_path,
            HotPathPolicy::InterestGated | HotPathPolicy::Mounted
        )
    }
}
