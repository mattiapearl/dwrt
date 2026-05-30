use std::collections::BTreeMap;

#[cfg(test)]
mod tests;

use dwrt_engine::{Access, ServerSurface};

use crate::{BuildId, FactKey, ManifestError, MemoryFact, ModuleId};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryManifest {
    pub build: BuildId,
    pub modules: Vec<ModuleId>,
    facts: BTreeMap<FactKey, MemoryFact>,
}

impl MemoryManifest {
    #[must_use]
    pub fn new(build: BuildId) -> Self {
        Self {
            build,
            modules: Vec::new(),
            facts: BTreeMap::new(),
        }
    }

    pub fn add_module(&mut self, module: ModuleId) {
        self.modules.push(module);
    }

    pub fn insert(&mut self, fact: MemoryFact) -> Result<(), ManifestError> {
        validate_fact_shape(&fact)?;
        if self.facts.contains_key(&fact.key) {
            return Err(ManifestError::DuplicateFact(fact.key));
        }
        self.facts.insert(fact.key.clone(), fact);
        Ok(())
    }

    #[must_use]
    pub fn get(&self, key: &FactKey) -> Option<&MemoryFact> {
        self.facts.get(key)
    }

    pub fn facts(&self) -> impl Iterator<Item = &MemoryFact> {
        self.facts.values()
    }

    pub fn facts_for_surface(&self, surface: ServerSurface) -> impl Iterator<Item = &MemoryFact> {
        self.facts
            .values()
            .filter(move |fact| fact.key.surface == surface)
    }

    pub fn public_safe_facts(&self) -> impl Iterator<Item = &MemoryFact> {
        self.facts.values().filter(|fact| fact.is_public_safe())
    }

    pub fn validate(&self) -> Result<(), ManifestError> {
        for fact in self.facts.values() {
            validate_fact_shape(fact)?;
            if fact.evidence.is_empty() {
                return Err(ManifestError::MissingEvidence(fact.key.clone()));
            }
        }
        Ok(())
    }
}

fn validate_fact_shape(fact: &MemoryFact) -> Result<(), ManifestError> {
    let value_kind = fact.value.kind();
    if fact.key.kind != value_kind {
        return Err(ManifestError::KindMismatch {
            key: fact.key.clone(),
            value_kind,
        });
    }
    if fact.access.contains(Access::REPLACE) {
        return Err(ManifestError::ReplaceAccessForbidden(fact.key.clone()));
    }
    Ok(())
}
