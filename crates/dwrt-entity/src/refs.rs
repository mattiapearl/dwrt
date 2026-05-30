use crate::{
    CCitadelPlayerController, CCitadelPlayerPawn, EngineRef, EntityHandle, EntityIndex, PlayerSlot,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlayerControllerRef {
    ptr: EngineRef<CCitadelPlayerController>,
    slot: PlayerSlot,
    handle: Option<EntityHandle>,
}

impl PlayerControllerRef {
    #[must_use]
    pub const fn new(
        ptr: EngineRef<CCitadelPlayerController>,
        slot: PlayerSlot,
        handle: Option<EntityHandle>,
    ) -> Self {
        Self { ptr, slot, handle }
    }

    #[must_use]
    pub const fn ptr(self) -> EngineRef<CCitadelPlayerController> {
        self.ptr
    }

    #[must_use]
    pub const fn slot(self) -> PlayerSlot {
        self.slot
    }

    #[must_use]
    pub const fn handle(self) -> Option<EntityHandle> {
        self.handle
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlayerPawnRef {
    ptr: EngineRef<CCitadelPlayerPawn>,
    entity_index: EntityIndex,
    handle: Option<EntityHandle>,
    owner_slot: Option<PlayerSlot>,
}

impl PlayerPawnRef {
    #[must_use]
    pub const fn new(
        ptr: EngineRef<CCitadelPlayerPawn>,
        entity_index: EntityIndex,
        handle: Option<EntityHandle>,
        owner_slot: Option<PlayerSlot>,
    ) -> Self {
        Self {
            ptr,
            entity_index,
            handle,
            owner_slot,
        }
    }

    #[must_use]
    pub const fn ptr(self) -> EngineRef<CCitadelPlayerPawn> {
        self.ptr
    }

    #[must_use]
    pub const fn entity_index(self) -> EntityIndex {
        self.entity_index
    }

    #[must_use]
    pub const fn handle(self) -> Option<EntityHandle> {
        self.handle
    }

    #[must_use]
    pub const fn owner_slot(self) -> Option<PlayerSlot> {
        self.owner_slot
    }
}
