use std::fmt;

use crate::{FactKey, FactKind};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ManifestError {
    DuplicateFact(FactKey),
    KindMismatch { key: FactKey, value_kind: FactKind },
    ReplaceAccessForbidden(FactKey),
    MissingEvidence(FactKey),
}

impl fmt::Display for ManifestError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::DuplicateFact(key) => write!(f, "duplicate memory fact: {key}"),
            Self::KindMismatch { key, value_kind } => {
                write!(
                    f,
                    "memory fact kind mismatch for {key}: value is {value_kind:?}"
                )
            }
            Self::ReplaceAccessForbidden(key) => write!(f, "replace access is forbidden for {key}"),
            Self::MissingEvidence(key) => write!(f, "memory fact has no evidence: {key}"),
        }
    }
}

impl std::error::Error for ManifestError {}
