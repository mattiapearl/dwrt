use dwrt_core::usercmd::{UsercmdMount, UsercmdRoute};
use dwrt_ffi::{DWRT_ABI_VERSION, NetMessageDirection};

use crate::{DwrtRuntime, route_code, usercmd_route_code};

#[unsafe(no_mangle)]
pub extern "C" fn dwrt_abi_version() -> u32 {
    DWRT_ABI_VERSION
}

#[unsafe(no_mangle)]
pub extern "C" fn dwrt_runtime_new() -> *mut DwrtRuntime {
    Box::into_raw(Box::new(DwrtRuntime::new()))
}

/// Frees a runtime allocated by `dwrt_runtime_new`.
///
/// # Safety
///
/// `runtime` must be null or a pointer returned by `dwrt_runtime_new` that has
/// not already been freed. After this call the pointer must not be used again.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_runtime_free(runtime: *mut DwrtRuntime) {
    if !runtime.is_null() {
        drop(unsafe { Box::from_raw(runtime) });
    }
}

/// Adds serialized net-message interest.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_net_add_serialized(
    runtime: *const DwrtRuntime,
    direction: i32,
    msg_id: i32,
) -> u8 {
    with_runtime_and_direction(runtime, direction, |rt, dir| {
        rt.net().add_serialized(dir, msg_id)
    })
}

/// Removes serialized net-message interest.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_net_remove_serialized(
    runtime: *const DwrtRuntime,
    direction: i32,
    msg_id: i32,
) -> u8 {
    with_runtime_and_direction(runtime, direction, |rt, dir| {
        rt.net().remove_serialized(dir, msg_id)
    })
}

/// Adds fast net-message interest.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_net_add_fast(
    runtime: *const DwrtRuntime,
    direction: i32,
    msg_id: i32,
) -> u8 {
    with_runtime_and_direction(runtime, direction, |rt, dir| rt.net().add_fast(dir, msg_id))
}

/// Removes fast net-message interest.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_net_remove_fast(
    runtime: *const DwrtRuntime,
    direction: i32,
    msg_id: i32,
) -> u8 {
    with_runtime_and_direction(runtime, direction, |rt, dir| {
        rt.net().remove_fast(dir, msg_id)
    })
}

/// Adds fast nested user-message interest.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_net_add_user_fast(
    runtime: *const DwrtRuntime,
    user_msg_id: i32,
) -> u8 {
    with_runtime(runtime, |rt| rt.net().add_user_fast(user_msg_id))
}

/// Adds serialized nested user-message interest.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_net_add_user_serialized(
    runtime: *const DwrtRuntime,
    user_msg_id: i32,
) -> u8 {
    with_runtime(runtime, |rt| rt.net().add_user_serialized(user_msg_id))
}

/// Routes a net message. Return codes match `dwrt_route_*` constants.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_net_route(
    runtime: *const DwrtRuntime,
    direction: i32,
    msg_id: i32,
    has_user_msg_id: u8,
    user_msg_id: i32,
) -> u32 {
    let Some(direction) = NetMessageDirection::from_i32(direction) else {
        return route_code(dwrt_core::net::NetRoute::NoInterest);
    };
    let Some(runtime) = as_runtime(runtime) else {
        return route_code(dwrt_core::net::NetRoute::NoInterest);
    };
    let user_msg_id = (has_user_msg_id != 0).then_some(user_msg_id);
    route_code(runtime.net().route_message(direction, msg_id, user_msg_id))
}

/// Sets the usercmd mount mask.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_usercmd_set_mount_mask(runtime: *const DwrtRuntime, mask: u32) {
    if let Some(runtime) = as_runtime(runtime) {
        runtime
            .usercmd()
            .set_mount_mask(UsercmdMount::from_bits_truncate(mask));
    }
}

/// Routes a usercmd batch. Return codes match `dwrt_usercmd_route_*` constants.
///
/// # Safety
///
/// `runtime` must be a valid pointer returned by `dwrt_runtime_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dwrt_usercmd_route(runtime: *const DwrtRuntime) -> u32 {
    let Some(runtime) = as_runtime(runtime) else {
        return usercmd_route_code(UsercmdRoute::NoWork);
    };
    usercmd_route_code(runtime.usercmd().route())
}

fn with_runtime(runtime: *const DwrtRuntime, f: impl FnOnce(&DwrtRuntime) -> bool) -> u8 {
    as_runtime(runtime).is_some_and(f) as u8
}

fn with_runtime_and_direction(
    runtime: *const DwrtRuntime,
    direction: i32,
    f: impl FnOnce(&DwrtRuntime, NetMessageDirection) -> bool,
) -> u8 {
    let Some(direction) = NetMessageDirection::from_i32(direction) else {
        return 0;
    };
    as_runtime(runtime).is_some_and(|rt| f(rt, direction)) as u8
}

fn as_runtime<'a>(runtime: *const DwrtRuntime) -> Option<&'a DwrtRuntime> {
    if runtime.is_null() {
        None
    } else {
        Some(unsafe { &*runtime })
    }
}
