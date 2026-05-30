use dwrt_core::net::NetRoute;
use dwrt_core::usercmd::UsercmdRoute;
use dwrt_ffi::{DWRT_ABI_VERSION, NetMessageDirection};

use crate::*;

#[test]
fn jsonl_writer_matches_golden_no_interest_trace() {
    let mut writer = JsonlTraceWriter::new(Vec::new());
    writer.write_runtime_loaded(DWRT_ABI_VERSION).unwrap();
    writer
        .write_event(TraceEvent::NetRoute(NetRouteTraceEvent::from_core_route(
            NetMessageDirection::Outgoing,
            1,
            72,
            0,
            None,
            NetRoute::NoInterest,
        )))
        .unwrap();
    writer
        .write_event(TraceEvent::UsercmdRoute(
            UsercmdRouteTraceEvent::from_core_route(1, 3, 0, 15, UsercmdRoute::CountOnly),
        ))
        .unwrap();

    let output = String::from_utf8(writer.into_inner()).unwrap();
    assert_eq!(output, include_str!("../tests/fixtures/no_interest.jsonl"));
}

#[test]
fn schema_header_is_versioned() {
    let mut writer = JsonlTraceWriter::new(Vec::new());
    assert_eq!(writer.write_schema_header().unwrap(), 0);
    assert_eq!(writer.next_sequence(), 1);

    let output = String::from_utf8(writer.into_inner()).unwrap();
    assert_eq!(
        output,
        format!("{{\"seq\":0,\"type\":\"schema\",\"schema_version\":{TRACE_SCHEMA_VERSION}}}\n")
    );
}

#[test]
fn hook_and_subscription_strings_are_escaped() {
    let record = TraceRecord::new(
        7,
        TraceEvent::HookStatus(HookStatusTraceEvent::new(
            "net.\"outgoing\"",
            HookTraceStatus::Disabled,
            HookTraceMode::Shadow,
            Some("missing\nfact".to_string()),
        )),
    );

    assert_eq!(
        record.to_json_line(),
        "{\"seq\":7,\"type\":\"hook_status\",\"hook\":\"net.\\\"outgoing\\\"\",\"status\":\"disabled\",\"mode\":\"shadow\",\"reason\":\"missing\\nfact\"}"
    );

    let record = TraceRecord::new(
        8,
        TraceEvent::Subscription(SubscriptionTraceEvent::new(
            "plugin\\demo",
            "net",
            "visit_user_message",
            SubscriptionAction::Added,
        )),
    );
    assert_eq!(
        record.to_json_line(),
        "{\"seq\":8,\"type\":\"subscription\",\"plugin\":\"plugin\\\\demo\",\"surface\":\"net\",\"name\":\"visit_user_message\",\"action\":\"added\"}"
    );
}

#[test]
fn ring_buffer_drops_oldest_records_with_counter() {
    let mut ring = TraceRingBuffer::new(2);
    ring.push(runtime_record(0));
    ring.push(runtime_record(1));
    ring.push(runtime_record(2));

    assert_eq!(ring.capacity(), 2);
    assert_eq!(ring.len(), 2);
    assert_eq!(ring.dropped_oldest(), 1);
    assert_eq!(
        ring.records()
            .map(|record| record.sequence)
            .collect::<Vec<_>>(),
        vec![1, 2]
    );

    let drained = ring.drain();
    assert_eq!(drained.len(), 2);
    assert!(ring.is_empty());
    assert_eq!(ring.dropped_oldest(), 1);
}

#[test]
fn zero_capacity_ring_counts_all_drops() {
    let mut ring = TraceRingBuffer::new(0);
    ring.push(runtime_record(0));
    ring.push(runtime_record(1));

    assert!(ring.is_empty());
    assert_eq!(ring.dropped_oldest(), 2);
}

#[test]
fn route_comparison_reports_mismatch_missing_and_extra() {
    let expected = vec![
        TraceRecord::new(
            0,
            TraceEvent::NetRoute(NetRouteTraceEvent::from_core_route(
                NetMessageDirection::Outgoing,
                1,
                72,
                0,
                Some(314),
                NetRoute::FastOnly,
            )),
        ),
        TraceRecord::new(
            1,
            TraceEvent::UsercmdRoute(UsercmdRouteTraceEvent::from_core_route(
                1,
                3,
                2,
                15,
                UsercmdRoute::FastRead,
            )),
        ),
    ];
    let actual = vec![
        TraceRecord::new(
            0,
            TraceEvent::NetRoute(NetRouteTraceEvent::from_core_route(
                NetMessageDirection::Outgoing,
                1,
                72,
                0,
                Some(314),
                NetRoute::SerializedOnly,
            )),
        ),
        TraceRecord::new(
            2,
            TraceEvent::NetRoute(NetRouteTraceEvent::from_core_route(
                NetMessageDirection::Incoming,
                2,
                33,
                0,
                None,
                NetRoute::NoInterest,
            )),
        ),
    ];

    let comparison = compare_route_decisions(&expected, &actual);
    assert!(!comparison.is_match());
    assert_eq!(comparison.mismatches.len(), 2);
    assert!(comparison.missing_actual.is_empty());
    assert!(comparison.extra_actual.is_empty());
    assert_eq!(comparison.mismatches[0].index, 0);
}

#[test]
fn route_comparison_reports_length_differences() {
    let expected = vec![TraceRecord::new(
        0,
        TraceEvent::UsercmdRoute(UsercmdRouteTraceEvent::from_core_route(
            1,
            1,
            0,
            15,
            UsercmdRoute::CountOnly,
        )),
    )];
    let actual = vec![
        expected[0].clone(),
        TraceRecord::new(
            1,
            TraceEvent::NetRoute(NetRouteTraceEvent::from_core_route(
                NetMessageDirection::Incoming,
                1,
                33,
                0,
                None,
                NetRoute::NoInterest,
            )),
        ),
    ];

    let comparison = compare_route_decisions(&expected, &actual);
    assert!(!comparison.is_match());
    assert!(comparison.mismatches.is_empty());
    assert!(comparison.missing_actual.is_empty());
    assert_eq!(comparison.extra_actual.len(), 1);
}

#[test]
fn non_route_records_are_ignored_by_route_comparison() {
    let expected = vec![runtime_record(0)];
    let actual = vec![TraceRecord::new(
        1,
        TraceEvent::Subscription(SubscriptionTraceEvent::new(
            "demo",
            "usercmd",
            "visit",
            SubscriptionAction::Added,
        )),
    )];

    assert!(compare_route_decisions(&expected, &actual).is_match());
}

fn runtime_record(sequence: u64) -> TraceRecord {
    TraceRecord::new(
        sequence,
        TraceEvent::Runtime(RuntimeTraceEvent::new(
            RuntimePhase::Loaded,
            DWRT_ABI_VERSION,
        )),
    )
}
