use std::fmt::Write as _;

pub(crate) fn push_json_string(out: &mut String, value: &str) {
    out.push('"');
    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            ch if ch <= '\u{1f}' => {
                write!(out, "\\u{:04x}", ch as u32).expect("writing to a String cannot fail");
            }
            ch => out.push(ch),
        }
    }
    out.push('"');
}

pub(crate) fn push_json_string_field(out: &mut String, name: &str, value: &str) {
    push_json_string(out, name);
    out.push(':');
    push_json_string(out, value);
}

pub(crate) fn push_json_optional_i32_field(out: &mut String, name: &str, value: Option<i32>) {
    push_json_string(out, name);
    out.push(':');
    match value {
        Some(value) => write!(out, "{value}").expect("writing to a String cannot fail"),
        None => out.push_str("null"),
    }
}

pub(crate) fn push_json_optional_string_field(out: &mut String, name: &str, value: Option<&str>) {
    push_json_string(out, name);
    out.push(':');
    match value {
        Some(value) => push_json_string(out, value),
        None => out.push_str("null"),
    }
}
