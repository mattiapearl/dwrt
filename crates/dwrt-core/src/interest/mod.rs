use std::sync::atomic::{AtomicU32, Ordering};

/// Ref-counted fixed-size interest table.
///
/// Plugins/scripts can mount the same id independently. The hot path only asks
/// whether the id is mounted (`has`) or whether anything at all is mounted
/// (`has_any`). Add/remove are expected from control paths, not per packet.
pub struct InterestTable {
    counts: Box<[AtomicU32]>,
    active_ids: AtomicU32,
}

impl InterestTable {
    #[must_use]
    pub fn new(capacity: usize) -> Self {
        let counts = (0..capacity).map(|_| AtomicU32::new(0)).collect();
        Self {
            counts,
            active_ids: AtomicU32::new(0),
        }
    }

    #[must_use]
    pub fn capacity(&self) -> usize {
        self.counts.len()
    }

    #[must_use]
    pub fn active_ids(&self) -> u32 {
        self.active_ids.load(Ordering::Relaxed)
    }

    #[must_use]
    pub fn has_any(&self) -> bool {
        self.active_ids() != 0
    }

    #[must_use]
    pub fn has(&self, id: i32) -> bool {
        let Some(entry) = self.entry(id) else {
            return false;
        };
        entry.load(Ordering::Relaxed) != 0
    }

    /// Adds one mount for `id`.
    ///
    /// Returns `true` only when the id transitions from disabled to enabled.
    pub fn add(&self, id: i32) -> bool {
        let Some(entry) = self.entry(id) else {
            return false;
        };

        let previous = entry.fetch_add(1, Ordering::Relaxed);
        if previous == 0 {
            self.active_ids.fetch_add(1, Ordering::Relaxed);
            true
        } else {
            false
        }
    }

    /// Removes one mount for `id`.
    ///
    /// Returns `true` only when the id transitions from enabled to disabled.
    /// Extra removes are ignored and return `false`.
    pub fn remove(&self, id: i32) -> bool {
        let Some(entry) = self.entry(id) else {
            return false;
        };

        let mut current = entry.load(Ordering::Relaxed);
        loop {
            if current == 0 {
                return false;
            }

            match entry.compare_exchange_weak(
                current,
                current - 1,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                Ok(_) => {
                    if current == 1 {
                        self.active_ids.fetch_sub(1, Ordering::Relaxed);
                        return true;
                    }
                    return false;
                }
                Err(actual) => current = actual,
            }
        }
    }

    fn entry(&self, id: i32) -> Option<&AtomicU32> {
        let id = usize::try_from(id).ok()?;
        self.counts.get(id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ref_counts_interest_without_duplicate_active_ids() {
        let table = InterestTable::new(8);

        assert!(!table.has_any());
        assert!(table.add(3));
        assert!(!table.add(3));
        assert!(table.has(3));
        assert_eq!(table.active_ids(), 1);

        assert!(!table.remove(3));
        assert!(table.has(3));
        assert_eq!(table.active_ids(), 1);

        assert!(table.remove(3));
        assert!(!table.has(3));
        assert_eq!(table.active_ids(), 0);
        assert!(!table.remove(3));
    }

    #[test]
    fn invalid_ids_are_noops() {
        let table = InterestTable::new(2);
        assert!(!table.add(-1));
        assert!(!table.add(2));
        assert!(!table.has(-1));
        assert!(!table.has(2));
        assert!(!table.remove(-1));
        assert!(!table.remove(2));
        assert_eq!(table.active_ids(), 0);
    }
}
