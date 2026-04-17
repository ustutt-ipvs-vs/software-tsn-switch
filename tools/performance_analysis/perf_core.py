from __future__ import annotations

import re
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime
from typing import Sequence

LOG_LINE_RE = re.compile(
    r"^\[(?P<timestamp>[^\]]+)\]\s+\|\s+Thread:(?P<thread>\d+)\s+\|\s+(?P<context>(?:\[[^\]]+\]\s*)+)\|\s+(?P<message>.*)$"
)
MARKER_RE = re.compile(r"\b(Start|End)\b")
KEY_VALUE_RE = re.compile(r"(?P<key>[A-Za-z_][A-Za-z0-9_.-]*)(?P<sep>[:=])(?P<value>[^\s]+)")

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
    source: str
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
    response_category: str | None = None
    reset_trigger_request_id: int | None = None
    reset_trigger_start_time: datetime | None = None
    reset_trigger_end_time: datetime | None = None
    child_segments: list[dict[str, object]] = field(default_factory=list)
    self_time_seconds: float | None = None


@dataclass(slots=True)
class ParseWarning:
    source: str
    line_index: int
    message: str


@dataclass(slots=True)
class SpanAnalysisResult:
    spans: list[PerformanceSpan]
    warnings: list[ParseWarning]


@dataclass(slots=True)
class LatencySample:
    operation: str
    correlation_mode: str
    response_category: str
    response_operation: str | None
    initiator_group_key: str
    initiator_iter: int | None
    initiator_msg: int | None
    initiator_index: int
    initiator_start: datetime
    response_start: datetime
    latency_ms: float
    response_request_id: int | None
    heuristic: bool
    note: str


def _extract_fields_and_free_text(message: str) -> tuple[dict[str, str], str]:
    fields: dict[str, str] = {}
    kept_tokens: list[str] = []
    cursor = 0
    for match in KEY_VALUE_RE.finditer(message):
        kept_tokens.extend(message[cursor:match.start()].split())
        key = match.group("key").lower()
        sep = match.group("sep")
        value = match.group("value")

        # Treat id:<value> as free text to match logging intent.
        if key == "id" and sep == ":":
            kept_tokens.append(match.group(0))
        else:
            fields[key] = value
        cursor = match.end()

    kept_tokens.extend(message[cursor:].split())
    free_text = " ".join(kept_tokens).strip()
    return fields, free_text


def parse_log_line(raw_line: str) -> ParsedLogLine | None:
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
    fields, free_text = _extract_fields_and_free_text(combined)

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


def parse_log_file(path: str) -> list[tuple[int, ParsedLogLine]]:
    parsed_lines: list[tuple[int, ParsedLogLine]] = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line_index, raw_line in enumerate(handle):
            parsed = parse_log_line(raw_line)
            if parsed is None:
                continue
            parsed_lines.append((line_index, parsed))
    return parsed_lines


def _annotate_response_categories(spans: Sequence[PerformanceSpan]) -> None:
    for span in spans:
        if "CB_CHANGE" not in span.context_text and "CB_OPER" not in span.context_text:
            continue
        end_text = span.end_free_text.lower()
        if "internal" in end_text:
            span.response_category = "internal"
        elif "external" in end_text or "allowed" in end_text or "changed" in end_text:
            span.response_category = "external"


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


def _annotate_reset_trigger_lineage(spans: Sequence[PerformanceSpan]) -> None:
    reset_spans = [
        span
        for span in spans
        if span.context_text == "[RESET_GPT_TRIGGER]" and span.request_id is not None
    ]
    if not reset_spans:
        return

    # Prefer more specific windows first if nested resets ever occur.
    reset_spans.sort(key=lambda span: (span.duration_seconds, span.start_time, span.end_time))
    for span in spans:
        if ("CB_CHANGE" not in span.context_text and "CB_OPER" not in span.context_text) or span.request_id is None:
            continue
        for reset in reset_spans:
            if span.request_id == reset.request_id:
                continue
            if reset.start_time <= span.start_time and span.end_time <= reset.end_time:
                span.reset_trigger_request_id = reset.request_id
                span.reset_trigger_start_time = reset.start_time
                span.reset_trigger_end_time = reset.end_time
                break


def reconstruct_spans(parsed_lines: Sequence[tuple[int, ParsedLogLine]], source: str) -> SpanAnalysisResult:
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
            warnings.append(ParseWarning(source, line_index, f"End without matching Start for {parsed.context_text}"))
            continue

        match_index = None
        for candidate_index in range(len(thread_stack) - 1, -1, -1):
            _, candidate = thread_stack[candidate_index]
            if candidate.context_parts != parsed.context_parts:
                continue
            candidate_req = candidate.request_id
            parsed_req = parsed.request_id
            if candidate_req is not None and parsed_req is not None and candidate_req != parsed_req:
                continue
            match_index = candidate_index
            break

        if match_index is None:
            warnings.append(ParseWarning(source, line_index, f"No matching Start found for End {parsed.context_text}"))
            continue

        unmatched_suffix = thread_stack[match_index + 1:]
        if unmatched_suffix:
            warnings.append(
                ParseWarning(
                    source,
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
            )
            for _, ancestor_event in thread_stack
        ]
        current_frame = SpanFrame(
            context_parts=start_event.context_parts,
            context_text=start_event.context_text,
            fields=start_event.fields,
            free_text=start_event.free_text,
        )

        stack_context_key = " > ".join(frame.signature(False) for frame in [*ancestor_frames, current_frame])
        stack_context_param_key = " > ".join(frame.signature(True) for frame in [*ancestor_frames, current_frame])

        span = PerformanceSpan(
            source=source,
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
            warnings.append(
                ParseWarning(source, line_index, f"Unclosed Start on thread {thread_id} for {event.context_text}"))

    spans.sort(key=lambda span: (span.start_time, span.thread_id, span.start_line_index))
    _annotate_response_categories(spans)
    _annotate_reset_trigger_lineage(spans)
    _annotate_nested_segments(spans)
    return SpanAnalysisResult(spans=spans, warnings=warnings)


def _as_int(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def _initiator_grouping(span: PerformanceSpan, initiator_index: int) -> tuple[str, int | None, int | None]:
    operation = span.context_text.strip("[]")
    iter_index = _as_int(span.start_fields.get("iter"))
    msg_index = _as_int(span.start_fields.get("msg"))
    iter_part = str(iter_index) if iter_index is not None else "na"
    msg_part = str(msg_index) if msg_index is not None else f"idx{initiator_index}"
    return (f"{operation}|iter={iter_part}|msg={msg_part}", iter_index, msg_index)


def _normalize_operation_name(value: str) -> str:
    return value.strip().strip("[]")


def _response_operation_name(span: PerformanceSpan) -> str:
    if span.context_parts:
        return _normalize_operation_name(span.context_parts[0])
    return _normalize_operation_name(span.context_text)


def _normalize_operation_association_rules(
        rules: dict[str, Sequence[str]] | None,
) -> dict[str, tuple[str, ...]]:
    normalized: dict[str, tuple[str, ...]] = {}
    if not rules:
        return normalized

    for initiator_operation, response_operations in rules.items():
        initiator_key = _normalize_operation_name(str(initiator_operation))
        if not initiator_key:
            continue

        normalized_response_operations: list[str] = []
        for response_operation in response_operations:
            response_key = _normalize_operation_name(str(response_operation))
            if response_key and response_key not in normalized_response_operations:
                normalized_response_operations.append(response_key)

        normalized[initiator_key] = tuple(normalized_response_operations)

    return normalized


def _response_pool_key(response_operations: Sequence[str] | None) -> tuple[str, ...] | None:
    if not response_operations:
        return None
    return tuple(
        sorted({_normalize_operation_name(operation) for operation in response_operations if operation.strip()}))


def correlate_spans(
        initiators: Sequence[PerformanceSpan],
        responses: Sequence[PerformanceSpan],
        *,
        initiator_context_pattern: re.Pattern[str] | None = None,
        response_context_pattern: re.Pattern[str] | None = None,
        operation_association_rules: dict[str, Sequence[str]] | None = None,
        correlation_mode: str = "chronological_first",
) -> list[LatencySample]:
    association_rules = _normalize_operation_association_rules(operation_association_rules)

    selected_initiators: list[PerformanceSpan] = []
    for span in initiators:
        if initiator_context_pattern and not initiator_context_pattern.search(span.context_text):
            continue
        selected_initiators.append(span)

    selected_responses: list[PerformanceSpan] = []
    for span in responses:
        if response_context_pattern and not response_context_pattern.search(span.context_text):
            continue
        selected_responses.append(span)

    selected_initiators.sort(key=lambda span: (span.start_time, span.thread_id, span.start_line_index))
    selected_responses.sort(key=lambda span: (span.start_time, span.thread_id, span.start_line_index))

    pool_cache: dict[tuple[str, ...] | None, dict[str, list[PerformanceSpan]]] = {}
    cursor_cache: dict[tuple[str, ...] | None, dict[str, int]] = {}

    def get_pools(pool_key: tuple[str, ...] | None) -> dict[str, list[PerformanceSpan]]:
        cached = pool_cache.get(pool_key)
        if cached is not None:
            return cached

        if pool_key is None:
            base_responses = selected_responses
        else:
            allowed = set(pool_key)
            base_responses = [span for span in selected_responses if _response_operation_name(span) in allowed]

        cached = {
            "all": base_responses,
            "internal": [span for span in base_responses if span.response_category == "internal"],
            "external": [span for span in base_responses if span.response_category == "external"],
        }
        pool_cache[pool_key] = cached
        cursor_cache[pool_key] = {"all": 0, "internal": 0, "external": 0}
        return cached

    samples: list[LatencySample] = []
    for initiator_index, initiator in enumerate(selected_initiators):
        initiator_operation = _normalize_operation_name(initiator.context_text)
        pool_key = _response_pool_key(association_rules.get(initiator_operation))
        pools = get_pools(pool_key)
        cursors = cursor_cache[pool_key]
        group_key, iter_index, msg_index = _initiator_grouping(initiator, initiator_index)
        for category in ("all", "internal", "external"):
            pool = pools[category]
            cursor = cursors[category]
            chosen = None
            while cursor < len(pool):
                candidate = pool[cursor]
                cursor += 1
                if candidate.start_time >= initiator.start_time:
                    chosen = candidate
                    break
            cursors[category] = cursor
            if chosen is None:
                continue

            samples.append(
                LatencySample(
                    operation=initiator_operation,
                    correlation_mode=correlation_mode,
                    response_category=category,
                    response_operation=_response_operation_name(chosen),
                    initiator_group_key=group_key,
                    initiator_iter=iter_index,
                    initiator_msg=msg_index,
                    initiator_index=initiator_index,
                    initiator_start=initiator.start_time,
                    response_start=chosen.start_time,
                    latency_ms=(chosen.start_time - initiator.start_time).total_seconds() * 1000.0,
                    response_request_id=chosen.request_id,
                    heuristic=True,
                    note=(
                        "Chronological first-match after initiator start time"
                        if pool_key is None
                        else f"Chronological first-match after initiator start time with response operations {', '.join(pool_key)}"
                    ),
                )
            )

    return samples
