use std::collections::BTreeMap;

use dwrt_memory::MemoryManifest;

use crate::{FeatureSet, HookDescriptor, HookName, HookRegistryError, HookRunMode, HookStatus};

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct HookRegistry {
    hooks: BTreeMap<HookName, HookDescriptor>,
    installed: BTreeMap<HookName, HookRunMode>,
}

impl HookRegistry {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register(&mut self, descriptor: HookDescriptor) -> Result<(), HookRegistryError> {
        descriptor.validate_shape()?;
        if self.hooks.contains_key(descriptor.name()) {
            return Err(HookRegistryError::DuplicateHook(descriptor.name().clone()));
        }
        self.hooks.insert(descriptor.name().clone(), descriptor);
        Ok(())
    }

    #[must_use]
    pub fn get(&self, name: &HookName) -> Option<&HookDescriptor> {
        self.hooks.get(name)
    }

    pub fn iter(&self) -> impl Iterator<Item = &HookDescriptor> {
        self.hooks.values()
    }

    pub fn evaluate(
        &self,
        name: &HookName,
        manifest: &MemoryManifest,
        features: &FeatureSet,
    ) -> Result<HookStatus, HookRegistryError> {
        let descriptor = self
            .hooks
            .get(name)
            .ok_or_else(|| HookRegistryError::MissingHook(name.clone()))?;
        Ok(self.evaluate_descriptor(descriptor, manifest, features))
    }

    #[must_use]
    pub fn evaluate_all(
        &self,
        manifest: &MemoryManifest,
        features: &FeatureSet,
    ) -> BTreeMap<HookName, HookStatus> {
        self.hooks
            .iter()
            .map(|(name, descriptor)| {
                (
                    name.clone(),
                    self.evaluate_descriptor(descriptor, manifest, features),
                )
            })
            .collect()
    }

    pub fn mark_installed(
        &mut self,
        name: &HookName,
        manifest: &MemoryManifest,
        features: &FeatureSet,
    ) -> Result<HookStatus, HookRegistryError> {
        let status = self.evaluate(name, manifest, features)?;
        if status.mode == HookRunMode::Disabled {
            return Err(HookRegistryError::CannotInstallDisabledHook(name.clone()));
        }
        if !status.is_usable() {
            return Err(HookRegistryError::CannotInstallUnresolvedHook(name.clone()));
        }
        self.installed.insert(name.clone(), status.mode);
        Ok(HookStatus::installed(status.mode))
    }

    fn evaluate_descriptor(
        &self,
        descriptor: &HookDescriptor,
        manifest: &MemoryManifest,
        features: &FeatureSet,
    ) -> HookStatus {
        if descriptor.run_mode() == HookRunMode::Disabled {
            return HookStatus::disabled(
                descriptor.run_mode(),
                "hook disabled by run mode",
                Vec::new(),
                Vec::new(),
            );
        }

        let missing_facts = descriptor
            .discovery()
            .iter()
            .filter(|requirement| {
                requirement.is_required() && manifest.get(requirement.fact()).is_none()
            })
            .map(|requirement| requirement.fact().clone())
            .collect::<Vec<_>>();

        let missing_features = features.missing_required(descriptor.features());

        if !missing_facts.is_empty() || !missing_features.is_empty() {
            return HookStatus::disabled(
                descriptor.run_mode(),
                "required hook facts or features are missing",
                missing_facts,
                missing_features,
            );
        }

        if self.installed.contains_key(descriptor.name()) {
            HookStatus::installed(descriptor.run_mode())
        } else {
            HookStatus::resolved(descriptor.run_mode())
        }
    }
}
