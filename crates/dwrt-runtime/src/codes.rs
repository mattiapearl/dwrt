use dwrt_core::net::NetRoute;
use dwrt_core::usercmd::UsercmdRoute;

use crate::probe::ProbeRoute;

pub const fn dwrt_route_no_interest() -> u32 {
    0
}

pub const fn dwrt_route_fast_only() -> u32 {
    1
}

pub const fn dwrt_route_serialized_only() -> u32 {
    2
}

pub const fn dwrt_route_fast_and_serialized() -> u32 {
    3
}

pub const fn dwrt_usercmd_route_no_work() -> u32 {
    0
}

pub const fn dwrt_usercmd_route_count_only() -> u32 {
    1
}

pub const fn dwrt_usercmd_route_fast_read() -> u32 {
    2
}

pub const fn dwrt_usercmd_route_full_protobuf() -> u32 {
    3
}

pub const fn dwrt_usercmd_route_fast_and_full() -> u32 {
    4
}

pub const fn dwrt_probe_route_no_interest() -> u32 {
    0
}

pub const fn dwrt_probe_route_counted() -> u32 {
    1
}

pub(crate) fn route_code(route: NetRoute) -> u32 {
    match route {
        NetRoute::NoInterest => dwrt_route_no_interest(),
        NetRoute::FastOnly => dwrt_route_fast_only(),
        NetRoute::SerializedOnly => dwrt_route_serialized_only(),
        NetRoute::FastAndSerialized => dwrt_route_fast_and_serialized(),
    }
}

pub(crate) fn usercmd_route_code(route: UsercmdRoute) -> u32 {
    match route {
        UsercmdRoute::NoWork => dwrt_usercmd_route_no_work(),
        UsercmdRoute::CountOnly => dwrt_usercmd_route_count_only(),
        UsercmdRoute::FastRead => dwrt_usercmd_route_fast_read(),
        UsercmdRoute::FullProtobuf => dwrt_usercmd_route_full_protobuf(),
        UsercmdRoute::FastAndFull => dwrt_usercmd_route_fast_and_full(),
    }
}

pub(crate) fn probe_route_code(route: ProbeRoute) -> u32 {
    match route {
        ProbeRoute::NoInterest => dwrt_probe_route_no_interest(),
        ProbeRoute::Counted => dwrt_probe_route_counted(),
    }
}
