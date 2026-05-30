use std::collections::BTreeSet;

#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct FeatureDependency {
    name: String,
    required: bool,
}

impl FeatureDependency {
    #[must_use]
    pub fn required(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            required: true,
        }
    }

    #[must_use]
    pub fn optional(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            required: false,
        }
    }

    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }

    #[must_use]
    pub const fn is_required(&self) -> bool {
        self.required
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct FeatureSet {
    enabled: BTreeSet<String>,
}

impl FeatureSet {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn enable(&mut self, name: impl Into<String>) -> bool {
        self.enabled.insert(name.into())
    }

    pub fn disable(&mut self, name: &str) -> bool {
        self.enabled.remove(name)
    }

    #[must_use]
    pub fn contains(&self, name: &str) -> bool {
        self.enabled.contains(name)
    }

    #[must_use]
    pub fn missing_required<'a>(
        &self,
        dependencies: impl IntoIterator<Item = &'a FeatureDependency>,
    ) -> Vec<String> {
        dependencies
            .into_iter()
            .filter(|dependency| dependency.is_required() && !self.contains(dependency.name()))
            .map(|dependency| dependency.name().to_owned())
            .collect()
    }
}
