use std::fmt;

use crate::HookName;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum HookRegistryError {
    InvalidHookName(String),
    DuplicateHook(HookName),
    MissingHook(HookName),
    VisitorRequestsWriteAccess(HookName),
    ReplaceAccessForbidden(HookName),
    CannotInstallDisabledHook(HookName),
    CannotInstallUnresolvedHook(HookName),
}

impl fmt::Display for HookRegistryError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidHookName(name) => write!(f, "invalid hook name: {name}"),
            Self::DuplicateHook(name) => write!(f, "duplicate hook: {name}"),
            Self::MissingHook(name) => write!(f, "missing hook: {name}"),
            Self::VisitorRequestsWriteAccess(name) => {
                write!(f, "visitor hook requests write/block/call access: {name}")
            }
            Self::ReplaceAccessForbidden(name) => {
                write!(f, "replace access is forbidden for hook: {name}")
            }
            Self::CannotInstallDisabledHook(name) => {
                write!(f, "cannot install disabled hook: {name}")
            }
            Self::CannotInstallUnresolvedHook(name) => {
                write!(f, "cannot install unresolved hook: {name}")
            }
        }
    }
}

impl std::error::Error for HookRegistryError {}
