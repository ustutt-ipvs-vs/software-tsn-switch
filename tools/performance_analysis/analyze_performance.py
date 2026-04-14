from __future__ import annotations

import argparse
import csv
import json
import math
import re
import statistics
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Iterable, Iterator, Sequence

LOG_LINE_RE = re.compile(
    r"^\[(?P<timestamp>[^\]]+)\]\s+\|\s+Thread:(?P<thread>\d+)\s+\|\s+(?P<context>(?:\[[^\]]+\]\s*)+)\|\s+(?P<message>.*)$"
)
MARKER_RE = re.compile(r"\b(Start|End)\b")
KEY_VALUE_RE = re.compile(r"(?P<key>[A-Za-z_][A-Za-z0-9_.:-]*)(?P<sep>[:=])(?P<value>[^\s]+)")

EXCLUDED_SIGNATURE_KEYS = {"req", "requestid", "msgid", "msg", "id"}


@dataclass(slots=True)
class ParsedLogLine:
    raw: str
    timestamp: datetime
    thread_id: int
    context_parts: tuple[str, ...]
    context_text: str
    marker: str | None
    fields: dict[str, str]
    free_text: str

    @property
    def request_id(self) -> int | None:
        for key in ("req", "requestid"):
            value = self.fields.get(key)
            if value is None:
                continue
            try:
                return int(value)
            except ValueError:
                return None
        return None


@dataclass(slots=True)
class SpanFrame:
    context_parts: tuple[str, ...]
    context_text: str
    fields: dict[str, str]
    free_text: str
    timestamp: datetime
    thread_id: int
    line_index: int

    def signature(self, include_params: bool) -> str:
        if not include_params:
            return self.context_text

        interesting_fields = []
        for key, value in sorted(self.fields.items()):
            if key.lower() in EXCLUDED_SIGNATURE_KEYS:
                continue
            interesting_fields.append(f"{key}={value}")
        if self.free_text:
            interesting_fields.append(self.free_text)
        if not interesting_fields:
            return self.context_text
        return f"{self.context_text}<{';'.join(interesting_fields)}>"


@dataclass(slots=True)
class PerformanceSpan:
    context_parts: tuple[str, ...]
    context_text: str
    thread_id: int
    start_time: datetime
    end_time: datetime
    duration_seconds: float
    start_fields: dict[str, str]
    end_fields: dict[str, str]
    start_free_text: str
    end_free_text: str
    start_line_index: int
    end_line_index: int
    request_id: int | None
    stack_context_key: str
    stack_context_param_key: str
    caller_stack: tuple[str, ...]
    caller_stack_with_params: tuple[str, ...]
    cb_change_category: str | None = None
    child_segments: list[dict[str, object]] = field(default_factory=list)
    self_time_seconds: float | None = None

    @property
    def operation_key(self) -> str:
        return self.context_text

    @property
    def start_label(self) -> str:
        request_part = f" req={self.request_id}" if self.request_id is not None else ""
        return f"{self.context_text}{request_part}"

    def to_row(self) -> dict[str, object]:
        return {
            "context": self.context_text,
            "thread_id": self.thread_id,
            "start_time": self.start_time.isoformat(timespec="microseconds"),
            "end_time": self.end_time.isoformat(timespec="microseconds"),
            "duration_ms": self.duration_seconds * 1000.0,
            "request_id": self.request_id,
            "stack_context_key": self.stack_context_key,
            "stack_context_param_key": self.stack_context_param_key,
            "cb_change_category": self.cb_change_category,
            "self_time_ms": None if self.self_time_seconds is None else self.self_time_seconds * 1000.0,
        }


@dataclass(slots=True)
class ParseWarning:
    line_index: int
    message: str


@dataclass(slots=True)
class SpanAnalysisResult:
    spans: list[PerformanceSpan]
    warnings: list[ParseWarning]


@dataclass(slots=True)
class GroupStats:
    group_key: str
    count: int
    mean_ms: float
    median_ms: float
    min_ms: float
    max_ms: float
    stdev_ms: float
    p90_ms: float
    p95_ms: float
    p99_ms: float


@dataclass(slots=True)
class LatencySample:
    operation: str
    correlation_mode: str
    callback_category: str
    client_group_key: str
    client_iter: int | None
    client_msg: int | None
    client_index: int
    client_start: datetime
    callback_start: datetime
    latency_ms: float
    callback_label: str
    callback_request_id: int | None
    heuristic: bool
    note: str


@dataclass(slots=True)
class MetricSample:
    metric_id: str
    metric_name: str
    scope: str
    msg_index: int | None
    operation: str
    correlation_mode: str
    source_mode: str
    value_ms: float
    request_id: int | None
    linked_request_id: int | None
    heuristic: bool
    note: str
    structural_self_ms: float | None = None
    structural_child_ms: float | None = None
    structural_gap_ms: float | None = None


@dataclass(slots=True)
class AnalysisBundle:
    client_spans: list[PerformanceSpan]
    server_spans: list[PerformanceSpan]
    client_warnings: list[ParseWarning]
    server_warnings: list[ParseWarning]
    group_stats: dict[str, list[GroupStats]]
    latency_samples: list[LatencySample]
    metric_samples: list[MetricSample]


def parse_log_line(raw_line: str, line_index: int) -> ParsedLogLine | None:
    stripped = raw_line.rstrip("\n")
    if not stripped:
        return None

    match = LOG_LINE_RE.match(stripped)
    if not match:
        return None

    timestamp = datetime.strptime(match.group("timestamp"), "%Y-%m-%d %H:%M:%S.%f")
    context_text = match.group("context").strip()
    context_parts = tuple(part for part in re.findall(r"\[([^\]]+)\]", context_text))
    message = match.group("message").strip()

    marker_match = MARKER_RE.search(message)
    marker = marker_match.group(1) if marker_match else None
    if marker_match:
        pre_marker = message[: marker_match.start()].strip()
        post_marker = message[marker_match.end():].strip()
        combined = f"{pre_marker} {post_marker}".strip()
    else:
        combined = message

    fields: dict[str, str] = {}
    for field_match in KEY_VALUE_RE.finditer(combined):
        fields[field_match.group("key").lower()] = field_match.group("value")

    free_text = KEY_VALUE_RE.sub(" ", combined)
    free_text = " ".join(free_text.split())

    return ParsedLogLine(
        raw=stripped,
        timestamp=timestamp,
        thread_id=int(match.group("thread")),
        context_parts=context_parts,
        context_text=context_text,
        marker=marker,
        fields=fields,
        free_text=free_text,
    )


def parse_log_file(path: Path) -> list[tuple[int, ParsedLogLine]]:
    parsed_lines: list[tuple[int, ParsedLogLine]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_index, raw_line in enumerate(handle):
            parsed = parse_log_line(raw_line, line_index)
            if parsed is None:
                continue
            parsed_lines.append((line_index, parsed))
    return parsed_lines


def _frame_signature(frame: SpanFrame, include_params: bool) -> str:
    return frame.signature(include_params)


def _stack_key(frames: Sequence[SpanFrame], include_params: bool) -> tuple[str, ...]:
    return tuple(_frame_signature(frame, include_params) for frame in frames)


def reconstruct_spans(parsed_lines: Sequence[tuple[int, ParsedLogLine]]) -> SpanAnalysisResult:
    stacks_by_thread: dict[int, list[tuple[int, ParsedLogLine]]] = defaultdict(list)
    spans: list[PerformanceSpan] = []
    warnings: list[ParseWarning] = []

    for line_index, parsed in parsed_lines:
        if parsed.marker is None:
            continue

        if parsed.marker == "Start":
            stacks_by_thread[parsed.thread_id].append((line_index, parsed))
            continue

        thread_stack = stacks_by_thread.get(parsed.thread_id)
        if not thread_stack:
            warnings.append(ParseWarning(line_index, f"End without matching Start for {parsed.context_text}"))
            continue

        match_index = None
        for candidate_index in range(len(thread_stack) - 1, -1, -1):
            candidate_line_index, candidate = thread_stack[candidate_index]
            if candidate.context_parts != parsed.context_parts:
                continue
            candidate_req = candidate.request_id
            parsed_req = parsed.request_id
            if candidate_req is not None and parsed_req is not None and candidate_req != parsed_req:
                continue
            match_index = candidate_index
            break

        if match_index is None:
            warnings.append(ParseWarning(line_index, f"No matching Start found for End {parsed.context_text}"))
            continue

        unmatched_suffix = thread_stack[match_index + 1:]
        if unmatched_suffix:
            warnings.append(
                ParseWarning(
                    line_index,
                    f"Nested frames closed out of order before End of {parsed.context_text}: "
                    + ", ".join(item[1].context_text for item in unmatched_suffix),
                )
            )

        start_line_index, start_event = thread_stack[match_index]
        del thread_stack[match_index:]

        ancestor_frames = [
            SpanFrame(
                context_parts=ancestor_event.context_parts,
                context_text=ancestor_event.context_text,
                fields=ancestor_event.fields,
                free_text=ancestor_event.free_text,
                timestamp=ancestor_event.timestamp,
                thread_id=ancestor_event.thread_id,
                line_index=ancestor_line_index,
            )
            for ancestor_line_index, ancestor_event in thread_stack
        ]
        current_frame = SpanFrame(
            context_parts=start_event.context_parts,
            context_text=start_event.context_text,
            fields=start_event.fields,
            free_text=start_event.free_text,
            timestamp=start_event.timestamp,
            thread_id=start_event.thread_id,
            line_index=start_line_index,
        )

        stack_context_key = " > ".join(_stack_key([*ancestor_frames, current_frame], include_params=False))
        stack_context_param_key = " > ".join(_stack_key([*ancestor_frames, current_frame], include_params=True))

        span = PerformanceSpan(
            context_parts=start_event.context_parts,
            context_text=start_event.context_text,
            thread_id=start_event.thread_id,
            start_time=start_event.timestamp,
            end_time=parsed.timestamp,
            duration_seconds=(parsed.timestamp - start_event.timestamp).total_seconds(),
            start_fields=start_event.fields,
            end_fields=parsed.fields,
            start_free_text=start_event.free_text,
            end_free_text=parsed.free_text,
            start_line_index=start_line_index,
            end_line_index=line_index,
            request_id=start_event.request_id if start_event.request_id is not None else parsed.request_id,
            stack_context_key=stack_context_key,
            stack_context_param_key=stack_context_param_key,
            caller_stack=tuple(frame.context_text for frame in ancestor_frames),
            caller_stack_with_params=tuple(frame.signature(True) for frame in ancestor_frames),
        )
        spans.append(span)

    for thread_id, remaining in stacks_by_thread.items():
        for line_index, event in remaining:
            warnings.append(ParseWarning(line_index, f"Unclosed Start on thread {thread_id} for {event.context_text}"))

    spans.sort(key=lambda span: (span.start_time, span.thread_id, span.start_line_index))
    _annotate_cb_change_categories(spans)
    _annotate_nested_segments(spans)
    return SpanAnalysisResult(spans=spans, warnings=warnings)


def _annotate_cb_change_categories(spans: Sequence[PerformanceSpan]) -> None:
    for span in spans:
        if not span.context_text.startswith("[CB_CHANGE]"):
            continue
        end_text = span.end_free_text.lower()
        span.cb_change_category = "internal" if "internal" in end_text else "external"


def _annotate_nested_segments(spans: list[PerformanceSpan]) -> None:
    spans_by_thread: dict[int, list[PerformanceSpan]] = defaultdict(list)
    for span in spans:
        spans_by_thread[span.thread_id].append(span)

    for thread_spans in spans_by_thread.values():
        thread_spans.sort(key=lambda span: (span.start_time, span.end_time, span.start_line_index))
        parent_stack: list[PerformanceSpan] = []
        children_by_parent: dict[int, list[PerformanceSpan]] = defaultdict(list)

        for span in thread_spans:
            while parent_stack and span.start_time >= parent_stack[-1].end_time:
                parent_stack.pop()
            if parent_stack:
                children_by_parent[id(parent_stack[-1])].append(span)
            parent_stack.append(span)

        for parent in thread_spans:
            children = sorted(children_by_parent.get(id(parent), []), key=lambda item: (item.start_time, item.end_time))
            if not children:
                parent.self_time_seconds = parent.duration_seconds
                continue

            segments: list[dict[str, object]] = []
            cursor = parent.start_time
            total_child_seconds = 0.0
            for child_index, child in enumerate(children):
                if child.start_time > cursor:
                    segments.append(
                        {
                            "kind": "gap_before_child",
                            "child_index": child_index,
                            "start": cursor.isoformat(timespec="microseconds"),
                            "end": child.start_time.isoformat(timespec="microseconds"),
                            "duration_ms": (child.start_time - cursor).total_seconds() * 1000.0,
                        }
                    )
                segments.append(
                    {
                        "kind": "child",
                        "child_index": child_index,
                        "context": child.context_text,
                        "start": child.start_time.isoformat(timespec="microseconds"),
                        "end": child.end_time.isoformat(timespec="microseconds"),
                        "duration_ms": child.duration_seconds * 1000.0,
                        "params": child.start_fields,
                    }
                )
                total_child_seconds += child.duration_seconds
                cursor = max(cursor, child.end_time)

            if cursor < parent.end_time:
                segments.append(
                    {
                        "kind": "tail_gap",
                        "start": cursor.isoformat(timespec="microseconds"),
                        "end": parent.end_time.isoformat(timespec="microseconds"),
                        "duration_ms": (parent.end_time - cursor).total_seconds() * 1000.0,
                    }
                )

            parent.child_segments = segments
            parent.self_time_seconds = max(parent.duration_seconds - total_child_seconds, 0.0)


def percentile(values: Sequence[float], pct: float) -> float:
    if not values:
        return float("nan")
    if len(values) == 1:
        return float(values[0])
    ordered = sorted(values)
    rank = (len(ordered) - 1) * pct
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return float(ordered[int(rank)])
    low_value = ordered[low]
    high_value = ordered[high]
    return float(low_value + (high_value - low_value) * (rank - low))


def summarise_groups(spans: Sequence[PerformanceSpan], group_name: str, key_func) -> list[GroupStats]:
    grouped: dict[str, list[float]] = defaultdict(list)
    for span in spans:
        grouped[key_func(span)].append(span.duration_seconds * 1000.0)

    stats: list[GroupStats] = []
    for group_key, values in sorted(grouped.items(), key=lambda item: (-len(item[1]), item[0])):
        stats.append(
            GroupStats(
                group_key=group_key,
                count=len(values),
                mean_ms=statistics.fmean(values),
                median_ms=statistics.median(values),
                min_ms=min(values),
                max_ms=max(values),
                stdev_ms=statistics.pstdev(values) if len(values) > 1 else 0.0,
                p90_ms=percentile(values, 0.90),
                p95_ms=percentile(values, 0.95),
                p99_ms=percentile(values, 0.99),
            )
        )
    return stats


def summarize_by_granularity(spans: Sequence[PerformanceSpan]) -> dict[str, list[GroupStats]]:
    return {
        "function": summarise_groups(spans, "function", lambda span: span.context_text),
        "stack": summarise_groups(spans, "stack", lambda span: span.stack_context_key),
        "stack_with_params": summarise_groups(spans, "stack_with_params", lambda span: span.stack_context_param_key),
    }


def _first_timestamp(spans: Sequence[PerformanceSpan]) -> datetime | None:
    return min((span.start_time for span in spans), default=None)


def _callback_pools(server_spans: Sequence[PerformanceSpan]) -> dict[str, list[PerformanceSpan]]:
    grouped: dict[str, list[PerformanceSpan]] = defaultdict(list)
    for span in server_spans:
        if span.context_text.startswith("[CB_OPER]"):
            grouped["CB_OPER|all"].append(span)
            continue
        if span.context_text.startswith("[CB_CHANGE]"):
            grouped["CB_CHANGE|all"].append(span)
            if span.cb_change_category in {"internal", "external"}:
                grouped[f"CB_CHANGE|{span.cb_change_category}"].append(span)
    for spans in grouped.values():
        spans.sort(key=lambda span: (span.start_time, span.thread_id, span.start_line_index))
    return grouped


def _as_int(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def _client_grouping(operation: str, span: PerformanceSpan, client_index: int) -> tuple[str, int | None, int | None]:
    iter_index = _as_int(span.start_fields.get("iter"))
    msg_index = _as_int(span.start_fields.get("msg"))
    iter_part = str(iter_index) if iter_index is not None else "na"
    msg_part = str(msg_index) if msg_index is not None else f"idx{client_index}"
    return (f"{operation}|iter={iter_part}|msg={msg_part}", iter_index, msg_index)


def _correlation_targets(operation: str) -> list[tuple[str, str, str]]:
    if operation == "GET":
        return [("primary_get", "CB_OPER", "all")]
    if operation == "COMMIT":
        return [
            ("primary_commit", "CB_CHANGE", "all"),
            ("primary_commit", "CB_CHANGE", "internal"),
            ("primary_commit", "CB_CHANGE", "external"),
        ]
    if operation == "EDIT_CANDIDATE":
        return [
            ("diagnostic_edit_candidate", "CB_CHANGE", "all"),
            ("diagnostic_edit_candidate", "CB_CHANGE", "internal"),
            ("diagnostic_edit_candidate", "CB_CHANGE", "external"),
        ]
    return []


def correlate_latency(client_spans: Sequence[PerformanceSpan], server_spans: Sequence[PerformanceSpan]) -> list[
    LatencySample]:
    client_events: list[tuple[str, int, PerformanceSpan]] = []
    for index, span in enumerate(client_spans):
        op = span.context_text.strip("[]")
        if op not in {"EDIT_CANDIDATE", "COMMIT", "GET"}:
            continue
        client_events.append((op, index, span))

    server_grouped = _callback_pools(server_spans)
    server_cursors: dict[tuple[str, str], int] = {}
    samples: list[LatencySample] = []

    for operation, client_index, client_span in client_events:
        client_group_key, iter_index, msg_index = _client_grouping(operation, client_span, client_index)

        for correlation_mode, callback_kind, callback_category in _correlation_targets(operation):
            pool_key = f"{callback_kind}|{callback_category}"
            if pool_key not in server_grouped:
                continue

            server_list = server_grouped[pool_key]
            cursor_key = (pool_key, correlation_mode)
            cursor = server_cursors.get(cursor_key, 0)
            chosen_span = None
            while cursor < len(server_list):
                candidate = server_list[cursor]
                cursor += 1
                if candidate.start_time >= client_span.start_time:
                    chosen_span = candidate
                    break
            server_cursors[cursor_key] = cursor
            if chosen_span is None:
                continue

            note = (
                f"Matched by chronological order for {callback_kind}"
                f" ({callback_category}), grouped by operation+iter+msg with fallback"
            )
            samples.append(
                LatencySample(
                    operation=operation,
                    correlation_mode=correlation_mode,
                    callback_category=callback_category,
                    client_group_key=client_group_key,
                    client_iter=iter_index,
                    client_msg=msg_index,
                    client_index=client_index,
                    client_start=client_span.start_time,
                    callback_start=chosen_span.start_time,
                    latency_ms=(chosen_span.start_time - client_span.start_time).total_seconds() * 1000.0,
                    callback_label=chosen_span.context_text,
                    callback_request_id=chosen_span.request_id,
                    heuristic=True,
                    note=note,
                )
            )

    return samples


def summarise_latency_samples(samples: Sequence[LatencySample]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str], list[float]] = defaultdict(list)
    for sample in samples:
        key = (sample.operation, sample.correlation_mode, sample.callback_category)
        grouped[key].append(sample.latency_ms)

    rows: list[dict[str, object]] = []
    for (operation, correlation_mode, callback_category), values in sorted(grouped.items()):
        rows.append(
            {
                "operation": operation,
                "correlation_mode": correlation_mode,
                "callback_category": callback_category,
                "count": len(values),
                "mean_ms": statistics.fmean(values),
                "median_ms": statistics.median(values),
                "min_ms": min(values),
                "max_ms": max(values),
                "p95_ms": percentile(values, 0.95),
                "p99_ms": percentile(values, 0.99),
            }
        )
    return rows


def _add_metric_sample(
        sink: list[MetricSample],
        *,
        metric_id: str,
        metric_name: str,
        operation: str,
        correlation_mode: str,
        source_mode: str,
        value_ms: float,
        msg_index: int | None,
        request_id: int | None,
        linked_request_id: int | None,
        heuristic: bool,
        note: str,
        structural_self_ms: float | None = None,
        structural_child_ms: float | None = None,
        structural_gap_ms: float | None = None,
) -> None:
    sink.append(
        MetricSample(
            metric_id=metric_id,
            metric_name=metric_name,
            scope="all",
            msg_index=None,
            operation=operation,
            correlation_mode=correlation_mode,
            source_mode=source_mode,
            value_ms=value_ms,
            request_id=request_id,
            linked_request_id=linked_request_id,
            heuristic=heuristic,
            note=note,
            structural_self_ms=structural_self_ms,
            structural_child_ms=structural_child_ms,
            structural_gap_ms=structural_gap_ms,
        )
    )
    if msg_index is not None:
        sink.append(
            MetricSample(
                metric_id=metric_id,
                metric_name=metric_name,
                scope="per_msg",
                msg_index=msg_index,
                operation=operation,
                correlation_mode=correlation_mode,
                source_mode=source_mode,
                value_ms=value_ms,
                request_id=request_id,
                linked_request_id=linked_request_id,
                heuristic=heuristic,
                note=note,
                structural_self_ms=structural_self_ms,
                structural_child_ms=structural_child_ms,
                structural_gap_ms=structural_gap_ms,
            )
        )


def _gpt_structure_breakdown(span: PerformanceSpan) -> dict[str, float]:
    child_ms = 0.0
    gap_ms = 0.0
    for segment in span.child_segments:
        duration_ms = float(segment.get("duration_ms", 0.0))
        if segment.get("kind") == "child":
            child_ms += duration_ms
        elif segment.get("kind") in {"gap_before_child", "tail_gap"}:
            gap_ms += duration_ms

    self_ms = (span.self_time_seconds or 0.0) * 1000.0
    total_ms = span.duration_seconds * 1000.0
    return {
        "total_ms": total_ms,
        "self_ms": self_ms,
        "child_ms": child_ms,
        "gap_ms": gap_ms,
    }


def _request_message_index_map(latency_samples: Sequence[LatencySample]) -> dict[int, int]:
    grouped: dict[int, set[int]] = defaultdict(set)
    for sample in latency_samples:
        if sample.callback_request_id is None or sample.client_msg is None:
            continue
        grouped[sample.callback_request_id].add(sample.client_msg)

    return {request_id: min(msg_indices) for request_id, msg_indices in grouped.items() if msg_indices}


def _gpt_path_key(root_span: PerformanceSpan, span: PerformanceSpan) -> str | None:
    if span.thread_id != root_span.thread_id:
        return None
    if span.start_time < root_span.start_time or span.end_time > root_span.end_time:
        return None

    if span is root_span:
        return root_span.context_text

    path_parts = list(span.caller_stack)
    if root_span.context_text in path_parts:
        path_parts = path_parts[path_parts.index(root_span.context_text):]
    else:
        return None

    return " > ".join([*path_parts, span.context_text])


def _gpt_path_samples(
        chains: dict[int, dict[str, object]],
        latency_samples: Sequence[LatencySample],
        *,
        source_mode: str,
) -> list[MetricSample]:
    rows: list[MetricSample] = []
    request_message_indices = _request_message_index_map(latency_samples)

    for request_id, chain in sorted(chains.items()):
        request_spans = chain.get("request_spans", [])
        gpt_spans = chain.get("gpt_spans", [])
        if not isinstance(request_spans, list) or not isinstance(gpt_spans, list):
            continue

        msg_index = request_message_indices.get(request_id)

        for root_span in gpt_spans:
            if not isinstance(root_span, PerformanceSpan):
                continue

            for span in request_spans:
                if not isinstance(span, PerformanceSpan):
                    continue

                path_key = _gpt_path_key(root_span, span)
                if path_key is None:
                    continue

                _add_metric_sample(
                    rows,
                    metric_id="m6_non_internal_cb_change_gpt_duration",
                    metric_name="Non-internal CB_CHANGE GPT path duration",
                    operation=path_key,
                    correlation_mode="gpt_path",
                    source_mode=source_mode,
                    value_ms=span.duration_seconds * 1000.0,
                    msg_index=None,
                    request_id=request_id,
                    linked_request_id=request_id,
                    heuristic=False,
                    note="Nested GPT path segment duration",
                )

                if msg_index is None:
                    continue

                _add_metric_sample(
                    rows,
                    metric_id="m6_5_non_internal_cb_change_gpt_duration",
                    metric_name="Non-internal CB_CHANGE GPT path duration by message index",
                    operation=path_key,
                    correlation_mode="gpt_path_msg_index",
                    source_mode=source_mode,
                    value_ms=span.duration_seconds * 1000.0,
                    msg_index=msg_index,
                    request_id=request_id,
                    linked_request_id=request_id,
                    heuristic=False,
                    note="Nested GPT path segment duration grouped by message index",
                )

    return rows


def _request_chains(server_spans: Sequence[PerformanceSpan]) -> dict[int, dict[str, object]]:
    external_by_request: dict[int, list[PerformanceSpan]] = defaultdict(list)
    reset_by_request: dict[int, list[PerformanceSpan]] = defaultdict(list)
    gpt_by_request: dict[int, list[PerformanceSpan]] = defaultdict(list)
    all_by_request: dict[int, list[PerformanceSpan]] = defaultdict(list)

    for span in server_spans:
        if span.request_id is None:
            continue
        all_by_request[span.request_id].append(span)
        if span.context_text.startswith("[CB_CHANGE]") and span.cb_change_category == "external":
            external_by_request[span.request_id].append(span)
            if span.context_text.startswith("[CB_CHANGE] [GPT]"):
                gpt_by_request[span.request_id].append(span)
        if span.context_text.startswith("[RESET_GPT_TRIGGER]"):
            reset_by_request[span.request_id].append(span)

    chains: dict[int, dict[str, object]] = {}
    for request_id, spans in external_by_request.items():
        ordered = sorted(spans, key=lambda item: (item.start_time, item.end_time, item.start_line_index))
        first_start = ordered[0].start_time
        last_end = max(span.end_time for span in ordered)

        gpt_spans = sorted(
            gpt_by_request.get(request_id, []),
            key=lambda item: (item.start_time, item.end_time, item.start_line_index),
        )
        gpt_structures = [_gpt_structure_breakdown(span) for span in gpt_spans]

        reset_spans = sorted(
            reset_by_request.get(request_id, []),
            key=lambda item: (item.start_time, item.end_time, item.start_line_index)
        )
        reset_end = max((span.end_time for span in reset_spans), default=None)
        reset_start = min((span.start_time for span in reset_spans), default=None)

        chains[request_id] = {
            "request_id": request_id,
            "first_external_start": first_start,
            "last_external_end": last_end,
            "reset_start": reset_start,
            "reset_end": reset_end,
            "request_spans": sorted(all_by_request.get(request_id, []),
                                    key=lambda item: (item.start_time, item.end_time, item.start_line_index)),
            "external_spans": ordered,
            "gpt_spans": gpt_spans,
            "gpt_structures": gpt_structures,
        }

    return chains


def _duration_metric_samples(client_spans: Sequence[PerformanceSpan], source_mode: str) -> list[MetricSample]:
    rows: list[MetricSample] = []
    metric_map = {
        "GET": ("m1_client_get_duration", "Client GET duration"),
        "EDIT_CANDIDATE": ("m2_client_edit_candidate_duration", "Client EDIT_CANDIDATE duration"),
        "COMMIT": ("m4_client_commit_duration", "Client COMMIT duration"),
    }

    for span in client_spans:
        operation = span.context_text.strip("[]")
        metric = metric_map.get(operation)
        if metric is None:
            continue
        msg_index = _as_int(span.start_fields.get("msg"))
        _add_metric_sample(
            rows,
            metric_id=metric[0],
            metric_name=metric[1],
            operation=operation,
            correlation_mode="direct_span",
            source_mode=source_mode,
            value_ms=span.duration_seconds * 1000.0,
            msg_index=msg_index,
            request_id=span.request_id,
            linked_request_id=None,
            heuristic=False,
            note="Direct client span duration",
        )
    return rows


def _cross_side_reset_metrics(
        latency_samples: Sequence[LatencySample],
        chains: dict[int, dict[str, object]],
        source_mode: str,
) -> list[MetricSample]:
    rows: list[MetricSample] = []
    targets = {
        "EDIT_CANDIDATE": (
            "diagnostic_edit_candidate",
            "m3_edit_start_to_reset_done",
            "EDIT_CANDIDATE start to RESET_GPT_TRIGGER end",
        ),
        "COMMIT": (
            "primary_commit",
            "m5_commit_start_to_reset_done",
            "COMMIT start to RESET_GPT_TRIGGER end",
        ),
    }

    for sample in latency_samples:
        target = targets.get(sample.operation)
        if target is None:
            continue
        if sample.correlation_mode != target[0] or sample.callback_category != "external":
            continue
        if sample.callback_request_id is None:
            continue
        chain = chains.get(sample.callback_request_id)
        if chain is None:
            continue
        reset_end = chain.get("reset_end")
        if reset_end is None:
            continue

        value_ms = (reset_end - sample.client_start).total_seconds() * 1000.0
        _add_metric_sample(
            rows,
            metric_id=target[1],
            metric_name=target[2],
            operation=sample.operation,
            correlation_mode=sample.correlation_mode,
            source_mode=source_mode,
            value_ms=value_ms,
            msg_index=sample.client_msg,
            request_id=None,
            linked_request_id=sample.callback_request_id,
            heuristic=sample.heuristic,
            note="Chronological client/server join via matched external CB_CHANGE request",
        )

    return rows


def _server_request_metrics(
        chains: dict[int, dict[str, object]],
        *,
        source_mode: str,
        latency_samples: Sequence[LatencySample],
) -> list[MetricSample]:
    rows: list[MetricSample] = []

    rows.extend(_gpt_path_samples(chains, latency_samples, source_mode=source_mode))

    for request_id, chain in sorted(chains.items()):
        first_external_start = chain["first_external_start"]
        last_external_end = chain["last_external_end"]
        reset_end = chain.get("reset_end")
        gpt_spans = chain.get("gpt_spans", [])

        _add_metric_sample(
            rows,
            metric_id="m7_non_internal_request_window",
            metric_name="First non-internal CB_CHANGE start to same-request last CB_CHANGE end",
            operation="CB_CHANGE",
            correlation_mode="request_window",
            source_mode=source_mode,
            value_ms=(last_external_end - first_external_start).total_seconds() * 1000.0,
            msg_index=None,
            request_id=request_id,
            linked_request_id=request_id,
            heuristic=False,
            note="Request-scoped non-internal CB_CHANGE window",
        )

        if reset_end is not None:
            _add_metric_sample(
                rows,
                metric_id="m8_non_internal_start_to_reset_done",
                metric_name="First non-internal CB_CHANGE start to reset completion",
                operation="CB_CHANGE",
                correlation_mode="request_to_reset",
                source_mode=source_mode,
                value_ms=(reset_end - first_external_start).total_seconds() * 1000.0,
                msg_index=None,
                request_id=request_id,
                linked_request_id=request_id,
                heuristic=False,
                note="Joined by shared RESET_GPT_TRIGGER request id",
            )

    return rows


def compute_metric_samples(
        client_spans: Sequence[PerformanceSpan],
        server_spans: Sequence[PerformanceSpan],
        latency_samples: Sequence[LatencySample],
        *,
        source_mode: str,
) -> list[MetricSample]:
    chains = _request_chains(server_spans)
    rows: list[MetricSample] = []
    rows.extend(_server_request_metrics(chains, source_mode=source_mode, latency_samples=latency_samples))

    if source_mode != "server_only":
        rows.extend(_duration_metric_samples(client_spans, source_mode))
        rows.extend(_cross_side_reset_metrics(latency_samples, chains, source_mode))

    return rows


def summarise_metric_samples(samples: Sequence[MetricSample]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str, int | None, str, str], list[float]] = defaultdict(list)
    for sample in samples:
        key = (
            sample.metric_id,
            sample.metric_name,
            sample.scope,
            sample.msg_index,
            sample.operation,
            sample.correlation_mode,
        )
        grouped[key].append(sample.value_ms)

    rows: list[dict[str, object]] = []

    def _mean_or_none(values: list[float | None]) -> float | None:
        filtered = [value for value in values if value is not None]
        if not filtered:
            return None
        return statistics.fmean(filtered)

    for (metric_id, metric_name, scope, msg_index, operation, correlation_mode), values in sorted(grouped.items()):
        matching_samples = [
            sample
            for sample in samples
            if sample.metric_id == metric_id
               and sample.metric_name == metric_name
               and sample.scope == scope
               and sample.msg_index == msg_index
               and sample.operation == operation
               and sample.correlation_mode == correlation_mode
        ]
        rows.append(
            {
                "metric_id": metric_id,
                "metric_name": metric_name,
                "scope": scope,
                "msg_index": msg_index,
                "operation": operation,
                "correlation_mode": correlation_mode,
                "count": len(values),
                "mean_ms": statistics.fmean(values),
                "median_ms": statistics.median(values),
                "min_ms": min(values),
                "max_ms": max(values),
                "p95_ms": percentile(values, 0.95),
                "p99_ms": percentile(values, 0.99),
                "structural_self_mean_ms": _mean_or_none([sample.structural_self_ms for sample in matching_samples]),
                "structural_child_mean_ms": _mean_or_none([sample.structural_child_ms for sample in matching_samples]),
                "structural_gap_mean_ms": _mean_or_none([sample.structural_gap_ms for sample in matching_samples]),
            }
        )
    return rows


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, default=str), encoding="utf-8")


def write_csv(path: Path, rows: Iterable[dict[str, object]]) -> None:
    rows = list(rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()) if rows else [])
        if rows:
            writer.writeheader()
            writer.writerows(rows)


def try_import_matplotlib():
    try:
        import matplotlib.pyplot as plt  # type: ignore
    except Exception:
        return None
    return plt


def plot_outputs(
        output_dir: Path,
        spans: Sequence[PerformanceSpan],
        stats_map: dict[str, list[GroupStats]],
        latency_samples: Sequence[LatencySample],
        server_spans: Sequence[PerformanceSpan],
        metric_samples: Sequence[MetricSample],
) -> list[Path]:
    plt = try_import_matplotlib()
    if plt is None:
        return []

    output_dir.mkdir(parents=True, exist_ok=True)
    created_files: list[Path] = []

    function_spans = [
        span
        for span in spans
        if not span.context_text.startswith("[CB_") and not span.context_text.startswith("[RESET_")
    ]
    function_stats = summarise_groups(function_spans, "function", lambda span: span.context_text)[:20]

    top_function_names = [item.group_key for item in function_stats[:10]]
    top_durations = [
        [span.duration_seconds * 1000.0 for span in function_spans if span.context_text == function_name]
        for function_name in top_function_names
    ]
    if any(top_durations):
        fig, ax = plt.subplots(figsize=(12, 6))
        ax.boxplot(top_durations, showfliers=False)
        ax.set_xticks(range(1, len(top_function_names) + 1))
        ax.set_xticklabels(top_function_names, rotation=35)
        ax.set_title("Duration spread for frequent functions")
        ax.set_xlabel("Function")
        ax.set_ylabel("Duration (ms)")
        fig.tight_layout()
        path = output_dir / "duration_boxplot_by_function.png"
        fig.savefig(path, dpi=160)
        plt.close(fig)
        created_files.append(path)

    commit_primary = [
        sample for sample in latency_samples if
        sample.operation == "COMMIT" and sample.correlation_mode == "primary_commit"
    ]
    commit_by_category: dict[str, list[float]] = defaultdict(list)
    for sample in commit_primary:
        commit_by_category[sample.callback_category].append(sample.latency_ms)

    ordered_categories = [cat for cat in ("all", "internal", "external") if commit_by_category.get(cat)]
    if ordered_categories:
        fig, ax = plt.subplots(figsize=(9, 5))
        ax.boxplot([commit_by_category[cat] for cat in ordered_categories], showfliers=False)
        ax.set_xticks(range(1, len(ordered_categories) + 1))
        ax.set_xticklabels(ordered_categories)
        ax.set_title("COMMIT -> CB_CHANGE latency by category")
        ax.set_xlabel("Callback category")
        ax.set_ylabel("Latency (ms)")
        fig.tight_layout()
        path = output_dir / "commit_latency_by_category.png"
        fig.savefig(path, dpi=160)
        plt.close(fig)
        created_files.append(path)

    comparison_rows = [sample for sample in latency_samples if sample.callback_category == "all"]
    comparison_grouped: dict[str, list[float]] = defaultdict(list)
    for sample in comparison_rows:
        if sample.correlation_mode in {"primary_commit", "diagnostic_edit_candidate"}:
            comparison_grouped[sample.correlation_mode].append(sample.latency_ms)
    if comparison_grouped:
        labels = sorted(comparison_grouped)
        fig, ax = plt.subplots(figsize=(9, 5))
        ax.boxplot([comparison_grouped[label] for label in labels], showfliers=False)
        ax.set_xticks(range(1, len(labels) + 1))
        ax.set_xticklabels(labels)
        ax.set_title("CB_CHANGE latency: COMMIT primary vs EDIT diagnostic")
        ax.set_ylabel("Latency (ms)")
        fig.tight_layout()
        path = output_dir / "commit_vs_edit_diagnostic_latency.png"
        fig.savefig(path, dpi=160)
        plt.close(fig)
        created_files.append(path)

    metric_summary_rows = summarise_metric_samples(metric_samples)

    per_msg_rows = [
        row
        for row in metric_summary_rows
        if row["scope"] == "per_msg" and isinstance(row["msg_index"], int)
    ]
    if per_msg_rows:
        by_metric: dict[tuple[str, str], list[float]] = defaultdict(list)
        for row in per_msg_rows:
            key = (row["metric_id"], row["correlation_mode"])
            by_metric[key].append(row["mean_ms"])

        selected_groups = sorted(by_metric.items(), key=lambda item: (-len(item[1]), item[0][0], item[0][1]))[:12]

        if selected_groups:
            fig, ax = plt.subplots(figsize=(12, 6))
            ax.boxplot([values for _, values in selected_groups], showfliers=False)
            ax.set_xticks(range(1, len(selected_groups) + 1))
            ax.set_xticklabels(
                [f"{metric_id} | {correlation_mode}" for (metric_id, correlation_mode), _ in selected_groups],
                rotation=30, ha="right")
            ax.set_title("Per-message metric distributions")
            ax.set_xlabel("Metric and correlation mode")
            ax.set_ylabel("Duration (ms)")
            ax.grid(axis="y", alpha=0.25)
            fig.tight_layout()
            path = output_dir / "metric_means_per_msg_scope.png"
            fig.savefig(path, dpi=160)
            created_files.append(path)
            plt.close(fig)

    m6_path_samples = [
        sample
        for sample in metric_samples
        if sample.metric_id == "m6_non_internal_cb_change_gpt_duration"
           and sample.scope == "all"
           and sample.correlation_mode == "gpt_path"
    ]
    if m6_path_samples:
        by_path: dict[str, list[float]] = defaultdict(list)
        for sample in m6_path_samples:
            by_path[sample.operation].append(sample.value_ms)
        selected_paths = sorted(by_path.items(), key=lambda item: (-len(item[1]), item[0]))[:12]
        if selected_paths:
            fig, ax = plt.subplots(figsize=(14, 7))
            ax.boxplot([values for _, values in selected_paths], showfliers=False)
            ax.set_xticks(range(1, len(selected_paths) + 1))
            ax.set_xticklabels([path for path, _ in selected_paths], rotation=30, ha="right")
            ax.set_title("M6 GPT path breakdown")
            ax.set_xlabel("Nested GPT path")
            ax.set_ylabel("Duration (ms)")
            ax.grid(axis="y", alpha=0.25)
            fig.tight_layout()
            path = output_dir / "m6_gpt_path_breakdown.png"
            fig.savefig(path, dpi=160)
            plt.close(fig)
            created_files.append(path)

    m65_samples = [
        sample
        for sample in metric_samples
        if sample.metric_id == "m6_5_non_internal_cb_change_gpt_duration"
           and sample.scope == "per_msg"
           and sample.correlation_mode == "gpt_path_msg_index"
           and isinstance(sample.msg_index, int)
    ]
    if m65_samples:
        by_msg: dict[int, list[float]] = defaultdict(list)
        for sample in m65_samples:
            by_msg[sample.msg_index].append(sample.value_ms)
        selected_msgs = sorted(by_msg.items(), key=lambda item: (-len(item[1]), item[0]))[:12]
        if selected_msgs:
            fig, ax = plt.subplots(figsize=(14, 7))
            ax.boxplot([values for _, values in selected_msgs], showfliers=False)
            ax.set_xticks(range(1, len(selected_msgs) + 1))
            ax.set_xticklabels([str(msg_index) for msg_index, _ in selected_msgs], rotation=0)
            ax.set_title("M6.5 GPT message-index breakdown")
            ax.set_xlabel("Message index")
            ax.set_ylabel("Duration (ms)")
            ax.grid(axis="y", alpha=0.25)
            fig.tight_layout()
            path = output_dir / "m6_5_gpt_message_index_breakdown.png"
            fig.savefig(path, dpi=160)
            plt.close(fig)
            created_files.append(path)

    request_window_samples = [
        sample
        for sample in metric_samples
        if sample.scope == "all"
           and sample.metric_id in {"m7_non_internal_request_window", "m8_non_internal_start_to_reset_done"}
    ]
    if request_window_samples:
        by_metric: dict[str, list[float]] = defaultdict(list)
        for sample in request_window_samples:
            by_metric[sample.metric_id].append(sample.value_ms)
        labels = [label for label in ("m7_non_internal_request_window", "m8_non_internal_start_to_reset_done") if
                  by_metric.get(label)]
        fig, ax = plt.subplots(figsize=(9, 5))
        ax.boxplot([by_metric[label] for label in labels], showfliers=False)
        ax.set_xticks(range(1, len(labels) + 1))
        ax.set_xticklabels(labels, rotation=12)
        ax.set_title("Request window metrics distribution")
        ax.set_ylabel("Duration (ms)")
        fig.tight_layout()
        path = output_dir / "request_window_distribution.png"
        fig.savefig(path, dpi=160)
        plt.close(fig)
        created_files.append(path)

    return created_files


def export_results(output_dir: Path, bundle: AnalysisBundle, generate_plots: bool = True) -> list[Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    created: list[Path] = []

    client_rows = [span.to_row() for span in bundle.client_spans]
    server_rows = [span.to_row() for span in bundle.server_spans]
    write_csv(output_dir / "client_spans.csv", client_rows)
    write_csv(output_dir / "server_spans.csv", server_rows)
    created.extend([output_dir / "client_spans.csv", output_dir / "server_spans.csv"])

    latency_summary_rows = summarise_latency_samples(bundle.latency_samples)
    metric_summary_rows = summarise_metric_samples(bundle.metric_samples)
    summary_payload = {
        "group_stats": {
            group_name: [asdict(item) for item in stats] for group_name, stats in bundle.group_stats.items()
        },
        "latency_samples": [asdict(sample) for sample in bundle.latency_samples],
        "latency_summary": latency_summary_rows,
        "metric_samples": [asdict(sample) for sample in bundle.metric_samples],
        "metric_summary": metric_summary_rows,
        "client_warnings": [asdict(warning) for warning in bundle.client_warnings],
        "server_warnings": [asdict(warning) for warning in bundle.server_warnings],
    }
    write_json(output_dir / "analysis_summary.json", summary_payload)
    created.append(output_dir / "analysis_summary.json")

    stats_rows = []
    for group_name, stats_list in bundle.group_stats.items():
        for item in stats_list:
            stats_rows.append(
                {
                    "group_name": group_name,
                    **asdict(item),
                }
            )
    write_csv(output_dir / "group_stats.csv", stats_rows)
    created.append(output_dir / "group_stats.csv")

    latency_rows = [asdict(sample) for sample in bundle.latency_samples]
    write_csv(output_dir / "latency_samples.csv", latency_rows)
    created.append(output_dir / "latency_samples.csv")
    write_csv(output_dir / "latency_summary.csv", latency_summary_rows)
    created.append(output_dir / "latency_summary.csv")

    metric_rows = [asdict(sample) for sample in bundle.metric_samples]
    write_csv(output_dir / "metric_samples.csv", metric_rows)
    created.append(output_dir / "metric_samples.csv")
    write_csv(output_dir / "metric_summary.csv", metric_summary_rows)
    created.append(output_dir / "metric_summary.csv")

    if generate_plots:
        plot_paths = plot_outputs(
            output_dir / "plots",
            [*bundle.client_spans, *bundle.server_spans],
            bundle.group_stats,
            bundle.latency_samples,
            bundle.server_spans,
            bundle.metric_samples,
        )
        created.extend(plot_paths)
    return created


def run_analysis(client_log: Path | None, server_log: Path, output_dir: Path, *,
                 source_mode: str = "client_server") -> AnalysisBundle:
    client_parsed = parse_log_file(client_log) if client_log is not None else []
    server_parsed = parse_log_file(server_log)

    client_analysis = reconstruct_spans(client_parsed)
    server_analysis = reconstruct_spans(server_parsed)

    group_stats = summarize_by_granularity([*client_analysis.spans, *server_analysis.spans])
    latency_samples = correlate_latency(client_analysis.spans, server_analysis.spans)
    metric_samples = compute_metric_samples(
        client_analysis.spans,
        server_analysis.spans,
        latency_samples,
        source_mode=source_mode,
    )

    return AnalysisBundle(
        client_spans=client_analysis.spans,
        server_spans=server_analysis.spans,
        client_warnings=client_analysis.warnings,
        server_warnings=server_analysis.warnings,
        group_stats=group_stats,
        latency_samples=latency_samples,
        metric_samples=metric_samples,
    )


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Analyze performance logs produced by PERFORMANCE_LOGGING.")
    default_base = Path(__file__).resolve().parent
    parser.add_argument(
        "--client-log",
        type=Path,
        default=default_base / "performance_trace_performance_testing.log",
        help="Path to the performance_testing trace log.",
    )
    parser.add_argument(
        "--server-log",
        type=Path,
        default=default_base / "performance_trace_tsnctrld_app.log",
        help="Path to the tsnctrld app trace log.",
    )
    parser.add_argument(
        "--server-only",
        action="store_true",
        help="Analyze only server log and omit client-side metrics.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=default_base / "analysis_out",
        help="Directory for CSV, JSON, and plot outputs.",
    )
    parser.add_argument("--no-plots", action="store_true", help="Skip plot generation.")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)

    source_mode = "server_only" if args.server_only else "client_server"

    client_log_path: Path | None = args.client_log
    if source_mode != "server_only":
        if not args.client_log.exists():
            raise FileNotFoundError(f"Client log not found: {args.client_log}")
    else:
        client_log_path = None

    if not args.server_log.exists():
        raise FileNotFoundError(f"Server log not found: {args.server_log}")

    bundle = run_analysis(client_log_path, args.server_log, args.output_dir, source_mode=source_mode)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    export_results(args.output_dir, bundle, generate_plots=not args.no_plots)

    print(f"Parsed client spans: {len(bundle.client_spans)}")
    print(f"Parsed server spans: {len(bundle.server_spans)}")
    print(f"Latency samples: {len(bundle.latency_samples)}")
    print(f"Metric samples: {len(bundle.metric_samples)}")
    print(f"Output directory: {args.output_dir}")

    if bundle.client_warnings or bundle.server_warnings:
        print("Warnings:")
        for warning in [*bundle.client_warnings[:5], *bundle.server_warnings[:5]]:
            print(f"  line {warning.line_index + 1}: {warning.message}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
