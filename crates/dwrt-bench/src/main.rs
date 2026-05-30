#![deny(unsafe_op_in_unsafe_fn)]

use std::env;
use std::hint::black_box;
use std::time::{Duration, Instant};

use dwrt_core::net::{NetInterest, NetRoute};
use dwrt_core::usercmd::{UsercmdMount, UsercmdPolicy, UsercmdRoute};
use dwrt_ffi::NetMessageDirection;
use dwrt_trace::{RuntimePhase, RuntimeTraceEvent, TraceEvent, TraceRecord, TraceRingBuffer};

const DEFAULT_ITERATIONS: u64 = 5_000_000;
const TRACE_RING_CAPACITY: usize = 4096;

fn main() {
    let args = Args::parse(env::args().skip(1));
    let iterations = args.iterations;

    println!("# DWRT runtime microbenchmarks");
    println!();
    println!("iterations: {iterations}");
    println!();
    println!("| case | ns/op | ops/s | checksum |");
    println!("| --- | ---: | ---: | ---: |");

    let cases = [
        bench_net_no_interest(iterations),
        bench_net_fast_user_message(iterations),
        bench_net_serialized_envelope(iterations),
        bench_net_fast_and_serialized_user_message(iterations),
        bench_usercmd_count_only(iterations),
        bench_usercmd_fast_read(iterations),
        bench_usercmd_fast_and_full(iterations),
        bench_trace_ring_push(iterations),
    ];

    for result in cases {
        println!(
            "| {} | {:.3} | {:.0} | {} |",
            result.name,
            result.ns_per_op(),
            result.ops_per_sec(),
            result.checksum
        );
    }
}

#[derive(Clone, Debug)]
struct Args {
    iterations: u64,
}

impl Args {
    fn parse(mut args: impl Iterator<Item = String>) -> Self {
        let mut iterations = DEFAULT_ITERATIONS;
        while let Some(arg) = args.next() {
            match arg.as_str() {
                "--iterations" | "-n" => {
                    let Some(value) = args.next() else {
                        panic!("missing value for {arg}");
                    };
                    iterations = value
                        .parse::<u64>()
                        .unwrap_or_else(|_| panic!("invalid iteration count: {value}"));
                }
                "--help" | "-h" => {
                    print_help_and_exit();
                }
                _ => panic!("unknown argument: {arg}"),
            }
        }
        assert!(iterations > 0, "iterations must be greater than zero");
        Self { iterations }
    }
}

fn print_help_and_exit() -> ! {
    println!("dwrt-bench [--iterations N]");
    std::process::exit(0);
}

#[derive(Clone, Debug)]
struct BenchResult {
    name: &'static str,
    elapsed: Duration,
    iterations: u64,
    checksum: u64,
}

impl BenchResult {
    fn ns_per_op(&self) -> f64 {
        self.elapsed.as_secs_f64() * 1_000_000_000.0 / self.iterations as f64
    }

    fn ops_per_sec(&self) -> f64 {
        self.iterations as f64 / self.elapsed.as_secs_f64()
    }
}

fn bench_net_no_interest(iterations: u64) -> BenchResult {
    let interest = NetInterest::new();
    bench("net.no_interest", iterations, || {
        route_checksum(interest.route_message(NetMessageDirection::Outgoing, 72, None))
    })
}

fn bench_net_fast_user_message(iterations: u64) -> BenchResult {
    let interest = NetInterest::new();
    interest.add_user_fast(314);
    bench("net.fast_user_message", iterations, || {
        route_checksum(interest.route_message(NetMessageDirection::Outgoing, 72, Some(314)))
    })
}

fn bench_net_serialized_envelope(iterations: u64) -> BenchResult {
    let interest = NetInterest::new();
    interest.add_serialized(NetMessageDirection::Outgoing, 72);
    bench("net.serialized_envelope", iterations, || {
        route_checksum(interest.route_message(NetMessageDirection::Outgoing, 72, None))
    })
}

fn bench_net_fast_and_serialized_user_message(iterations: u64) -> BenchResult {
    let interest = NetInterest::new();
    interest.add_user_fast(314);
    interest.add_serialized(NetMessageDirection::Outgoing, 72);
    bench("net.fast_and_serialized_user_message", iterations, || {
        route_checksum(interest.route_message(NetMessageDirection::Outgoing, 72, Some(314)))
    })
}

fn bench_usercmd_count_only(iterations: u64) -> BenchResult {
    let policy = UsercmdPolicy::new();
    bench("usercmd.count_only", iterations, || {
        usercmd_route_checksum(policy.route())
    })
}

fn bench_usercmd_fast_read(iterations: u64) -> BenchResult {
    let policy = UsercmdPolicy::new();
    policy.mount(UsercmdMount::FAST_READ);
    bench("usercmd.fast_read", iterations, || {
        usercmd_route_checksum(policy.route())
    })
}

fn bench_usercmd_fast_and_full(iterations: u64) -> BenchResult {
    let policy = UsercmdPolicy::new();
    policy.mount(UsercmdMount::FAST_READ.union(UsercmdMount::FULL_PROTOBUF));
    bench("usercmd.fast_and_full", iterations, || {
        usercmd_route_checksum(policy.route())
    })
}

fn bench_trace_ring_push(iterations: u64) -> BenchResult {
    let mut ring = TraceRingBuffer::new(TRACE_RING_CAPACITY);
    let mut sequence = 0;
    bench("trace.ring_push", iterations, || {
        let record = TraceRecord::new(
            sequence,
            TraceEvent::Runtime(RuntimeTraceEvent::new(RuntimePhase::Loaded, 1)),
        );
        sequence = sequence.wrapping_add(1);
        ring.push(record);
        ring.dropped_oldest() ^ sequence
    })
}

fn bench(name: &'static str, iterations: u64, mut f: impl FnMut() -> u64) -> BenchResult {
    let mut checksum = 0_u64;
    for _ in 0..1024 {
        checksum ^= black_box(f());
    }

    let start = Instant::now();
    for _ in 0..iterations {
        checksum = checksum.wrapping_add(black_box(f()));
    }
    let elapsed = start.elapsed();

    BenchResult {
        name,
        elapsed,
        iterations,
        checksum: black_box(checksum),
    }
}

const fn route_checksum(route: NetRoute) -> u64 {
    match route {
        NetRoute::NoInterest => 0,
        NetRoute::FastOnly => 1,
        NetRoute::SerializedOnly => 2,
        NetRoute::FastAndSerialized => 3,
    }
}

const fn usercmd_route_checksum(route: UsercmdRoute) -> u64 {
    match route {
        UsercmdRoute::NoWork => 0,
        UsercmdRoute::CountOnly => 1,
        UsercmdRoute::FastRead => 2,
        UsercmdRoute::FullProtobuf => 3,
        UsercmdRoute::FastAndFull => 4,
    }
}
