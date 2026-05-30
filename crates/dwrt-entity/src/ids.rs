use crate::EntityError;

pub const MAX_PLAYER_SLOTS: i32 = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct PlayerSlot(u8);

impl PlayerSlot {
    pub fn new(slot: i32) -> Result<Self, EntityError> {
        if (0..MAX_PLAYER_SLOTS).contains(&slot) {
            Ok(Self(slot as u8))
        } else {
            Err(EntityError::InvalidPlayerSlot(slot))
        }
    }

    #[must_use]
    pub const fn get(self) -> i32 {
        self.0 as i32
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct EntityIndex(u32);

impl EntityIndex {
    #[must_use]
    pub const fn new(index: u32) -> Self {
        Self(index)
    }

    #[must_use]
    pub const fn get(self) -> u32 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct EntityHandle(u32);

impl EntityHandle {
    pub const INVALID_RAW: u32 = u32::MAX;

    #[must_use]
    pub const fn from_raw(raw: u32) -> Self {
        Self(raw)
    }

    #[must_use]
    pub const fn raw(self) -> u32 {
        self.0
    }

    #[must_use]
    pub const fn is_valid(self) -> bool {
        self.0 != Self::INVALID_RAW
    }
}
