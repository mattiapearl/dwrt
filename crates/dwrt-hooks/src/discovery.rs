use dwrt_memory::FactKey;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiscoveryRequirement {
    fact: FactKey,
    required: bool,
}

impl DiscoveryRequirement {
    #[must_use]
    pub fn required(fact: FactKey) -> Self {
        Self {
            fact,
            required: true,
        }
    }

    #[must_use]
    pub fn optional(fact: FactKey) -> Self {
        Self {
            fact,
            required: false,
        }
    }

    #[must_use]
    pub fn fact(&self) -> &FactKey {
        &self.fact
    }

    #[must_use]
    pub const fn is_required(&self) -> bool {
        self.required
    }
}
