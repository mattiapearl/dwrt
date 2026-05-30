use std::fmt::Write as _;
use std::io::{self, Write};

use dwrt_core::net::NetRoute;
use dwrt_core::usercmd::UsercmdRoute;
use dwrt_ffi::NetMessageDirection;

use crate::json::{
    push_json_optional_i32_field, push_json_optional_string_field, push_json_string,
    push_json_string_field,
};

pub const TRACE_SCHEMA_VERSION: u32 = 1;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TraceRecord {
    pub sequence: u64,
    pub event: TraceEvent,
}

impl TraceRecord {
    #[must_use]
    pub fn new(sequence: u64, event: TraceEvent) -> Self {
        Self { sequence, event }
    }

    #[must_use]
    pub fn to_json_line(&self) -> String {
        let mut out = String::new();
        out.push('{');
        write!(out, "\"seq\":{}", self.sequence).expect("writing to a String cannot fail");
        out.push(',');
        self.event.push_json_fields(&mut out);
        out.push('}');
        out
    }

    pub fn write_json_line(&self, writer: &mut impl Write) -> io::Result<()> {
        writer.write_all(self.to_json_line().as_bytes())?;
        writer.write_all(b"\n")
    }

    #[must_use]
    pub fn route_decision(&self) -> Option<RouteDecision> {
        match &self.event {
            TraceEvent::NetRoute(event) => Some(RouteDecision {
                sequence: self.sequence,
                key: RouteKey::Net {
                    direction: event.direction,
                    endpoint_slot: event.endpoint_slot,
                    msg_id: event.msg_id,
                    recipient_mask: event.recipient_mask,
                    user_message_type: event.user_message_type,
                },
                outcome: event.route.into(),
            }),
            TraceEvent::UsercmdRoute(event) => Some(RouteDecision {
                sequence: self.sequence,
                key: RouteKey::Usercmd {
                    slot: event.slot,
                    command_count: event.command_count,
                },
                outcome: event.route.into(),
            }),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TraceEvent {
    Runtime(RuntimeTraceEvent),
    HookStatus(HookStatusTraceEvent),
    NetRoute(NetRouteTraceEvent),
    UsercmdRoute(UsercmdRouteTraceEvent),
    Subscription(SubscriptionTraceEvent),
}

impl TraceEvent {
    fn push_json_fields(&self, out: &mut String) {
        match self {
            Self::Runtime(event) => event.push_json_fields(out),
            Self::HookStatus(event) => event.push_json_fields(out),
            Self::NetRoute(event) => event.push_json_fields(out),
            Self::UsercmdRoute(event) => event.push_json_fields(out),
            Self::Subscription(event) => event.push_json_fields(out),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RuntimePhase {
    Loaded,
    Started,
    Shutdown,
    Unloaded,
}

impl RuntimePhase {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Loaded => "loaded",
            Self::Started => "started",
            Self::Shutdown => "shutdown",
            Self::Unloaded => "unloaded",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RuntimeTraceEvent {
    pub phase: RuntimePhase,
    pub abi_version: u32,
}

impl RuntimeTraceEvent {
    #[must_use]
    pub const fn new(phase: RuntimePhase, abi_version: u32) -> Self {
        Self { phase, abi_version }
    }

    fn push_json_fields(&self, out: &mut String) {
        push_json_string_field(out, "type", "runtime");
        out.push(',');
        push_json_string_field(out, "phase", self.phase.as_str());
        out.push(',');
        write!(out, "\"abi_version\":{}", self.abi_version)
            .expect("writing to a String cannot fail");
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HookTraceMode {
    Disabled,
    Shadow,
    Active,
}

impl HookTraceMode {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Disabled => "disabled",
            Self::Shadow => "shadow",
            Self::Active => "active",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HookTraceStatus {
    Declared,
    Resolved,
    Installed,
    Disabled,
}

impl HookTraceStatus {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Declared => "declared",
            Self::Resolved => "resolved",
            Self::Installed => "installed",
            Self::Disabled => "disabled",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HookStatusTraceEvent {
    pub hook: String,
    pub status: HookTraceStatus,
    pub mode: HookTraceMode,
    pub reason: Option<String>,
}

impl HookStatusTraceEvent {
    #[must_use]
    pub fn new(
        hook: impl Into<String>,
        status: HookTraceStatus,
        mode: HookTraceMode,
        reason: Option<String>,
    ) -> Self {
        Self {
            hook: hook.into(),
            status,
            mode,
            reason,
        }
    }

    fn push_json_fields(&self, out: &mut String) {
        push_json_string_field(out, "type", "hook_status");
        out.push(',');
        push_json_string_field(out, "hook", &self.hook);
        out.push(',');
        push_json_string_field(out, "status", self.status.as_str());
        out.push(',');
        push_json_string_field(out, "mode", self.mode.as_str());
        out.push(',');
        push_json_optional_string_field(out, "reason", self.reason.as_deref());
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NetTraceDirection {
    Incoming,
    Outgoing,
}

impl NetTraceDirection {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Incoming => "incoming",
            Self::Outgoing => "outgoing",
        }
    }
}

impl From<NetMessageDirection> for NetTraceDirection {
    fn from(value: NetMessageDirection) -> Self {
        match value {
            NetMessageDirection::Incoming => Self::Incoming,
            NetMessageDirection::Outgoing => Self::Outgoing,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NetRouteKind {
    NoInterest,
    FastOnly,
    SerializedOnly,
    FastAndSerialized,
}

impl NetRouteKind {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::NoInterest => "no_interest",
            Self::FastOnly => "fast_only",
            Self::SerializedOnly => "serialized_only",
            Self::FastAndSerialized => "fast_and_serialized",
        }
    }
}

impl From<NetRoute> for NetRouteKind {
    fn from(value: NetRoute) -> Self {
        match value {
            NetRoute::NoInterest => Self::NoInterest,
            NetRoute::FastOnly => Self::FastOnly,
            NetRoute::SerializedOnly => Self::SerializedOnly,
            NetRoute::FastAndSerialized => Self::FastAndSerialized,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NetRouteTraceEvent {
    pub direction: NetTraceDirection,
    pub endpoint_slot: i32,
    pub msg_id: i32,
    pub recipient_mask: u64,
    pub user_message_type: Option<i32>,
    pub route: NetRouteKind,
}

impl NetRouteTraceEvent {
    #[must_use]
    pub fn from_core_route(
        direction: NetMessageDirection,
        endpoint_slot: i32,
        msg_id: i32,
        recipient_mask: u64,
        user_message_type: Option<i32>,
        route: NetRoute,
    ) -> Self {
        Self {
            direction: direction.into(),
            endpoint_slot,
            msg_id,
            recipient_mask,
            user_message_type,
            route: route.into(),
        }
    }

    fn push_json_fields(&self, out: &mut String) {
        push_json_string_field(out, "type", "net_route");
        out.push(',');
        push_json_string_field(out, "direction", self.direction.as_str());
        out.push(',');
        write!(
            out,
            "\"endpoint_slot\":{},\"msg_id\":{},\"recipient_mask\":{}",
            self.endpoint_slot, self.msg_id, self.recipient_mask
        )
        .expect("writing to a String cannot fail");
        out.push(',');
        push_json_optional_i32_field(out, "user_message_type", self.user_message_type);
        out.push(',');
        push_json_string_field(out, "route", self.route.as_str());
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UsercmdRouteKind {
    NoWork,
    CountOnly,
    FastRead,
    FullProtobuf,
    FastAndFull,
}

impl UsercmdRouteKind {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::NoWork => "no_work",
            Self::CountOnly => "count_only",
            Self::FastRead => "fast_read",
            Self::FullProtobuf => "full_protobuf",
            Self::FastAndFull => "fast_and_full",
        }
    }
}

impl From<UsercmdRoute> for UsercmdRouteKind {
    fn from(value: UsercmdRoute) -> Self {
        match value {
            UsercmdRoute::NoWork => Self::NoWork,
            UsercmdRoute::CountOnly => Self::CountOnly,
            UsercmdRoute::FastRead => Self::FastRead,
            UsercmdRoute::FullProtobuf => Self::FullProtobuf,
            UsercmdRoute::FastAndFull => Self::FastAndFull,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct UsercmdRouteTraceEvent {
    pub slot: i32,
    pub command_count: u32,
    pub mount_mask: u32,
    pub field_mask: u32,
    pub route: UsercmdRouteKind,
}

impl UsercmdRouteTraceEvent {
    #[must_use]
    pub fn from_core_route(
        slot: i32,
        command_count: u32,
        mount_mask: u32,
        field_mask: u32,
        route: UsercmdRoute,
    ) -> Self {
        Self {
            slot,
            command_count,
            mount_mask,
            field_mask,
            route: route.into(),
        }
    }

    fn push_json_fields(&self, out: &mut String) {
        push_json_string_field(out, "type", "usercmd_route");
        out.push(',');
        write!(
            out,
            "\"slot\":{},\"command_count\":{},\"mount_mask\":{},\"field_mask\":{}",
            self.slot, self.command_count, self.mount_mask, self.field_mask
        )
        .expect("writing to a String cannot fail");
        out.push(',');
        push_json_string_field(out, "route", self.route.as_str());
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SubscriptionAction {
    Added,
    Removed,
}

impl SubscriptionAction {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Added => "added",
            Self::Removed => "removed",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SubscriptionTraceEvent {
    pub plugin: String,
    pub surface: String,
    pub name: String,
    pub action: SubscriptionAction,
}

impl SubscriptionTraceEvent {
    #[must_use]
    pub fn new(
        plugin: impl Into<String>,
        surface: impl Into<String>,
        name: impl Into<String>,
        action: SubscriptionAction,
    ) -> Self {
        Self {
            plugin: plugin.into(),
            surface: surface.into(),
            name: name.into(),
            action,
        }
    }

    fn push_json_fields(&self, out: &mut String) {
        push_json_string_field(out, "type", "subscription");
        out.push(',');
        push_json_string_field(out, "plugin", &self.plugin);
        out.push(',');
        push_json_string_field(out, "surface", &self.surface);
        out.push(',');
        push_json_string_field(out, "name", &self.name);
        out.push(',');
        push_json_string_field(out, "action", self.action.as_str());
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RouteDecision {
    pub sequence: u64,
    pub key: RouteKey,
    pub outcome: RouteOutcome,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RouteKey {
    Net {
        direction: NetTraceDirection,
        endpoint_slot: i32,
        msg_id: i32,
        recipient_mask: u64,
        user_message_type: Option<i32>,
    },
    Usercmd {
        slot: i32,
        command_count: u32,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RouteOutcome {
    Net(NetRouteKind),
    Usercmd(UsercmdRouteKind),
}

impl From<NetRouteKind> for RouteOutcome {
    fn from(value: NetRouteKind) -> Self {
        Self::Net(value)
    }
}

impl From<UsercmdRouteKind> for RouteOutcome {
    fn from(value: UsercmdRouteKind) -> Self {
        Self::Usercmd(value)
    }
}

pub(crate) fn push_json_header_fields(out: &mut String) {
    push_json_string(out, "schema_version");
    out.push(':');
    write!(out, "{TRACE_SCHEMA_VERSION}").expect("writing to a String cannot fail");
}
