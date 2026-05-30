use std::fmt;

use dwrt_engine::{Access, ServerSurface};

use crate::{
    DiscoveryRequirement, FeatureDependency, HookFrequency, HookRegistryError, HookRunMode,
};

#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct HookName(String);

impl HookName {
    pub fn new(name: impl Into<String>) -> Result<Self, HookRegistryError> {
        let name = name.into();
        if name.trim().is_empty() {
            return Err(HookRegistryError::InvalidHookName(name));
        }
        if !name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
        {
            return Err(HookRegistryError::InvalidHookName(name));
        }
        Ok(Self(name))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for HookName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum HookPurpose {
    /// Read-only observation path. Must not request block/mutate/call access.
    Visitor,
    /// Scoped intervention path. May request block/mutate/call access.
    Hook,
    /// One low-level engine hook that supports both read-only visitors and
    /// mutating/blocking hook contexts behind separate runtime subscriptions.
    VisitorAndHook,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HookDescriptor {
    name: HookName,
    surface: ServerSurface,
    purpose: HookPurpose,
    frequency: HookFrequency,
    access: Access,
    run_mode: HookRunMode,
    discovery: Vec<DiscoveryRequirement>,
    features: Vec<FeatureDependency>,
}

impl HookDescriptor {
    pub fn new(
        name: HookName,
        surface: ServerSurface,
        purpose: HookPurpose,
        frequency: HookFrequency,
        access: Access,
    ) -> Result<Self, HookRegistryError> {
        let descriptor = Self {
            name,
            surface,
            purpose,
            frequency,
            access,
            run_mode: HookRunMode::Shadow,
            discovery: Vec::new(),
            features: Vec::new(),
        };
        descriptor.validate_shape()?;
        Ok(descriptor)
    }

    #[must_use]
    pub fn name(&self) -> &HookName {
        &self.name
    }

    #[must_use]
    pub const fn surface(&self) -> ServerSurface {
        self.surface
    }

    #[must_use]
    pub const fn purpose(&self) -> HookPurpose {
        self.purpose
    }

    #[must_use]
    pub const fn frequency(&self) -> HookFrequency {
        self.frequency
    }

    #[must_use]
    pub const fn access(&self) -> Access {
        self.access
    }

    #[must_use]
    pub const fn run_mode(&self) -> HookRunMode {
        self.run_mode
    }

    #[must_use]
    pub fn discovery(&self) -> &[DiscoveryRequirement] {
        &self.discovery
    }

    #[must_use]
    pub fn features(&self) -> &[FeatureDependency] {
        &self.features
    }

    pub fn with_run_mode(mut self, run_mode: HookRunMode) -> Self {
        self.run_mode = run_mode;
        self
    }

    pub fn require_fact(mut self, discovery: DiscoveryRequirement) -> Self {
        self.discovery.push(discovery);
        self
    }

    pub fn require_feature(mut self, feature: FeatureDependency) -> Self {
        self.features.push(feature);
        self
    }

    pub fn validate_shape(&self) -> Result<(), HookRegistryError> {
        if self.purpose == HookPurpose::Visitor {
            for forbidden in [Access::BLOCK, Access::MUTATE, Access::CALL, Access::REPLACE] {
                if self.access.contains(forbidden) {
                    return Err(HookRegistryError::VisitorRequestsWriteAccess(
                        self.name.clone(),
                    ));
                }
            }
        }
        if self.access.contains(Access::REPLACE) {
            return Err(HookRegistryError::ReplaceAccessForbidden(self.name.clone()));
        }
        Ok(())
    }
}
