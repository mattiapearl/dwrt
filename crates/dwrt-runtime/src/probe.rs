use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};

use dwrt_ffi::{
    DWRT_PROBE_MOUNT_DAMAGE, DWRT_PROBE_MOUNT_ENTITY_INPUT, DWRT_PROBE_MOUNT_ENTITY_OUTPUT,
    DWRT_PROBE_MOUNT_ENTITY_TOUCH, DwrtProbeCountersNative, FastDamageNative, FastEntityIoNative,
    FastEntityTouchNative,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProbeRoute {
    NoInterest,
    Counted,
}

#[derive(Debug, Default)]
pub struct ProbeState {
    mount_mask: AtomicU32,
    damage_seen: AtomicU64,
    damage_counted: AtomicU64,
    entity_input_seen: AtomicU64,
    entity_input_counted: AtomicU64,
    entity_output_seen: AtomicU64,
    entity_output_counted: AtomicU64,
    entity_touch_seen: AtomicU64,
    entity_touch_counted: AtomicU64,
}

impl ProbeState {
    #[must_use]
    pub const fn new() -> Self {
        Self {
            mount_mask: AtomicU32::new(0),
            damage_seen: AtomicU64::new(0),
            damage_counted: AtomicU64::new(0),
            entity_input_seen: AtomicU64::new(0),
            entity_input_counted: AtomicU64::new(0),
            entity_output_seen: AtomicU64::new(0),
            entity_output_counted: AtomicU64::new(0),
            entity_touch_seen: AtomicU64::new(0),
            entity_touch_counted: AtomicU64::new(0),
        }
    }

    pub fn set_mount_mask(&self, mask: u32) {
        self.mount_mask.store(mask, Ordering::Release);
    }

    #[must_use]
    pub fn mount_mask(&self) -> u32 {
        self.mount_mask.load(Ordering::Acquire)
    }

    pub fn record_damage(&self, _event: &FastDamageNative) -> ProbeRoute {
        self.record_if_mounted(
            DWRT_PROBE_MOUNT_DAMAGE,
            &self.damage_seen,
            &self.damage_counted,
        )
    }

    pub fn record_entity_input(&self, _event: &FastEntityIoNative) -> ProbeRoute {
        self.record_if_mounted(
            DWRT_PROBE_MOUNT_ENTITY_INPUT,
            &self.entity_input_seen,
            &self.entity_input_counted,
        )
    }

    pub fn record_entity_output(&self, _event: &FastEntityIoNative) -> ProbeRoute {
        self.record_if_mounted(
            DWRT_PROBE_MOUNT_ENTITY_OUTPUT,
            &self.entity_output_seen,
            &self.entity_output_counted,
        )
    }

    pub fn record_entity_touch(&self, _event: &FastEntityTouchNative) -> ProbeRoute {
        self.record_if_mounted(
            DWRT_PROBE_MOUNT_ENTITY_TOUCH,
            &self.entity_touch_seen,
            &self.entity_touch_counted,
        )
    }

    #[must_use]
    pub fn snapshot(&self) -> DwrtProbeCountersNative {
        DwrtProbeCountersNative {
            mount_mask: self.mount_mask(),
            _pad0: 0,
            damage_seen: self.damage_seen.load(Ordering::Relaxed),
            damage_counted: self.damage_counted.load(Ordering::Relaxed),
            entity_input_seen: self.entity_input_seen.load(Ordering::Relaxed),
            entity_input_counted: self.entity_input_counted.load(Ordering::Relaxed),
            entity_output_seen: self.entity_output_seen.load(Ordering::Relaxed),
            entity_output_counted: self.entity_output_counted.load(Ordering::Relaxed),
            entity_touch_seen: self.entity_touch_seen.load(Ordering::Relaxed),
            entity_touch_counted: self.entity_touch_counted.load(Ordering::Relaxed),
        }
    }

    pub fn reset_counters(&self) {
        self.damage_seen.store(0, Ordering::Relaxed);
        self.damage_counted.store(0, Ordering::Relaxed);
        self.entity_input_seen.store(0, Ordering::Relaxed);
        self.entity_input_counted.store(0, Ordering::Relaxed);
        self.entity_output_seen.store(0, Ordering::Relaxed);
        self.entity_output_counted.store(0, Ordering::Relaxed);
        self.entity_touch_seen.store(0, Ordering::Relaxed);
        self.entity_touch_counted.store(0, Ordering::Relaxed);
    }

    fn record_if_mounted(&self, mount: u32, seen: &AtomicU64, counted: &AtomicU64) -> ProbeRoute {
        if self.mount_mask.load(Ordering::Relaxed) & mount == 0 {
            return ProbeRoute::NoInterest;
        }
        seen.fetch_add(1, Ordering::Relaxed);
        counted.fetch_add(1, Ordering::Relaxed);
        ProbeRoute::Counted
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_mount_returns_before_counting() {
        let state = ProbeState::new();
        assert_eq!(
            state.record_damage(&FastDamageNative::default()),
            ProbeRoute::NoInterest
        );
        let snapshot = state.snapshot();
        assert_eq!(snapshot.damage_seen, 0);
        assert_eq!(snapshot.damage_counted, 0);
    }

    #[test]
    fn mounted_probe_counts_interest() {
        let state = ProbeState::new();
        state.set_mount_mask(DWRT_PROBE_MOUNT_DAMAGE | DWRT_PROBE_MOUNT_ENTITY_INPUT);
        assert_eq!(
            state.record_damage(&FastDamageNative::default()),
            ProbeRoute::Counted
        );
        assert_eq!(
            state.record_entity_input(&FastEntityIoNative::default()),
            ProbeRoute::Counted
        );
        assert_eq!(
            state.record_entity_output(&FastEntityIoNative::default()),
            ProbeRoute::NoInterest
        );
        let snapshot = state.snapshot();
        assert_eq!(snapshot.damage_seen, 1);
        assert_eq!(snapshot.damage_counted, 1);
        assert_eq!(snapshot.entity_input_seen, 1);
        assert_eq!(snapshot.entity_input_counted, 1);
        assert_eq!(snapshot.entity_output_seen, 0);
        assert_eq!(snapshot.entity_output_counted, 0);
    }
}
