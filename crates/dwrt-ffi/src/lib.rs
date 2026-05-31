#![deny(unsafe_op_in_unsafe_fn)]

//! Stable C ABI structs shared by the native hook shim and the Rust runtime.
//!
//! Keep this crate boring: no allocation, no engine pointers hidden in Rust types,
//! and no ownership transfer without an explicit function contract.

use core::ffi::{c_char, c_void};

pub const DWRT_ABI_VERSION: u32 = 1;
pub const DWRT_MAX_TRACKED_MESSAGE_ID: usize = 4096;

pub const DWRT_PROBE_MOUNT_DAMAGE: u32 = 1u32 << 0;
pub const DWRT_PROBE_MOUNT_ENTITY_INPUT: u32 = 1u32 << 1;
pub const DWRT_PROBE_MOUNT_ENTITY_OUTPUT: u32 = 1u32 << 2;
pub const DWRT_PROBE_MOUNT_ENTITY_TOUCH: u32 = 1u32 << 3;

#[repr(i32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NetMessageDirection {
    Incoming = 0,
    Outgoing = 1,
}

impl NetMessageDirection {
    #[must_use]
    pub const fn from_i32(value: i32) -> Option<Self> {
        match value {
            0 => Some(Self::Incoming),
            1 => Some(Self::Outgoing),
            _ => None,
        }
    }
}

/// Compact native-extracted usercmd fields.
///
/// Layout intentionally matches Deadworks' `FastUsercmdNative`/managed `FastUsercmd`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FastUsercmdNative {
    pub command_index: i32,
    pub client_tick: i32,
    pub buttons: u64,
    pub pitch: f32,
    pub yaw: f32,
    pub roll: f32,
    pub forward_move: f32,
    pub left_move: f32,
    pub has_base: i32,
    pub has_buttons: i32,
    pub has_view_angles: i32,
}

/// Compact native-extracted net-message fields.
///
/// This is read-only metadata; full protobuf bytes are a separate, explicitly
/// mounted mutation/compatibility path.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FastNetMessageNative {
    pub direction: i32,
    pub endpoint_slot: i32,
    pub msg_id: i32,
    pub recipient_mask: u64,
    pub user_message_type: i32,
    pub has_user_message_type: u8,
    pub has_pause_request: u8,
    pub has_pause_state: u8,
    pub _pad0: u8,
    pub pause_type: i32,
    pub pause_group: i32,
    pub paused: u8,
    pub _pad1: [u8; 7],
}

/// Compact native-extracted damage event metadata.
///
/// This is intentionally handle/hash based. The native host owns engine pointer
/// lifetimes and must not pass raw entity pointers into public Rust/plugin APIs.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FastDamageNative {
    pub victim_handle: u32,
    pub attacker_handle: u32,
    pub inflictor_handle: u32,
    pub ability_handle: u32,
    pub victim_team: u8,
    pub attacker_team: u8,
    pub _pad0: [u8; 2],
    pub victim_class_hash: u32,
    pub attacker_class_hash: u32,
    pub damage: f32,
    pub damage_type: i32,
    pub damage_flags: u64,
}

/// Compact native-extracted entity I/O event metadata.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FastEntityIoNative {
    pub entity_handle: u32,
    pub activator_handle: u32,
    pub caller_handle: u32,
    pub class_hash: u32,
    pub name_hash: u32,
    pub value_hash: u32,
    pub phase: u8,
    pub _pad0: [u8; 7],
}

/// Compact native-extracted touch event metadata.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FastEntityTouchNative {
    pub entity_handle: u32,
    pub other_handle: u32,
    pub entity_class_hash: u32,
    pub other_class_hash: u32,
    pub phase: u8,
    pub _pad0: [u8; 7],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DwrtProbeCountersNative {
    pub mount_mask: u32,
    pub _pad0: u32,
    pub damage_seen: u64,
    pub damage_counted: u64,
    pub entity_input_seen: u64,
    pub entity_input_counted: u64,
    pub entity_output_seen: u64,
    pub entity_output_counted: u64,
    pub entity_touch_seen: u64,
    pub entity_touch_counted: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DwrtHostApi {
    pub abi_version: u32,
    pub log: Option<unsafe extern "C" fn(level: u32, message: *const c_char)>,
    pub find_interface: Option<unsafe extern "C" fn(name: *const c_char) -> *mut c_void>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{align_of, size_of};

    #[test]
    fn fast_usercmd_layout_matches_deadworks() {
        assert_eq!(size_of::<FastUsercmdNative>(), 48);
        assert_eq!(align_of::<FastUsercmdNative>(), 8);
    }

    #[test]
    fn fast_net_message_layout_is_stable() {
        assert_eq!(size_of::<FastNetMessageNative>(), 48);
        assert_eq!(align_of::<FastNetMessageNative>(), 8);
    }

    #[test]
    fn probe_event_layouts_are_stable() {
        assert_eq!(size_of::<FastDamageNative>(), 48);
        assert_eq!(align_of::<FastDamageNative>(), 8);
        assert_eq!(size_of::<FastEntityIoNative>(), 32);
        assert_eq!(align_of::<FastEntityIoNative>(), 4);
        assert_eq!(size_of::<FastEntityTouchNative>(), 24);
        assert_eq!(align_of::<FastEntityTouchNative>(), 4);
        assert_eq!(size_of::<DwrtProbeCountersNative>(), 72);
        assert_eq!(align_of::<DwrtProbeCountersNative>(), 8);
    }

    #[test]
    fn direction_conversion_rejects_unknown_values() {
        assert_eq!(
            NetMessageDirection::from_i32(0),
            Some(NetMessageDirection::Incoming)
        );
        assert_eq!(
            NetMessageDirection::from_i32(1),
            Some(NetMessageDirection::Outgoing)
        );
        assert_eq!(NetMessageDirection::from_i32(2), None);
        assert_eq!(NetMessageDirection::from_i32(-1), None);
    }
}
