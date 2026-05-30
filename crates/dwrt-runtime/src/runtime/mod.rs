use dwrt_core::net::NetInterest;
use dwrt_core::usercmd::UsercmdPolicy;

pub struct DwrtRuntime {
    net: NetInterest,
    usercmd: UsercmdPolicy,
}

impl DwrtRuntime {
    #[must_use]
    pub fn new() -> Self {
        Self {
            net: NetInterest::new(),
            usercmd: UsercmdPolicy::new(),
        }
    }

    #[must_use]
    pub fn net(&self) -> &NetInterest {
        &self.net
    }

    #[must_use]
    pub fn usercmd(&self) -> &UsercmdPolicy {
        &self.usercmd
    }
}

impl Default for DwrtRuntime {
    fn default() -> Self {
        Self::new()
    }
}
