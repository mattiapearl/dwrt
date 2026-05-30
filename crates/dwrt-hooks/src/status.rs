use dwrt_memory::FactKey;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum HookFrequency {
    Cold,
    MapLifecycle,
    PerClient,
    PerTick,
    PerPacket,
    PerUsercmd,
    EventStorm,
}

impl HookFrequency {
    #[must_use]
    pub const fn is_hot(self) -> bool {
        matches!(
            self,
            Self::PerTick | Self::PerPacket | Self::PerUsercmd | Self::EventStorm
        )
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum HookRunMode {
    Disabled,
    Shadow,
    Active,
}

impl HookRunMode {
    #[must_use]
    pub const fn can_change_engine_behavior(self) -> bool {
        matches!(self, Self::Active)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum HookHealth {
    Declared,
    Resolved,
    Installed,
    Disabled { reason: String },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HookStatus {
    pub mode: HookRunMode,
    pub health: HookHealth,
    pub missing_facts: Vec<FactKey>,
    pub missing_features: Vec<String>,
}

impl HookStatus {
    #[must_use]
    pub fn declared(mode: HookRunMode) -> Self {
        Self {
            mode,
            health: HookHealth::Declared,
            missing_facts: Vec::new(),
            missing_features: Vec::new(),
        }
    }

    #[must_use]
    pub fn resolved(mode: HookRunMode) -> Self {
        Self {
            mode,
            health: HookHealth::Resolved,
            missing_facts: Vec::new(),
            missing_features: Vec::new(),
        }
    }

    #[must_use]
    pub fn installed(mode: HookRunMode) -> Self {
        Self {
            mode,
            health: HookHealth::Installed,
            missing_facts: Vec::new(),
            missing_features: Vec::new(),
        }
    }

    #[must_use]
    pub fn disabled(
        mode: HookRunMode,
        reason: impl Into<String>,
        missing_facts: Vec<FactKey>,
        missing_features: Vec<String>,
    ) -> Self {
        Self {
            mode,
            health: HookHealth::Disabled {
                reason: reason.into(),
            },
            missing_facts,
            missing_features,
        }
    }

    #[must_use]
    pub fn is_usable(&self) -> bool {
        matches!(self.health, HookHealth::Resolved | HookHealth::Installed)
            && self.mode != HookRunMode::Disabled
            && self.missing_facts.is_empty()
            && self.missing_features.is_empty()
    }
}
