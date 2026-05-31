use dwrt_core::net::NetInterest;
use dwrt_core::usercmd::UsercmdPolicy;

use crate::probe::ProbeState;

pub struct DwrtRuntime {
    net: NetInterest,
    usercmd: UsercmdPolicy,
    probe: ProbeState,
}

impl DwrtRuntime {
    #[must_use]
    pub fn new() -> Self {
        Self {
            net: NetInterest::new(),
            usercmd: UsercmdPolicy::new(),
            probe: ProbeState::new(),
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

    #[must_use]
    pub const fn probe(&self) -> &ProbeState {
        &self.probe
    }
}

impl Default for DwrtRuntime {
    fn default() -> Self {
        Self::new()
    }
}
