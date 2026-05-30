use crate::{RouteDecision, TraceRecord};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RouteComparison {
    pub mismatches: Vec<RouteMismatch>,
    pub missing_actual: Vec<RouteDecision>,
    pub extra_actual: Vec<RouteDecision>,
}

impl RouteComparison {
    #[must_use]
    pub fn is_match(&self) -> bool {
        self.mismatches.is_empty() && self.missing_actual.is_empty() && self.extra_actual.is_empty()
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RouteMismatch {
    pub index: usize,
    pub expected: RouteDecision,
    pub actual: RouteDecision,
}

#[must_use]
pub fn compare_route_decisions(
    expected: &[TraceRecord],
    actual: &[TraceRecord],
) -> RouteComparison {
    let expected = route_decisions(expected);
    let actual = route_decisions(actual);
    compare_decisions(&expected, &actual)
}

#[must_use]
pub fn compare_decisions(expected: &[RouteDecision], actual: &[RouteDecision]) -> RouteComparison {
    let shared = expected.len().min(actual.len());
    let mismatches = expected
        .iter()
        .take(shared)
        .zip(actual.iter().take(shared))
        .enumerate()
        .filter(|(_, (expected, actual))| {
            expected.key != actual.key || expected.outcome != actual.outcome
        })
        .map(|(index, (expected, actual))| RouteMismatch {
            index,
            expected: expected.clone(),
            actual: actual.clone(),
        })
        .collect();

    RouteComparison {
        mismatches,
        missing_actual: expected.iter().skip(shared).cloned().collect(),
        extra_actual: actual.iter().skip(shared).cloned().collect(),
    }
}

#[must_use]
pub fn route_decisions(records: &[TraceRecord]) -> Vec<RouteDecision> {
    records
        .iter()
        .filter_map(TraceRecord::route_decision)
        .collect()
}
