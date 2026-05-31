use dwrt_core::usercmd::UsercmdMount;
use dwrt_ffi::{
    DWRT_ABI_VERSION, DWRT_PROBE_MOUNT_DAMAGE, DWRT_PROBE_MOUNT_ENTITY_INPUT,
    DwrtProbeCountersNative, FastDamageNative, FastEntityIoNative, NetMessageDirection,
};

use crate::exports;
use crate::*;

#[test]
fn abi_version_is_exposed() {
    assert_eq!(exports::dwrt_abi_version(), DWRT_ABI_VERSION);
}

#[test]
fn c_abi_runtime_routes_net_interest() {
    let runtime = exports::dwrt_runtime_new();
    assert!(!runtime.is_null());

    unsafe {
        assert_eq!(
            exports::dwrt_net_route(runtime, NetMessageDirection::Outgoing as i32, 72, 1, 314),
            dwrt_route_no_interest()
        );
        assert_eq!(exports::dwrt_net_add_user_fast(runtime, 314), 1);
        assert_eq!(
            exports::dwrt_net_route(runtime, NetMessageDirection::Outgoing as i32, 72, 1, 314),
            dwrt_route_fast_only()
        );
        exports::dwrt_runtime_free(runtime);
    }
}

#[test]
fn c_abi_runtime_routes_usercmd_mounts() {
    let runtime = exports::dwrt_runtime_new();
    unsafe {
        assert_eq!(
            exports::dwrt_usercmd_route(runtime),
            dwrt_usercmd_route_count_only()
        );
        exports::dwrt_usercmd_set_mount_mask(runtime, UsercmdMount::FAST_READ.bits());
        assert_eq!(
            exports::dwrt_usercmd_route(runtime),
            dwrt_usercmd_route_fast_read()
        );
        exports::dwrt_runtime_free(runtime);
    }
}

#[test]
fn c_abi_runtime_counts_native_probes() {
    let runtime = exports::dwrt_runtime_new();
    unsafe {
        let damage = FastDamageNative::default();
        let input = FastEntityIoNative::default();
        assert_eq!(
            exports::dwrt_probe_record_damage(runtime, &damage),
            dwrt_probe_route_no_interest()
        );

        exports::dwrt_probe_set_mount_mask(
            runtime,
            DWRT_PROBE_MOUNT_DAMAGE | DWRT_PROBE_MOUNT_ENTITY_INPUT,
        );
        assert_eq!(
            exports::dwrt_probe_record_damage(runtime, &damage),
            dwrt_probe_route_counted()
        );
        assert_eq!(
            exports::dwrt_probe_record_entity_input(runtime, &input),
            dwrt_probe_route_counted()
        );
        assert_eq!(
            exports::dwrt_probe_record_entity_output(runtime, &input),
            dwrt_probe_route_no_interest()
        );

        let mut snapshot = DwrtProbeCountersNative::default();
        assert_eq!(exports::dwrt_probe_snapshot(runtime, &mut snapshot), 1);
        assert_eq!(
            snapshot.mount_mask,
            DWRT_PROBE_MOUNT_DAMAGE | DWRT_PROBE_MOUNT_ENTITY_INPUT
        );
        assert_eq!(snapshot.damage_seen, 1);
        assert_eq!(snapshot.damage_counted, 1);
        assert_eq!(snapshot.entity_input_seen, 1);
        assert_eq!(snapshot.entity_input_counted, 1);
        assert_eq!(snapshot.entity_output_seen, 0);
        assert_eq!(snapshot.entity_output_counted, 0);

        exports::dwrt_probe_reset_counters(runtime);
        assert_eq!(exports::dwrt_probe_snapshot(runtime, &mut snapshot), 1);
        assert_eq!(
            snapshot.mount_mask,
            DWRT_PROBE_MOUNT_DAMAGE | DWRT_PROBE_MOUNT_ENTITY_INPUT
        );
        assert_eq!(snapshot.damage_seen, 0);
        assert_eq!(snapshot.entity_input_seen, 0);
        exports::dwrt_runtime_free(runtime);
    }
}

#[test]
fn null_runtime_is_safe_no_interest() {
    unsafe {
        assert_eq!(
            exports::dwrt_net_route(
                core::ptr::null(),
                NetMessageDirection::Incoming as i32,
                33,
                0,
                0
            ),
            dwrt_route_no_interest()
        );
        assert_eq!(
            exports::dwrt_usercmd_route(core::ptr::null()),
            dwrt_usercmd_route_no_work()
        );
    }
}
