from __future__ import annotations

import csv
import json
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Iterable, Sequence

from perf_core import LatencySample, ParseWarning, PerformanceSpan


def _json_default(value: object) -> object:
    if isinstance(value, datetime):
        return value.isoformat(timespec="microseconds")
    raise TypeError(f"Unsupported type for JSON serialization: {type(value)!r}")


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, default=_json_default), encoding="utf-8")


def read_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def write_csv(path: Path, rows: Iterable[dict[str, object]]) -> None:
    materialized = list(rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(materialized[0].keys()) if materialized else [])
        if materialized:
            writer.writeheader()
            writer.writerows(materialized)


def span_to_record(span: PerformanceSpan) -> dict[str, object]:
    return {
        "source": span.source,
        "context_parts": list(span.context_parts),
        "context_text": span.context_text,
        "thread_id": span.thread_id,
        "start_time": span.start_time.isoformat(timespec="microseconds"),
        "end_time": span.end_time.isoformat(timespec="microseconds"),
        "duration_seconds": span.duration_seconds,
        "start_fields": span.start_fields,
        "end_fields": span.end_fields,
        "start_free_text": span.start_free_text,
        "end_free_text": span.end_free_text,
        "start_line_index": span.start_line_index,
        "end_line_index": span.end_line_index,
        "request_id": span.request_id,
        "stack_context_key": span.stack_context_key,
        "stack_context_param_key": span.stack_context_param_key,
        "caller_stack": list(span.caller_stack),
        "caller_stack_with_params": list(span.caller_stack_with_params),
        "response_category": span.response_category,
        "reset_trigger_request_id": span.reset_trigger_request_id,
        "reset_trigger_start_time": None if span.reset_trigger_start_time is None else span.reset_trigger_start_time.isoformat(
            timespec="microseconds"),
        "reset_trigger_end_time": None if span.reset_trigger_end_time is None else span.reset_trigger_end_time.isoformat(
            timespec="microseconds"),
        "child_segments": span.child_segments,
        "self_time_seconds": span.self_time_seconds,
    }


def span_to_csv_row(span: PerformanceSpan) -> dict[str, object]:
    return {
        "source": span.source,
        "context_text": span.context_text,
        "thread_id": span.thread_id,
        "start_time": span.start_time.isoformat(timespec="microseconds"),
        "end_time": span.end_time.isoformat(timespec="microseconds"),
        "duration_ms": span.duration_seconds * 1000.0,
        "request_id": span.request_id,
        "response_category": span.response_category,
        "start_line_index": span.start_line_index,
        "end_line_index": span.end_line_index,
        "stack_context_key": span.stack_context_key,
        "stack_context_param_key": span.stack_context_param_key,
        "self_time_ms": None if span.self_time_seconds is None else span.self_time_seconds * 1000.0,
        "context_parts_json": json.dumps(list(span.context_parts)),
        "caller_stack_json": json.dumps(list(span.caller_stack)),
        "caller_stack_with_params_json": json.dumps(list(span.caller_stack_with_params)),
        "start_fields_json": json.dumps(span.start_fields),
        "end_fields_json": json.dumps(span.end_fields),
        "child_segments_json": json.dumps(span.child_segments),
        "start_free_text": span.start_free_text,
        "end_free_text": span.end_free_text,
    }


def warning_to_record(warning: ParseWarning) -> dict[str, object]:
    return asdict(warning)


def warning_to_csv_row(warning: ParseWarning) -> dict[str, object]:
    return {
        "source": warning.source,
        "line_index": warning.line_index,
        "message": warning.message,
    }


def _parse_datetime(value: str) -> datetime:
    return datetime.fromisoformat(value)


def record_to_span(record: dict[str, object]) -> PerformanceSpan:
    return PerformanceSpan(
        source=str(record["source"]),
        context_parts=tuple(record["context_parts"]),
        context_text=str(record["context_text"]),
        thread_id=int(record["thread_id"]),
        start_time=_parse_datetime(str(record["start_time"])),
        end_time=_parse_datetime(str(record["end_time"])),
        duration_seconds=float(record["duration_seconds"]),
        start_fields=dict(record["start_fields"]),
        end_fields=dict(record["end_fields"]),
        start_free_text=str(record["start_free_text"]),
        end_free_text=str(record["end_free_text"]),
        start_line_index=int(record["start_line_index"]),
        end_line_index=int(record["end_line_index"]),
        request_id=None if record["request_id"] is None else int(record["request_id"]),
        stack_context_key=str(record["stack_context_key"]),
        stack_context_param_key=str(record["stack_context_param_key"]),
        caller_stack=tuple(record["caller_stack"]),
        caller_stack_with_params=tuple(record["caller_stack_with_params"]),
        response_category=record.get("response_category"),
        reset_trigger_request_id=None if record.get("reset_trigger_request_id") is None else int(
            record["reset_trigger_request_id"]),
        reset_trigger_start_time=None if record.get("reset_trigger_start_time") is None else _parse_datetime(
            str(record["reset_trigger_start_time"])),
        reset_trigger_end_time=None if record.get("reset_trigger_end_time") is None else _parse_datetime(
            str(record["reset_trigger_end_time"])),
        child_segments=list(record.get("child_segments", [])),
        self_time_seconds=record.get("self_time_seconds"),
    )


def load_span_records(path: Path) -> list[dict[str, object]]:
    payload = read_json(path)
    if not isinstance(payload, list):
        raise ValueError(f"Expected list payload in {path}, got {type(payload)!r}")
    return [dict(item) for item in payload]


def load_spans(path: Path) -> list[PerformanceSpan]:
    return [record_to_span(record) for record in load_span_records(path)]


def latency_sample_to_record(sample: LatencySample) -> dict[str, object]:
    row = asdict(sample)
    row["initiator_start"] = sample.initiator_start.isoformat(timespec="microseconds")
    row["response_start"] = sample.response_start.isoformat(timespec="microseconds")
    return row


def latency_sample_to_csv_row(sample: LatencySample) -> dict[str, object]:
    return {
        "operation": sample.operation,
        "correlation_mode": sample.correlation_mode,
        "response_category": sample.response_category,
        "initiator_group_key": sample.initiator_group_key,
        "initiator_iter": sample.initiator_iter,
        "initiator_msg": sample.initiator_msg,
        "initiator_index": sample.initiator_index,
        "initiator_start": sample.initiator_start.isoformat(timespec="microseconds"),
        "response_start": sample.response_start.isoformat(timespec="microseconds"),
        "latency_ms": sample.latency_ms,
        "response_request_id": sample.response_request_id,
        "heuristic": sample.heuristic,
        "note": sample.note,
    }


def span_records_from_spans(spans: Sequence[PerformanceSpan]) -> list[dict[str, object]]:
    return [span_to_record(span) for span in spans]
