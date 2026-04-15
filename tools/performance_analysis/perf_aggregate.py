from __future__ import annotations

import argparse
import statistics
import textwrap
from collections import defaultdict
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Sequence

from perf_artifacts import load_spans, read_json, write_csv, write_json
from perf_core import PerformanceSpan

DEFAULT_BASE = Path(__file__).resolve().parent
ROOT_OPERATION = "[EDIT_CANDIDATE]"
COMMIT_OPERATION = "[COMMIT]"
RESET_OPERATION = "[RESET_GPT_TRIGGER]"


def percentile(values: Sequence[float], pct: float) -> float:
    if not values:
        return float("nan")
    if len(values) == 1:
        return float(values[0])
    ordered = sorted(values)
    rank = (len(ordered) - 1) * pct
    low = int(rank)
    high = min(low + 1, len(ordered) - 1)
    if low == high:
        return float(ordered[low])
    low_value = ordered[low]
    high_value = ordered[high]
    return float(low_value + (high_value - low_value) * (rank - low))


def _as_message_index(value: str | None) -> int | str | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError:
        return value


def _stats(values: Sequence[float]) -> dict[str, float | int]:
    if not values:
        return {
            "count": 0,
            "min": float("nan"),
            "max": float("nan"),
            "mean": float("nan"),
            "median": float("nan"),
            "stdev": float("nan"),
            "p90": float("nan"),
            "p95": float("nan"),
            "p99": float("nan"),
        }
    return {
        "count": len(values),
        "min": float(min(values)),
        "max": float(max(values)),
        "mean": float(statistics.fmean(values)),
        "median": float(statistics.median(values)),
        "stdev": float(statistics.pstdev(values)) if len(values) > 1 else 0.0,
        "p90": percentile(values, 0.90),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
    }


@dataclass(slots=True)
class NormalizedSpan:
    function_key: str
    function_label: str
    depth: int
    start_ms: float
    end_ms: float
    duration_ms: float
    thread_id: int
    request_id: int | None
    start_line_index: int
    end_line_index: int
    occurrence_in_thread: int = 0
    occurrence_in_section: int = 0
    reset_section_index: int = 0
    reset_anchor_request_id: int | None = None


@dataclass(slots=True)
class TraceInstance:
    trace_key: str
    message_index: int | str | None
    caller_stack_key: str
    caller_stack_with_params_key: str
    iteration: int | None
    thread_id: int
    root_start_time: str
    root_end_time: str
    root_duration_ms: float
    request_id: int | None
    request_lineage_ids: list[int] = field(default_factory=list)
    spans: list[NormalizedSpan] = field(default_factory=list)


@dataclass(slots=True)
class FunctionStats:
    function_key: str
    function_label: str
    count: int
    start_ms: dict[str, float | int]
    end_ms: dict[str, float | int]
    duration_ms: dict[str, float | int]


@dataclass(slots=True)
class SpanInstanceStats:
    function_key: str
    function_label: str
    thread_id: int
    request_id: int | None
    depth: int
    occurrence_in_thread: int
    occurrence_in_section: int
    reset_section_index: int
    reset_anchor_request_id: int | None
    count: int
    start_ms: dict[str, float | int]
    end_ms: dict[str, float | int]
    duration_ms: dict[str, float | int]
    mean_start_ms: float
    mean_end_ms: float


@dataclass(slots=True)
class AggregateGroup:
    title: str | None
    message_index: int | str | None
    caller_stack_key: str
    caller_stack_with_params_key: str
    caller_stack: tuple[str, ...]
    caller_stack_with_params: tuple[str, ...]
    trace_count: int
    request_ids_by_category: dict[str, list[int]]
    traces: list[TraceInstance] = field(default_factory=list)
    function_stats: list[FunctionStats] = field(default_factory=list)


def _trace_group_key(message_index: int | str | None, caller_stack_with_params_key: str) -> str:
    message_part = "na" if message_index is None else str(message_index)
    return f"msg={message_part}|stack={caller_stack_with_params_key}"


def _join_stack(parts: Sequence[str]) -> str:
    return " > ".join(parts)


def _safe_filename_part(value: str) -> str:
    cleaned = [char if char.isalnum() or char in {"-", "_"} else "_" for char in value]
    text = "".join(cleaned).strip("_")
    return text or "root"


def _request_id_sort_value(value: int | None) -> int:
    return 0 if value is None else value


def _section_sort_value(value: int | None) -> int:
    return -1 if value is None else value


def _span_sort_key(span: NormalizedSpan) -> tuple[int, float, int, int, int, str]:
    return (
        span.reset_section_index,
        span.start_ms,
        span.occurrence_in_section,
        _request_id_sort_value(span.request_id),
        span.depth,
        span.function_key,
    )


def _span_instance_sort_key(instance: SpanInstanceStats) -> tuple[int, float, int, int, int, str]:
    return (
        instance.reset_section_index,
        instance.mean_start_ms,
        instance.occurrence_in_section,
        _request_id_sort_value(instance.request_id),
        instance.depth,
        instance.function_key,
    )


def _assign_lanes(intervals: Sequence[tuple[float, float]]) -> list[int]:
    lane_ends: list[float] = []
    lane_by_index = [0 for _ in intervals]
    ordered_indexes = sorted(range(len(intervals)), key=lambda idx: (intervals[idx][0], intervals[idx][1], idx))

    for index in ordered_indexes:
        start, end = intervals[index]
        assigned_lane = None
        for lane_index, lane_end in enumerate(lane_ends):
            if start >= lane_end:
                assigned_lane = lane_index
                lane_ends[lane_index] = end
                break
        if assigned_lane is None:
            assigned_lane = len(lane_ends)
            lane_ends.append(end)
        lane_by_index[index] = assigned_lane

    return lane_by_index


def _plot_label(value: str, width: int = 28) -> str:
    lines = textwrap.wrap(value, width=width, break_long_words=False, break_on_hyphens=False)
    return "\n".join(lines) if lines else value


def _build_initiator_group_key(root: PerformanceSpan) -> str:
    iter_part = root.start_fields.get("iter", "na")
    msg_part = root.start_fields.get("msg", "na")
    return f"EDIT_CANDIDATE|iter={iter_part}|msg={msg_part}"


def _load_reset_trigger_map(extract_dir: Path) -> dict[int, dict[str, object]]:
    path = extract_dir / "reset_trigger_map.json"
    if not path.exists():
        return {}

    payload = read_json(path)
    if not isinstance(payload, dict):
        return {}
    raw_map = payload.get("by_request_id")
    if not isinstance(raw_map, dict):
        return {}

    out: dict[int, dict[str, object]] = {}
    for key, value in raw_map.items():
        if not isinstance(value, dict):
            continue
        try:
            request_id = int(key)
        except ValueError:
            continue
        spawned = value.get("spawned_internal_request_ids", [])
        if not isinstance(spawned, list):
            spawned = []
        out[request_id] = {
            "reset_start": value.get("reset_start"),
            "reset_end": value.get("reset_end"),
            "spawned_internal_request_ids": [int(item) for item in spawned],
        }
    return out


def _build_request_lineage(seed_request_ids: Sequence[int], reset_map: dict[int, dict[str, object]]) -> set[int]:
    lineage: set[int] = set(int(value) for value in seed_request_ids)
    queue = list(lineage)
    while queue:
        current = queue.pop(0)
        entry = reset_map.get(current)
        if entry is None:
            continue
        for spawned in entry.get("spawned_internal_request_ids", []):
            spawned_id = int(spawned)
            if spawned_id in lineage:
                continue
            lineage.add(spawned_id)
            queue.append(spawned_id)
    return lineage


def _collect_nested_members(anchor: PerformanceSpan, all_spans: Sequence[PerformanceSpan], lower: datetime,
                            upper: datetime) -> list[PerformanceSpan]:
    return [
        span
        for span in all_spans
        if span.thread_id == anchor.thread_id
           and span.start_time >= lower
           and span.end_time <= upper
           and span.start_time >= anchor.start_time
           and span.end_time <= anchor.end_time
    ]


def _collect_trace_spans(
        *,
        root: PerformanceSpan,
        all_spans: Sequence[PerformanceSpan],
        request_lineage: set[int],
        trace_end: datetime,
) -> list[PerformanceSpan]:
    anchors = [
        span
        for span in all_spans
        if span.request_id is not None
           and span.request_id in request_lineage
           and span.start_time >= root.start_time
           and span.end_time <= trace_end
    ]

    root_iter = root.start_fields.get("iter")
    root_msg = root.start_fields.get("msg")
    client_roots = [
        span
        for span in all_spans
        if span.source == "client"
           and span.thread_id == root.thread_id
           and span.start_time >= root.start_time
           and span.end_time <= trace_end
           and span.start_fields.get("iter") == root_iter
           and span.start_fields.get("msg") == root_msg
           and span.context_text in {ROOT_OPERATION, COMMIT_OPERATION}
    ]

    members: dict[tuple[str, int, int, int], PerformanceSpan] = {}
    for anchor in [*anchors, *client_roots]:
        key = (anchor.source, anchor.thread_id, anchor.start_line_index, anchor.end_line_index)
        members[key] = anchor
        for nested in _collect_nested_members(anchor, all_spans, root.start_time, trace_end):
            nested_key = (nested.source, nested.thread_id, nested.start_line_index, nested.end_line_index)
            members[nested_key] = nested

    ordered = list(members.values())
    ordered.sort(key=lambda span: (span.start_time, span.end_time, span.start_line_index))
    return ordered


def _load_request_ids_by_group(correlate_dir: Path) -> dict[str, dict[str, list[int]]]:
    latency_groups_path = correlate_dir / "latency_groups.json"
    if not latency_groups_path.exists():
        return {}

    payload = read_json(latency_groups_path)
    if not isinstance(payload, list):
        return {}

    lookup: dict[str, dict[str, list[int]]] = {}
    for item in payload:
        if not isinstance(item, dict):
            continue
        operation = str(item.get("operation", ""))
        initiator_group_key = str(item.get("initiator_group_key", ""))
        request_ids_by_category = item.get("request_ids_by_category", {})
        if not operation or not initiator_group_key or not isinstance(request_ids_by_category, dict):
            continue
        lookup[initiator_group_key] = {
            category: [int(value) for value in values]
            for category, values in request_ids_by_category.items()
            if isinstance(values, list)
        }
    return lookup


def _seed_request_ids_for_root(root: PerformanceSpan, request_lookup: dict[str, dict[str, list[int]]]) -> list[int]:
    key = _build_initiator_group_key(root)
    categories = request_lookup.get(key, {})
    seeds: set[int] = set()
    for values in categories.values():
        for value in values:
            seeds.add(int(value))
    if not seeds and root.request_id is not None:
        seeds.add(root.request_id)
    return sorted(seeds)


def _derive_trace_end(root: PerformanceSpan, spans: Sequence[PerformanceSpan], request_lineage: set[int]) -> datetime:
    end_time = root.end_time
    for span in spans:
        if span.request_id is None:
            continue
        if span.request_id not in request_lineage:
            continue
        if span.start_time < root.start_time:
            continue
        if span.end_time > end_time:
            end_time = span.end_time
    return end_time


def _build_trace_reset_windows(trace_spans: Sequence[PerformanceSpan]) -> list[tuple[datetime, datetime, int | None]]:
    windows: list[tuple[datetime, datetime, int | None]] = []
    for span in trace_spans:
        if RESET_OPERATION not in span.context_text:
            continue
        windows.append((span.start_time, span.end_time, span.request_id))
    windows.sort(key=lambda item: (item[1], item[0]))
    return windows


def _build_spawned_to_reset_root(reset_map: dict[int, dict[str, object]]) -> dict[int, int]:
    spawned_to_root: dict[int, int] = {}
    for root_request_id, entry in reset_map.items():
        for spawned in entry.get("spawned_internal_request_ids", []):
            spawned_to_root[int(spawned)] = int(root_request_id)
    return spawned_to_root


def _span_reset_section(
        span: PerformanceSpan,
        *,
        reset_windows: Sequence[tuple[datetime, datetime, int | None]],
        spawned_to_root: dict[int, int],
) -> tuple[int, int | None]:
    if not reset_windows:
        return 0, None

    reset_window_index_by_request: dict[int, int] = {
        int(reset_request_id): index
        for index, (_, _, reset_request_id) in enumerate(reset_windows)
        if reset_request_id is not None
    }

    for index, (reset_start, reset_end, reset_request_id) in enumerate(reset_windows):
        if RESET_OPERATION in span.context_text and span.start_time == reset_start and span.end_time == reset_end:
            return 2 * index + 1, reset_request_id

    anchor_request_id: int | None = None
    if span.reset_trigger_request_id is not None:
        anchor_request_id = int(span.reset_trigger_request_id)
    elif span.request_id is not None:
        anchor_request_id = spawned_to_root.get(int(span.request_id))

    if anchor_request_id is not None and anchor_request_id in reset_window_index_by_request:
        anchor_index = reset_window_index_by_request[anchor_request_id]
        return 2 * anchor_index + 2, anchor_request_id

    for index, (reset_start, reset_end, reset_request_id) in enumerate(reset_windows):
        if span.start_time >= reset_start and span.end_time <= reset_end:
            return 2 * index + 1, reset_request_id

    completed = [item for item in reset_windows if span.start_time >= item[1]]
    if not completed:
        return 0, None

    last_index = len(completed) - 1
    return 2 * last_index + 2, completed[last_index][2]


def _summarise_group(group: AggregateGroup) -> None:
    function_entries: dict[str, dict[str, list[float] | str]] = defaultdict(lambda: {
        "label": "",
        "start": [],
        "end": [],
        "duration": [],
    })

    for trace in group.traces:
        for span in trace.spans:
            entry = function_entries[span.function_key]
            entry["label"] = span.function_label
            entry["start"].append(span.start_ms)
            entry["end"].append(span.end_ms)
            entry["duration"].append(span.duration_ms)

    stats: list[FunctionStats] = []
    for function_key, entry in sorted(function_entries.items()):
        stats.append(
            FunctionStats(
                function_key=function_key,
                function_label=str(entry["label"]),
                count=len(entry["duration"]),
                start_ms=_stats(entry["start"]),
                end_ms=_stats(entry["end"]),
                duration_ms=_stats(entry["duration"]),
            )
        )
    group.function_stats = stats


def _build_span_instance_stats(group: AggregateGroup) -> list[SpanInstanceStats]:
    grouped_instances: dict[tuple[int, int, str, int], list[NormalizedSpan]] = defaultdict(list)
    for trace in group.traces:
        for span in trace.spans:
            grouped_instances[
                (span.reset_section_index, span.thread_id, span.function_key, span.occurrence_in_section)].append(span)

    rows: list[SpanInstanceStats] = []
    for (reset_section_index, thread_id, function_key, occurrence_in_section), spans_for_instance in sorted(
            grouped_instances.items(),
            key=lambda item: (
                    item[0][0],
                    min(span.start_ms for span in item[1]),
                    item[0][1],
                    item[0][3],
                    item[0][2],
            ),
    ):
        example = spans_for_instance[0]
        start_values = [span.start_ms for span in spans_for_instance]
        end_values = [span.end_ms for span in spans_for_instance]
        duration_values = [span.duration_ms for span in spans_for_instance]
        rows.append(
            SpanInstanceStats(
                function_key=function_key,
                function_label=example.function_label,
                thread_id=thread_id,
                request_id=example.request_id,
                depth=example.depth,
                occurrence_in_thread=example.occurrence_in_thread,
                occurrence_in_section=occurrence_in_section,
                reset_section_index=reset_section_index,
                reset_anchor_request_id=example.reset_anchor_request_id,
                count=len(spans_for_instance),
                start_ms=_stats(start_values),
                end_ms=_stats(end_values),
                duration_ms=_stats(duration_values),
                mean_start_ms=float(statistics.fmean(start_values)),
                mean_end_ms=float(statistics.fmean(end_values)),
            )
        )
    return rows


def build_aggregate_groups(
        spans: Sequence[PerformanceSpan],
        request_lookup: dict[str, dict[str, list[int]]] | None = None,
        reset_trigger_map: dict[int, dict[str, object]] | None = None,
) -> list[AggregateGroup]:
    request_lookup = request_lookup or {}
    reset_trigger_map = reset_trigger_map or {}
    spawned_to_root = _build_spawned_to_reset_root(reset_trigger_map)

    ordered_spans = sorted(spans, key=lambda span: (span.start_time, span.end_time, span.start_line_index))

    roots = [
        span
        for span in spans
        if span.source == "client" and span.context_text == ROOT_OPERATION
    ]
    roots.sort(key=lambda span: (span.start_time, span.thread_id, span.start_line_index))

    grouped: dict[tuple[int | str | None, tuple[str, ...]], AggregateGroup] = {}
    for root in roots:
        message_index = _as_message_index(root.start_fields.get("msg"))
        group_key = (message_index, root.caller_stack_with_params)
        caller_stack_key = _join_stack(root.caller_stack)
        caller_stack_with_params_key = _join_stack(root.caller_stack_with_params)
        trace_group_key = _trace_group_key(message_index, caller_stack_with_params_key)
        seed_request_ids = _seed_request_ids_for_root(root, request_lookup)
        request_lineage = _build_request_lineage(seed_request_ids, reset_trigger_map)
        trace_end = _derive_trace_end(root, ordered_spans, request_lineage)
        trace_spans = _collect_trace_spans(
            root=root,
            all_spans=ordered_spans,
            request_lineage=request_lineage,
            trace_end=trace_end,
        )
        reset_windows = _build_trace_reset_windows(trace_spans)

        occurrence_by_thread_and_key: dict[tuple[int, str], int] = {}
        occurrence_by_section_thread_and_key: dict[tuple[int, int, str], int] = {}
        normalized_spans = []
        for span in trace_spans:
            reset_section_index, reset_anchor_request_id = _span_reset_section(
                span,
                reset_windows=reset_windows,
                spawned_to_root=spawned_to_root,
            )
            key = (span.thread_id, span.stack_context_key)
            occurrence = occurrence_by_thread_and_key.get(key, 0)
            occurrence_by_thread_and_key[key] = occurrence + 1

            section_key = (reset_section_index, span.thread_id, span.stack_context_key)
            section_occurrence = occurrence_by_section_thread_and_key.get(section_key, 0)
            occurrence_by_section_thread_and_key[section_key] = section_occurrence + 1
            normalized_spans.append(
                NormalizedSpan(
                    function_key=span.stack_context_key,
                    function_label=span.context_text,
                    depth=len(span.caller_stack),
                    start_ms=(span.start_time - root.start_time).total_seconds() * 1000.0,
                    end_ms=(span.end_time - root.start_time).total_seconds() * 1000.0,
                    duration_ms=span.duration_seconds * 1000.0,
                    thread_id=span.thread_id,
                    request_id=span.request_id,
                    start_line_index=span.start_line_index,
                    end_line_index=span.end_line_index,
                    occurrence_in_thread=occurrence,
                    occurrence_in_section=section_occurrence,
                    reset_section_index=reset_section_index,
                    reset_anchor_request_id=reset_anchor_request_id,
                )
            )

        iteration_value = _as_message_index(root.start_fields.get("iter"))
        trace_instance = TraceInstance(
            trace_key=trace_group_key,
            message_index=message_index,
            caller_stack_key=caller_stack_key,
            caller_stack_with_params_key=caller_stack_with_params_key,
            iteration=iteration_value if isinstance(iteration_value, int) else None,
            thread_id=root.thread_id,
            root_start_time=root.start_time.isoformat(timespec="microseconds"),
            root_end_time=root.end_time.isoformat(timespec="microseconds"),
            root_duration_ms=root.duration_seconds * 1000.0,
            request_id=root.request_id,
            request_lineage_ids=sorted(request_lineage),
            spans=normalized_spans,
        )

        group = grouped.get(group_key)
        if group is None:
            request_ids_by_category = request_lookup.get(_build_initiator_group_key(root), {})
            group = AggregateGroup(
                title=None,
                message_index=message_index,
                caller_stack_key=caller_stack_key,
                caller_stack_with_params_key=caller_stack_with_params_key,
                caller_stack=root.caller_stack,
                caller_stack_with_params=root.caller_stack_with_params,
                trace_count=0,
                request_ids_by_category={category: list(values) for category, values in
                                         request_ids_by_category.items()},
            )
            grouped[group_key] = group

        group.traces.append(trace_instance)
        group.trace_count += 1

        if trace_instance.request_id is not None:
            group.request_ids_by_category.setdefault("all", [])
            if trace_instance.request_id not in group.request_ids_by_category["all"]:
                group.request_ids_by_category["all"].append(trace_instance.request_id)

    for group in grouped.values():
        for category, values in group.request_ids_by_category.items():
            group.request_ids_by_category[category] = sorted(set(values))
        _summarise_group(group)

    return sorted(grouped.values(),
                  key=lambda item: (-item.trace_count, str(item.message_index), item.caller_stack_with_params_key))


def _group_function_rows(group: AggregateGroup) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for function in group.function_stats:
        rows.append(
            {
                "message_index": group.message_index,
                "caller_stack_key": group.caller_stack_key,
                "caller_stack_with_params_key": group.caller_stack_with_params_key,
                "trace_count": group.trace_count,
                "function_key": function.function_key,
                "function_label": function.function_label,
                "count": function.count,
                "start_min_ms": function.start_ms["min"],
                "start_max_ms": function.start_ms["max"],
                "start_mean_ms": function.start_ms["mean"],
                "start_median_ms": function.start_ms["median"],
                "start_stdev_ms": function.start_ms["stdev"],
                "start_p90_ms": function.start_ms["p90"],
                "start_p95_ms": function.start_ms["p95"],
                "start_p99_ms": function.start_ms["p99"],
                "end_min_ms": function.end_ms["min"],
                "end_max_ms": function.end_ms["max"],
                "end_mean_ms": function.end_ms["mean"],
                "end_median_ms": function.end_ms["median"],
                "end_stdev_ms": function.end_ms["stdev"],
                "end_p90_ms": function.end_ms["p90"],
                "end_p95_ms": function.end_ms["p95"],
                "end_p99_ms": function.end_ms["p99"],
                "duration_min_ms": function.duration_ms["min"],
                "duration_max_ms": function.duration_ms["max"],
                "duration_mean_ms": function.duration_ms["mean"],
                "duration_median_ms": function.duration_ms["median"],
                "duration_stdev_ms": function.duration_ms["stdev"],
                "duration_p90_ms": function.duration_ms["p90"],
                "duration_p95_ms": function.duration_ms["p95"],
                "duration_p99_ms": function.duration_ms["p99"],
            }
        )
    return rows


def _group_span_instance_rows(group_index: int, group: AggregateGroup) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for instance in _build_span_instance_stats(group):
        rows.append(
            {
                "group_index": group_index,
                "message_index": group.message_index,
                "caller_stack_key": group.caller_stack_key,
                "caller_stack_with_params_key": group.caller_stack_with_params_key,
                "trace_count": group.trace_count,
                "thread_id": instance.thread_id,
                "request_id": instance.request_id,
                "reset_section_index": instance.reset_section_index,
                "reset_anchor_request_id": instance.reset_anchor_request_id,
                "function_key": instance.function_key,
                "function_label": instance.function_label,
                "depth": instance.depth,
                "occurrence_in_thread": instance.occurrence_in_thread,
                "occurrence_in_section": instance.occurrence_in_section,
                "count": instance.count,
                "start_min_ms": instance.start_ms["min"],
                "start_max_ms": instance.start_ms["max"],
                "start_mean_ms": instance.start_ms["mean"],
                "start_median_ms": instance.start_ms["median"],
                "start_stdev_ms": instance.start_ms["stdev"],
                "start_p90_ms": instance.start_ms["p90"],
                "start_p95_ms": instance.start_ms["p95"],
                "start_p99_ms": instance.start_ms["p99"],
                "end_min_ms": instance.end_ms["min"],
                "end_max_ms": instance.end_ms["max"],
                "end_mean_ms": instance.end_ms["mean"],
                "end_median_ms": instance.end_ms["median"],
                "end_stdev_ms": instance.end_ms["stdev"],
                "end_p90_ms": instance.end_ms["p90"],
                "end_p95_ms": instance.end_ms["p95"],
                "end_p99_ms": instance.end_ms["p99"],
                "duration_min_ms": instance.duration_ms["min"],
                "duration_max_ms": instance.duration_ms["max"],
                "duration_mean_ms": instance.duration_ms["mean"],
                "duration_median_ms": instance.duration_ms["median"],
                "duration_stdev_ms": instance.duration_ms["stdev"],
                "duration_p90_ms": instance.duration_ms["p90"],
                "duration_p95_ms": instance.duration_ms["p95"],
                "duration_p99_ms": instance.duration_ms["p99"],
                "mean_start_ms": instance.mean_start_ms,
                "mean_end_ms": instance.mean_end_ms,
            }
        )
    return rows


def _span_rows(group: AggregateGroup) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for trace in group.traces:
        for span in trace.spans:
            rows.append(
                {
                    "message_index": group.message_index,
                    "caller_stack_key": group.caller_stack_key,
                    "caller_stack_with_params_key": group.caller_stack_with_params_key,
                    "trace_key": trace.trace_key,
                    "thread_id": span.thread_id,
                    "iteration": trace.iteration,
                    "request_id": span.request_id,
                    "reset_section_index": span.reset_section_index,
                    "reset_anchor_request_id": span.reset_anchor_request_id,
                    "function_key": span.function_key,
                    "function_label": span.function_label,
                    "depth": span.depth,
                    "occurrence_in_thread": span.occurrence_in_thread,
                    "occurrence_in_section": span.occurrence_in_section,
                    "start_ms": span.start_ms,
                    "end_ms": span.end_ms,
                    "duration_ms": span.duration_ms,
                    "start_line_index": span.start_line_index,
                    "end_line_index": span.end_line_index,
                }
            )
    return rows


def _plot_group(group_index: int, group: AggregateGroup, output_path: Path, *, show_thread_labels: bool) -> Path | None:
    try:
        import matplotlib.pyplot as plt
    except Exception:
        return None

    instance_stats = _build_span_instance_stats(group)
    if not instance_stats:
        return None

    section_rows: dict[tuple[int, int], list[SpanInstanceStats]] = defaultdict(list)
    for instance in instance_stats:
        section_rows[(instance.reset_section_index, instance.thread_id)].append(instance)
    for rows in section_rows.values():
        rows.sort(key=_span_instance_sort_key)

    block_gap = 0.72
    row_step = 0.34
    top_padding = 0.24
    bottom_padding = 0.18
    bar_height = 0.20
    thread_sections: list[tuple[int, int, float, float, list[SpanInstanceStats]]] = []
    y_cursor = 0.0
    section_order = sorted(
        section_rows.items(),
        key=lambda item: (
            item[0][0],
            min((row.mean_start_ms for row in item[1]), default=float("inf")),
            item[0][1],
        ),
    )
    for (reset_section_index, thread_id), rows in section_order:
        section_height = top_padding + bottom_padding + max(1, len(rows)) * row_step
        thread_top = y_cursor
        thread_sections.append((reset_section_index, thread_id, thread_top, thread_top + section_height, rows))
        y_cursor += section_height + block_gap

    fig_height = max(4.0, 0.9 + 0.55 * sum(len(rows) for _, _, _, _, rows in thread_sections))
    fig, ax = plt.subplots(1, 1, figsize=(20, fig_height), sharex=False)

    depth_colors = {
        0: "#1f77b4",
        1: "#2ca02c",
        2: "#ff7f0e",
        3: "#d62728",
    }

    for reset_section_index, thread_id, thread_top, thread_bottom, rows in thread_sections:
        section_tone = (reset_section_index + thread_id) % 2
        ax.axhspan(thread_top, thread_bottom, facecolor="#f7f7f7" if section_tone == 0 else "#efefef", alpha=0.35,
                   zorder=0)
        if show_thread_labels:
            center = thread_top + (thread_bottom - thread_top) / 2.0
            ax.text(
                -0.08,
                center,
                f"section={reset_section_index} | thread={thread_id}",
                ha="right",
                va="center",
                fontsize=8,
                fontweight="bold",
                transform=ax.get_yaxis_transform(),
            )
        for row_index, row in enumerate(rows):
            y = thread_top + top_padding + row_index * row_step
            color = depth_colors.get(row.depth, "#7f7f7f")
            mean_start = row.mean_start_ms
            mean_end = row.mean_end_ms
            duration = max(mean_end - mean_start, 0.02)
            ax.broken_barh(
                [(mean_start, duration)],
                (y, bar_height),
                facecolors=color,
                alpha=0.9,
                edgecolors="#f0f0f0",
                linewidth=0.4,
            )
            # Start time range: min to max
            ax.hlines(y + bar_height / 2.0, row.start_ms["min"], row.start_ms["max"], colors=color, alpha=0.3,
                      linewidth=2.5, linestyles="solid")
            # End time range: min to max
            ax.hlines(y + bar_height / 2.0, row.end_ms["min"], row.end_ms["max"], colors=color, alpha=0.2,
                      linewidth=2.0, linestyles="solid")
            label = row.function_label
            label += f" (#{row.occurrence_in_section + 1})"
            if len(label) > 42:
                label = _plot_label(label, width=30)
            ax.text(-0.01, y + bar_height / 2.0, label, ha="right", va="center", fontsize=7,
                    transform=ax.get_yaxis_transform(), linespacing=0.9)

    ax.axvline(0.0, color="#444444", linestyle="--", linewidth=1.0)
    ax.set_ylim(max(y_cursor, 1.0), 0.0)
    ax.set_yticks([])
    ax.set_xlabel("Normalized time from EDIT_CANDIDATE start (ms)", fontsize=10)
    title_prefix = group.title if group.title else f"msg={group.message_index}"
    title_text = f"group={group_index} | {title_prefix} | traces={group.trace_count} | {group.caller_stack_with_params_key}"
    ax.set_title(title_text, fontsize=10, pad=10)
    ax.grid(axis="x", color="#cccccc", linestyle=":", linewidth=0.7, alpha=0.7)

    # Add legend
    legend_text = "Bars: mean start/end times | Thick line: start range (min-max) | Thin line: end range (min-max)"
    ax.text(0.02, 0.02, legend_text, transform=ax.transAxes, fontsize=8, verticalalignment="bottom",
            bbox=dict(boxstyle="round", facecolor="#f9f9f9", alpha=0.8, edgecolor="#cccccc"))

    fig.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=170)
    plt.close(fig)
    return output_path


def _extract_reset_end_times(group: AggregateGroup) -> dict[int, float]:
    """Extract RESET_GPT_TRIGGER end times for each trace.
    
    Returns: dict[trace_index] -> reset_end_time_ms (normalized from EDIT_CANDIDATE start)
    """
    reset_times: dict[int, float] = {}
    for trace_idx, trace in enumerate(group.traces):
        for span in trace.spans:
            if RESET_OPERATION in span.function_label:
                reset_times[trace_idx] = span.end_ms
                break
    return reset_times


def _get_reset_percentile_traces(reset_times: dict[int, float]) -> dict[str, int]:
    """Find trace indices at min, median, and max RESET end times.
    
    Returns: dict with keys "min", "median", "max" containing trace indices
    """
    if not reset_times:
        return {}

    values = sorted(reset_times.values())
    trace_map = {v: k for k, v in reset_times.items()}

    result: dict[str, int] = {}
    result["min"] = trace_map[min(values)]
    result["max"] = trace_map[max(values)]
    if len(values) > 1:
        median_value = statistics.median(values)
        # Find trace closest to median
        closest_idx = min(range(len(values)), key=lambda i: abs(values[i] - median_value))
        result["median"] = trace_map[values[closest_idx]]

    return result


def _plot_trace_detail(
        group_index: int,
        trace_index: int,
        trace: TraceInstance,
        group: AggregateGroup,
        label_suffix: str,
        output_path: Path,
        *,
        show_thread_labels: bool,
) -> Path | None:
    """Plot a single trace instance with all its spans detailed."""
    try:
        import matplotlib.pyplot as plt
    except Exception:
        return None

    if not trace.spans:
        return None

    # Group spans by reset-section and thread
    thread_spans: dict[tuple[int, int], list[NormalizedSpan]] = defaultdict(list)
    for span in trace.spans:
        thread_spans[(span.reset_section_index, span.thread_id)].append(span)

    for spans in thread_spans.values():
        spans.sort(key=_span_sort_key)

    block_gap = 0.8
    row_step = 0.38
    top_padding = 0.2
    bottom_padding = 0.22
    bar_height = 0.22
    thread_sections: list[tuple[int, int, float, float, list[NormalizedSpan]]] = []
    y_cursor = 0.0
    section_order = sorted(
        thread_spans.items(),
        key=lambda item: (
            item[0][0],
            min((span.start_ms for span in item[1]), default=float("inf")),
            item[0][1],
        ),
    )

    for (reset_section_index, thread_id), spans in section_order:
        thread_top = y_cursor
        section_height = max(1.0, top_padding + bottom_padding + max(1, len(spans)) * row_step)
        thread_sections.append((reset_section_index, thread_id, thread_top, thread_top + section_height, spans))
        y_cursor += section_height + block_gap

    total_spans = sum(len(spans) for spans in thread_spans.values())
    fig_height = max(4.0, 1.0 + 0.45 * total_spans)
    fig, ax = plt.subplots(1, 1, figsize=(20, fig_height), sharex=False)

    depth_colors = {
        0: "#1f77b4",
        1: "#2ca02c",
        2: "#ff7f0e",
        3: "#d62728",
        4: "#9467bd",
    }

    for reset_section_index, thread_id, thread_top, thread_bottom, spans in thread_sections:
        section_tone = (reset_section_index + thread_id) % 2
        ax.axhspan(thread_top, thread_bottom, facecolor="#f7f7f7" if section_tone == 0 else "#efefef", alpha=0.4,
                   zorder=0)
        if show_thread_labels:
            center = thread_top + (thread_bottom - thread_top) / 2.0
            ax.text(
                -0.08,
                center,
                f"section={reset_section_index} | thread={thread_id}",
                ha="right",
                va="center",
                fontsize=8,
                fontweight="bold",
                transform=ax.get_yaxis_transform(),
            )

        for row_index, span in enumerate(spans):
            y = thread_top + top_padding + row_index * row_step
            color = depth_colors.get(span.depth, "#7f7f7f")
            duration = max(span.end_ms - span.start_ms, 0.02)
            ax.broken_barh(
                [(span.start_ms, duration)],
                (y, bar_height),
                facecolors=color,
                alpha=0.85,
                edgecolors="#f0f0f0",
                linewidth=0.5,
            )
            label = span.function_label
            label += f" (#{span.occurrence_in_section + 1})"
            if len(label) > 40:
                label = _plot_label(label, width=24)
            ax.text(-0.01, y + bar_height / 2.0, label, ha="right", va="center", fontsize=6.5,
                    transform=ax.get_yaxis_transform(), linespacing=0.9)

    ax.axvline(0.0, color="#444444", linestyle="--", linewidth=1.0)
    ax.set_ylim(max(y_cursor, 1.0), 0.0)
    ax.set_yticks([])
    ax.set_xlabel("Normalized time from EDIT_CANDIDATE start (ms)", fontsize=10)
    title_prefix = group.title if group.title else f"msg={group.message_index}"
    ax.set_title(
        f"group={group_index} | trace={trace_index} | {label_suffix} | {title_prefix}",
        fontsize=10,
        pad=10,
    )
    ax.grid(axis="x", color="#cccccc", linestyle=":", linewidth=0.7, alpha=0.7)

    fig.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=170)
    plt.close(fig)
    return output_path


def _plot_group_reset_percentiles(group_index: int, group: AggregateGroup, output_dir: Path, *,
                                  show_thread_labels: bool) -> list[str]:
    """Plot traces at min/max/median RESET_GPT_TRIGGER end times."""
    reset_times = _extract_reset_end_times(group)
    if not reset_times:
        return []

    percentile_traces = _get_reset_percentile_traces(reset_times)
    plot_paths: list[str] = []

    message_part = f"msg_{group.message_index if group.message_index is not None else 'na'}"
    stack_part = _safe_filename_part(group.caller_stack_with_params_key or "root")

    for percentile_name in ["min", "median", "max"]:
        if percentile_name not in percentile_traces:
            continue

        trace_idx = percentile_traces[percentile_name]
        if trace_idx >= len(group.traces):
            continue

        trace = group.traces[trace_idx]
        output_path = output_dir / f"aggregate_group_{group_index:03d}_{message_part}_{stack_part}_reset_{percentile_name}.png"
        plot = _plot_trace_detail(group_index, trace_idx, trace, group, f"RESET_end_{percentile_name}", output_path,
                                  show_thread_labels=show_thread_labels)
        if plot is not None:
            plot_paths.append(str(plot))

    return plot_paths


def _plot_all_groups(groups: Sequence[AggregateGroup], output_dir: Path, *, show_thread_labels: bool) -> list[str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    plot_paths: list[str] = []
    for index, group in enumerate(groups):
        message_part = f"msg_{group.message_index if group.message_index is not None else 'na'}"
        stack_part = _safe_filename_part(group.caller_stack_with_params_key or "root")
        title_part = _safe_filename_part(group.title) if group.title else stack_part
        path = output_dir / f"aggregate_group_{index:03d}_{message_part}_{title_part}.png"
        plot = _plot_group(index, group, path, show_thread_labels=show_thread_labels)
        if plot is not None:
            plot_paths.append(str(plot))

        # Also generate plots for min/max/median RESET traces
        percentile_plots = _plot_group_reset_percentiles(index, group, output_dir,
                                                         show_thread_labels=show_thread_labels)
        plot_paths.extend(percentile_plots)

    return plot_paths


def aggregate_from_artifacts(
        *,
        extract_dir: Path,
        correlate_dir: Path,
        output_dir: Path,
        show_thread_labels: bool = True,
) -> dict[str, object]:
    spans_path = extract_dir / "spans.json"
    if not spans_path.exists():
        raise FileNotFoundError(f"Missing extraction artifact: {spans_path}")

    spans = load_spans(spans_path)
    request_lookup = _load_request_ids_by_group(correlate_dir)
    reset_trigger_map = _load_reset_trigger_map(extract_dir)
    groups = build_aggregate_groups(
        spans,
        request_lookup=request_lookup,
        reset_trigger_map=reset_trigger_map,
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    summary_rows: list[dict[str, object]] = []
    span_instance_rows: list[dict[str, object]] = []
    span_rows: list[dict[str, object]] = []

    for group_index, group in enumerate(groups):
        summary_rows.extend(_group_function_rows(group))
        span_instance_rows.extend(_group_span_instance_rows(group_index, group))
        span_rows.extend(_span_rows(group))

    payload = {
        "format_version": 1,
        "source_extract_dir": str(extract_dir),
        "source_correlate_dir": str(correlate_dir),
        "group_count": len(groups),
        "summary_row_count": len(summary_rows),
        "span_row_count": len(span_rows),
        "groups": [asdict(group) for group in groups],
    }

    write_json(output_dir / "aggregate_dataset.json", payload)
    write_csv(output_dir / "aggregate_function_stats.csv", summary_rows)
    write_json(output_dir / "aggregate_span_instance_stats.json", span_instance_rows)
    write_csv(output_dir / "aggregate_span_instance_stats.csv", span_instance_rows)
    write_csv(output_dir / "aggregate_trace_spans.csv", span_rows)

    plot_dir = output_dir / "aggregate_group_plots"
    plot_paths = _plot_all_groups(groups, plot_dir, show_thread_labels=show_thread_labels)

    metadata = {
        "format_version": 1,
        "extract_dir": str(extract_dir),
        "correlate_dir": str(correlate_dir),
        "output_dir": str(output_dir),
        "counts": {
            "groups": len(groups),
            "summary_rows": len(summary_rows),
            "span_instance_rows": len(span_instance_rows),
            "span_rows": len(span_rows),
        },
        "plot_path": plot_paths[0] if plot_paths else None,
        "plot_paths": plot_paths,
        "plot_dir": str(plot_dir),
        "span_instance_stats_path": str(output_dir / "aggregate_span_instance_stats.csv"),
    }
    write_json(output_dir / "aggregate_metadata.json", metadata)
    return metadata


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Aggregate trace timing data from split performance-analysis outputs.")
    parser.add_argument(
        "--extract-dir",
        type=Path,
        default=DEFAULT_BASE / "extract_out",
        help="Directory containing extraction artifacts (spans.json).",
    )
    parser.add_argument(
        "--correlate-dir",
        type=Path,
        default=DEFAULT_BASE / "correlate_out",
        help="Directory containing correlation artifacts (latency_groups.json).",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_BASE / "aggregate_out",
        help="Directory where aggregate artifacts are written.",
    )
    parser.add_argument(
        "--hide-thread-labels",
        action="store_true",
        help="Hide the thread=... labels in generated plots.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    metadata = aggregate_from_artifacts(
        extract_dir=args.extract_dir,
        correlate_dir=args.correlate_dir,
        output_dir=args.output_dir,
        show_thread_labels=not args.hide_thread_labels,
    )
    print(f"Aggregate output: {args.output_dir}")
    print(
        "Aggregate counts -> "
        f"groups: {metadata['counts']['groups']}, "
        f"summary_rows: {metadata['counts']['summary_rows']}, "
        f"span_instance_rows: {metadata['counts']['span_instance_rows']}, "
        f"span_rows: {metadata['counts']['span_rows']}"
    )
    if metadata["plot_paths"]:
        print(f"Plots: {len(metadata['plot_paths'])} files in {metadata['plot_dir']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
