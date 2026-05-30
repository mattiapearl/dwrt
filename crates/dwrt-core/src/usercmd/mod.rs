use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UsercmdMount(u32);

impl UsercmdMount {
    pub const NONE: Self = Self(0);
    pub const FULL_PROTOBUF: Self = Self(1 << 0);
    pub const FAST_READ: Self = Self(1 << 1);
    pub const BUTTON_TRIGGERS: Self = Self(1 << 2);
    pub const ALL: Self = Self(Self::FULL_PROTOBUF.0 | Self::FAST_READ.0 | Self::BUTTON_TRIGGERS.0);

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

    #[must_use]
    pub const fn from_bits_truncate(bits: u32) -> Self {
        Self(bits & Self::ALL.0)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UsercmdFields(u32);

impl UsercmdFields {
    pub const NONE: Self = Self(0);
    pub const CLIENT_TICK: Self = Self(1 << 0);
    pub const BUTTONS: Self = Self(1 << 1);
    pub const VIEW_ANGLES: Self = Self(1 << 2);
    pub const MOVEMENT: Self = Self(1 << 3);
    pub const ALL: Self =
        Self(Self::CLIENT_TICK.0 | Self::BUTTONS.0 | Self::VIEW_ANGLES.0 | Self::MOVEMENT.0);

    #[must_use]
    pub const fn bits(self) -> u32 {
        self.0
    }

    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        (self.0 & other.0) == other.0
    }

    #[must_use]
    pub const fn from_bits_truncate(bits: u32) -> Self {
        Self(bits & Self::ALL.0)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UsercmdRoute {
    NoWork,
    CountOnly,
    FastRead,
    FullProtobuf,
    FastAndFull,
}

impl UsercmdRoute {
    #[must_use]
    pub const fn wants_fast(self) -> bool {
        matches!(self, Self::FastRead | Self::FastAndFull)
    }

    #[must_use]
    pub const fn wants_full_protobuf(self) -> bool {
        matches!(self, Self::FullProtobuf | Self::FastAndFull)
    }
}

/// Runtime policy for the ProcessUsercmds hook.
pub struct UsercmdPolicy {
    mount_mask: AtomicU32,
    field_mask: AtomicU32,
    button_trigger_mask: AtomicU64,
}

impl UsercmdPolicy {
    #[must_use]
    pub const fn new() -> Self {
        Self {
            mount_mask: AtomicU32::new(0),
            field_mask: AtomicU32::new(UsercmdFields::ALL.bits()),
            button_trigger_mask: AtomicU64::new(0),
        }
    }

    pub fn mount(&self, mount: UsercmdMount) {
        self.mount_mask.fetch_or(mount.bits(), Ordering::Relaxed);
    }

    pub fn unmount(&self, mount: UsercmdMount) {
        self.mount_mask.fetch_and(!mount.bits(), Ordering::Relaxed);
    }

    pub fn set_mount_mask(&self, mount: UsercmdMount) {
        self.mount_mask.store(mount.bits(), Ordering::Relaxed);
    }

    #[must_use]
    pub fn mount_mask(&self) -> UsercmdMount {
        UsercmdMount::from_bits_truncate(self.mount_mask.load(Ordering::Relaxed))
    }

    pub fn set_field_mask(&self, fields: UsercmdFields) {
        self.field_mask.store(fields.bits(), Ordering::Relaxed);
    }

    #[must_use]
    pub fn field_mask(&self) -> UsercmdFields {
        UsercmdFields::from_bits_truncate(self.field_mask.load(Ordering::Relaxed))
    }

    pub fn set_button_trigger_mask(&self, mask: u64) {
        self.button_trigger_mask.store(mask, Ordering::Relaxed);
    }

    #[must_use]
    pub fn button_trigger_mask(&self) -> u64 {
        self.button_trigger_mask.load(Ordering::Relaxed)
    }

    /// Returns the minimum work required for this usercmd batch.
    #[must_use]
    pub fn route(&self) -> UsercmdRoute {
        let mount = self.mount_mask();
        let full = mount.contains(UsercmdMount::FULL_PROTOBUF);
        let fast = mount.contains(UsercmdMount::FAST_READ)
            || mount.contains(UsercmdMount::BUTTON_TRIGGERS);

        match (fast, full) {
            (false, false) => UsercmdRoute::CountOnly,
            (true, false) => UsercmdRoute::FastRead,
            (false, true) => UsercmdRoute::FullProtobuf,
            (true, true) => UsercmdRoute::FastAndFull,
        }
    }
}

impl Default for UsercmdPolicy {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_policy_counts_only() {
        let policy = UsercmdPolicy::new();
        assert_eq!(policy.route(), UsercmdRoute::CountOnly);
    }

    #[test]
    fn fast_and_full_mounts_are_composed() {
        let policy = UsercmdPolicy::new();
        policy.mount(UsercmdMount::FAST_READ);
        assert_eq!(policy.route(), UsercmdRoute::FastRead);

        policy.mount(UsercmdMount::FULL_PROTOBUF);
        assert_eq!(policy.route(), UsercmdRoute::FastAndFull);

        policy.unmount(UsercmdMount::FAST_READ);
        assert_eq!(policy.route(), UsercmdRoute::FullProtobuf);
    }

    #[test]
    fn button_triggers_use_fast_read_route() {
        let policy = UsercmdPolicy::new();
        policy.mount(UsercmdMount::BUTTON_TRIGGERS);
        policy.set_button_trigger_mask(0x20);
        assert_eq!(policy.route(), UsercmdRoute::FastRead);
        assert_eq!(policy.button_trigger_mask(), 0x20);
    }

    #[test]
    fn masks_truncate_unknown_bits() {
        assert_eq!(
            UsercmdMount::from_bits_truncate(u32::MAX),
            UsercmdMount::ALL
        );
        assert_eq!(
            UsercmdFields::from_bits_truncate(u32::MAX),
            UsercmdFields::ALL
        );
    }
}
