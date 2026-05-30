use std::fmt;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum EntityError {
    InvalidPlayerSlot(i32),
    MissingSchemaField {
        class_name: &'static str,
        field_name: &'static str,
    },
    FieldNotPublicSafe {
        class_name: &'static str,
        field_name: &'static str,
    },
    FieldNotReadable {
        class_name: &'static str,
        field_name: &'static str,
    },
    FieldNotWritable {
        class_name: &'static str,
        field_name: &'static str,
    },
    FactShapeMismatch {
        class_name: &'static str,
        field_name: &'static str,
    },
}

impl fmt::Display for EntityError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidPlayerSlot(slot) => write!(f, "invalid player slot {slot}"),
            Self::MissingSchemaField {
                class_name,
                field_name,
            } => {
                write!(f, "missing schema field {class_name}::{field_name}")
            }
            Self::FieldNotPublicSafe {
                class_name,
                field_name,
            } => {
                write!(
                    f,
                    "schema field is not public safe {class_name}::{field_name}"
                )
            }
            Self::FieldNotReadable {
                class_name,
                field_name,
            } => {
                write!(f, "schema field is not readable {class_name}::{field_name}")
            }
            Self::FieldNotWritable {
                class_name,
                field_name,
            } => {
                write!(f, "schema field is not writable {class_name}::{field_name}")
            }
            Self::FactShapeMismatch {
                class_name,
                field_name,
            } => {
                write!(f, "schema fact shape mismatch {class_name}::{field_name}")
            }
        }
    }
}

impl std::error::Error for EntityError {}
