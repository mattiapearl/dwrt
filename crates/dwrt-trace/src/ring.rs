use std::collections::VecDeque;

use crate::TraceRecord;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TraceRingBuffer {
    capacity: usize,
    records: VecDeque<TraceRecord>,
    dropped_oldest: u64,
}

impl TraceRingBuffer {
    #[must_use]
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity,
            records: VecDeque::with_capacity(capacity),
            dropped_oldest: 0,
        }
    }

    pub fn push(&mut self, record: TraceRecord) {
        if self.capacity == 0 {
            self.dropped_oldest += 1;
            return;
        }

        if self.records.len() == self.capacity {
            self.records.pop_front();
            self.dropped_oldest += 1;
        }
        self.records.push_back(record);
    }

    #[must_use]
    pub const fn capacity(&self) -> usize {
        self.capacity
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.records.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.records.is_empty()
    }

    #[must_use]
    pub const fn dropped_oldest(&self) -> u64 {
        self.dropped_oldest
    }

    pub fn records(&self) -> impl Iterator<Item = &TraceRecord> {
        self.records.iter()
    }

    #[must_use]
    pub fn drain(&mut self) -> Vec<TraceRecord> {
        self.records.drain(..).collect()
    }
}
