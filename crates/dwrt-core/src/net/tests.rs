use crate::net::*;
use dwrt_ffi::{FastNetMessageNative, NetMessageDirection};

#[test]
fn no_interest_short_circuits() {
    let net = NetInterest::with_capacity(128);
    assert!(!net.has_any());
    assert_eq!(
        net.route_message(NetMessageDirection::Outgoing, 43, None),
        NetRoute::NoInterest
    );
}

#[test]
fn message_interest_routes_by_direction_and_kind() {
    let net = NetInterest::with_capacity(128);
    assert!(net.add_fast(NetMessageDirection::Outgoing, 43));
    assert_eq!(
        net.route_message(NetMessageDirection::Outgoing, 43, None),
        NetRoute::FastOnly
    );
    assert_eq!(
        net.route_message(NetMessageDirection::Incoming, 43, None),
        NetRoute::NoInterest
    );

    assert!(net.add_serialized(NetMessageDirection::Outgoing, 43));
    assert_eq!(
        net.route_message(NetMessageDirection::Outgoing, 43, None),
        NetRoute::FastAndSerialized
    );

    assert!(net.remove_fast(NetMessageDirection::Outgoing, 43));
    assert_eq!(
        net.route_message(NetMessageDirection::Outgoing, 43, None),
        NetRoute::SerializedOnly
    );
}

#[test]
fn duplicate_mounts_do_not_disable_until_last_remove() {
    let net = NetInterest::with_capacity(128);
    assert!(net.add_serialized(NetMessageDirection::Incoming, 33));
    assert!(!net.add_serialized(NetMessageDirection::Incoming, 33));

    assert!(!net.remove_serialized(NetMessageDirection::Incoming, 33));
    assert_eq!(
        net.route_message(NetMessageDirection::Incoming, 33, None),
        NetRoute::SerializedOnly
    );

    assert!(net.remove_serialized(NetMessageDirection::Incoming, 33));
    assert_eq!(
        net.route_message(NetMessageDirection::Incoming, 33, None),
        NetRoute::NoInterest
    );
}

#[test]
fn nested_user_message_interest_routes_without_envelope_interest() {
    let net = NetInterest::with_capacity(512);
    assert!(net.add_user_fast(314));
    assert_eq!(
        net.route_message(NetMessageDirection::Outgoing, 72, Some(314)),
        NetRoute::FastOnly
    );
    assert_eq!(
        net.route_message(NetMessageDirection::Outgoing, 72, Some(315)),
        NetRoute::NoInterest
    );
}

#[test]
fn ffi_event_routes_nested_user_message() {
    let net = NetInterest::with_capacity(512);
    net.add_user_serialized(314);
    let event = FastNetMessageNative {
        direction: NetMessageDirection::Outgoing as i32,
        msg_id: 72,
        user_message_type: 314,
        has_user_message_type: 1,
        ..FastNetMessageNative::default()
    };
    assert_eq!(net.route_fast_event(&event), NetRoute::SerializedOnly);
}
