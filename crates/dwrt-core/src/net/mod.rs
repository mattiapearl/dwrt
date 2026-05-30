use std::sync::atomic::{AtomicU32, Ordering};

#[cfg(test)]
mod tests;

use dwrt_ffi::{DWRT_MAX_TRACKED_MESSAGE_ID, FastNetMessageNative, NetMessageDirection};

use crate::interest::InterestTable;

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum NetRoute {
    NoInterest,
    FastOnly,
    SerializedOnly,
    FastAndSerialized,
}

impl NetRoute {
    #[must_use]
    pub const fn wants_fast(self) -> bool {
        matches!(self, Self::FastOnly | Self::FastAndSerialized)
    }

    #[must_use]
    pub const fn wants_serialized(self) -> bool {
        matches!(self, Self::SerializedOnly | Self::FastAndSerialized)
    }
}

/// Interest-gated net-message routing.
///
/// This mirrors the intended runtime policy: no mounted interest means no
/// protobuf serialization, no Rust/plugin callback, and no allocation.
pub struct NetInterest {
    incoming_serialized: InterestTable,
    outgoing_serialized: InterestTable,
    incoming_fast: InterestTable,
    outgoing_fast: InterestTable,
    user_serialized: InterestTable,
    user_fast: InterestTable,
    total_active_tables: AtomicU32,
}

impl NetInterest {
    #[must_use]
    pub fn new() -> Self {
        Self::with_capacity(DWRT_MAX_TRACKED_MESSAGE_ID)
    }

    #[must_use]
    pub fn with_capacity(capacity: usize) -> Self {
        Self {
            incoming_serialized: InterestTable::new(capacity),
            outgoing_serialized: InterestTable::new(capacity),
            incoming_fast: InterestTable::new(capacity),
            outgoing_fast: InterestTable::new(capacity),
            user_serialized: InterestTable::new(capacity),
            user_fast: InterestTable::new(capacity),
            total_active_tables: AtomicU32::new(0),
        }
    }

    #[must_use]
    pub fn has_any(&self) -> bool {
        self.total_active_tables.load(Ordering::Relaxed) != 0
    }

    pub fn add_serialized(&self, direction: NetMessageDirection, msg_id: i32) -> bool {
        self.track(self.table(direction, InterestKind::Serialized).add(msg_id))
    }

    pub fn remove_serialized(&self, direction: NetMessageDirection, msg_id: i32) -> bool {
        self.untrack(
            self.table(direction, InterestKind::Serialized)
                .remove(msg_id),
        )
    }

    pub fn add_fast(&self, direction: NetMessageDirection, msg_id: i32) -> bool {
        self.track(self.table(direction, InterestKind::Fast).add(msg_id))
    }

    pub fn remove_fast(&self, direction: NetMessageDirection, msg_id: i32) -> bool {
        self.untrack(self.table(direction, InterestKind::Fast).remove(msg_id))
    }

    pub fn add_user_serialized(&self, user_msg_id: i32) -> bool {
        self.track(self.user_serialized.add(user_msg_id))
    }

    pub fn remove_user_serialized(&self, user_msg_id: i32) -> bool {
        self.untrack(self.user_serialized.remove(user_msg_id))
    }

    pub fn add_user_fast(&self, user_msg_id: i32) -> bool {
        self.track(self.user_fast.add(user_msg_id))
    }

    pub fn remove_user_fast(&self, user_msg_id: i32) -> bool {
        self.untrack(self.user_fast.remove(user_msg_id))
    }

    #[must_use]
    pub fn route_message(
        &self,
        direction: NetMessageDirection,
        msg_id: i32,
        user_message_type: Option<i32>,
    ) -> NetRoute {
        if !self.has_any() {
            return NetRoute::NoInterest;
        }

        let serialized = self.table(direction, InterestKind::Serialized).has(msg_id)
            || user_message_type.is_some_and(|id| self.user_serialized.has(id));

        let fast = self.table(direction, InterestKind::Fast).has(msg_id)
            || user_message_type.is_some_and(|id| self.user_fast.has(id));

        match (fast, serialized) {
            (false, false) => NetRoute::NoInterest,
            (true, false) => NetRoute::FastOnly,
            (false, true) => NetRoute::SerializedOnly,
            (true, true) => NetRoute::FastAndSerialized,
        }
    }

    #[must_use]
    pub fn route_fast_event(&self, event: &FastNetMessageNative) -> NetRoute {
        let Some(direction) = NetMessageDirection::from_i32(event.direction) else {
            return NetRoute::NoInterest;
        };
        let user_message_type =
            (event.has_user_message_type != 0).then_some(event.user_message_type);
        self.route_message(direction, event.msg_id, user_message_type)
    }

    fn table(&self, direction: NetMessageDirection, kind: InterestKind) -> &InterestTable {
        match (direction, kind) {
            (NetMessageDirection::Incoming, InterestKind::Serialized) => &self.incoming_serialized,
            (NetMessageDirection::Outgoing, InterestKind::Serialized) => &self.outgoing_serialized,
            (NetMessageDirection::Incoming, InterestKind::Fast) => &self.incoming_fast,
            (NetMessageDirection::Outgoing, InterestKind::Fast) => &self.outgoing_fast,
        }
    }

    fn track(&self, transitioned: bool) -> bool {
        if transitioned {
            self.total_active_tables.fetch_add(1, Ordering::Relaxed);
        }
        transitioned
    }

    fn untrack(&self, transitioned: bool) -> bool {
        if transitioned {
            self.total_active_tables.fetch_sub(1, Ordering::Relaxed);
        }
        transitioned
    }
}

impl Default for NetInterest {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Clone, Copy)]
enum InterestKind {
    Serialized,
    Fast,
}
