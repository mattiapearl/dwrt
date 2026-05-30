use std::io::{self, Write};

use crate::{RuntimePhase, RuntimeTraceEvent, TraceEvent, TraceRecord, push_json_header_fields};

pub struct JsonlTraceWriter<W> {
    writer: W,
    next_sequence: u64,
}

impl<W: Write> JsonlTraceWriter<W> {
    #[must_use]
    pub const fn new(writer: W) -> Self {
        Self {
            writer,
            next_sequence: 0,
        }
    }

    #[must_use]
    pub const fn next_sequence(&self) -> u64 {
        self.next_sequence
    }

    pub fn write_schema_header(&mut self) -> io::Result<u64> {
        let sequence = self.next_sequence;
        self.next_sequence += 1;

        let mut line = String::new();
        line.push('{');
        line.push_str("\"seq\":");
        line.push_str(&sequence.to_string());
        line.push(',');
        line.push_str("\"type\":\"schema\",");
        push_json_header_fields(&mut line);
        line.push('}');
        self.writer.write_all(line.as_bytes())?;
        self.writer.write_all(b"\n")?;
        Ok(sequence)
    }

    pub fn write_runtime_loaded(&mut self, abi_version: u32) -> io::Result<u64> {
        self.write_event(TraceEvent::Runtime(RuntimeTraceEvent::new(
            RuntimePhase::Loaded,
            abi_version,
        )))
    }

    pub fn write_event(&mut self, event: TraceEvent) -> io::Result<u64> {
        let sequence = self.next_sequence;
        self.next_sequence += 1;
        TraceRecord::new(sequence, event).write_json_line(&mut self.writer)?;
        Ok(sequence)
    }

    pub fn write_record(&mut self, record: &TraceRecord) -> io::Result<()> {
        record.write_json_line(&mut self.writer)
    }

    pub fn flush(&mut self) -> io::Result<()> {
        self.writer.flush()
    }

    pub fn into_inner(self) -> W {
        self.writer
    }
}
