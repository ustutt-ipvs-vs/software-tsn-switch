from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
import re
from typing import Sequence

from perf_core import LatencySample, correlate_spans
from perf_artifacts import (
    latency_sample_to_csv_row,
    latency_sample_to_record,
    load_spans,
    read_json,
    write_csv,
    write_json,
)

DEFAULT_BASE = Path(__file__).resolve().parent
DEFAULT_INITIATOR_SOURCE = "client"
DEFAULT_RESPONSE_SOURCE = "server"


def _latency_group_key(sample: LatencySample) -> tuple[str, str, str, int, str]:
    return (
        sample.operation,
        sample.correlation_mode,
        sample.initiator_group_key,
        sample.initiator_index,
        sample.initiator_start.isoformat(timespec="microseconds"),
    )


def _operation_key(value: str) -> str:
    return value.strip().strip("[]")


def _append_unique_operations(target: list[str], operations: Sequence[str]) -> None:
    seen = {_operation_key(item) for item in target}
    for operation in operations:
        key = _operation_key(operation)
        if key in seen:
            continue
        target.append(operation)
        seen.add(key)


def _validate_operation_string(value: object, *, location: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{location}: expected a string, got {type(value).__name__}")
    normalized = value.strip()
    if not normalized:
        raise ValueError(f"{location}: must be a non-empty string")
    return normalized


def _validate_operation_list(value: object, *, location: str) -> list[str]:
    if not isinstance(value, list):
        raise ValueError(f"{location}: expected a list of strings, got {type(value).__name__}")

    normalized: list[str] = []
    for index, item in enumerate(value):
        normalized.append(_validate_operation_string(item, location=f"{location}[{index}]"))

    deduped: list[str] = []
    _append_unique_operations(deduped, normalized)
    return deduped


def _parse_associations_schema(payload: dict[str, object], *, path: Path) -> dict[str, list[str]]:
    associations = payload.get("associations")
    if not isinstance(associations, list):
        raise ValueError(f"{path}: key 'associations' must be a list of objects")

    rules: dict[str, list[str]] = {}
    for index, association in enumerate(associations):
        location = f"{path}: associations[{index}]"
        if not isinstance(association, dict):
            raise ValueError(f"{location}: expected an object, got {type(association).__name__}")

        required_keys = {"client_operations", "server_operations"}
        actual_keys = set(association)
        missing_keys = required_keys - actual_keys
        extra_keys = actual_keys - required_keys
        if missing_keys:
            raise ValueError(f"{location}: missing required key(s): {sorted(missing_keys)}")
        if extra_keys:
            raise ValueError(f"{location}: unexpected key(s): {sorted(extra_keys)}")

        client_operations = _validate_operation_list(
            association["client_operations"],
            location=f"{location}.client_operations",
        )
        server_operations = _validate_operation_list(
            association["server_operations"],
            location=f"{location}.server_operations",
        )

        for client_operation in client_operations:
            rules.setdefault(client_operation, [])
            _append_unique_operations(rules[client_operation], server_operations)

    return rules


def _parse_flat_mapping_schema(payload: dict[str, object], *, path: Path) -> dict[str, list[str]]:
    rules: dict[str, list[str]] = {}
    for initiator_operation, response_operations in payload.items():
        initiator_key = _validate_operation_string(initiator_operation, location=f"{path}: key")
        if isinstance(response_operations, str):
            normalized = [
                _validate_operation_string(response_operations, location=f"{path}: value for '{initiator_key}'")]
        elif isinstance(response_operations, list):
            normalized = _validate_operation_list(
                response_operations,
                location=f"{path}: value for '{initiator_key}'",
            )
        else:
            raise ValueError(
                f"{path}: value for '{initiator_key}' must be a string or a list of strings, got {type(response_operations).__name__}"
            )

        rules.setdefault(initiator_key, [])
        _append_unique_operations(rules[initiator_key], normalized)

    return rules


def build_latency_groups(samples: Sequence[LatencySample]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str, int, str], list[LatencySample]] = defaultdict(list)
    for sample in samples:
        grouped[_latency_group_key(sample)].append(sample)

    rows: list[dict[str, object]] = []
    for key, members in sorted(grouped.items()):
        operation, correlation_mode, initiator_group_key, initiator_index, initiator_start = key
        request_ids_by_category: dict[str, list[int]] = {}
        for category in ("all", "internal", "external"):
            request_ids = sorted({
                int(member.response_request_id)
                for member in members
                if member.response_category == category and member.response_request_id is not None
            })
            if request_ids:
                request_ids_by_category[category] = request_ids

        response_operations = sorted({
            member.response_operation
            for member in members
            if member.response_operation is not None
        })

        combined_ids = sorted({
            request_id
            for request_ids in request_ids_by_category.values()
            for request_id in request_ids
        })

        first = members[0]
        rows.append(
            {
                "operation": operation,
                "correlation_mode": correlation_mode,
                "initiator_group_key": initiator_group_key,
                "initiator_index": initiator_index,
                "initiator_iter": first.initiator_iter,
                "initiator_msg": first.initiator_msg,
                "initiator_start": initiator_start,
                "request_ids_by_category": request_ids_by_category,
                "response_operations": response_operations,
                "combined_response_request_ids": combined_ids,
                "member_count": len(members),
            }
        )

    return rows


def _load_operation_association_rules(path: Path | None) -> dict[str, list[str]]:
    if path is None:
        return {}

    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"{path}: expected a JSON object, got {type(payload).__name__}")

    if set(payload) == {"associations"}:
        return _parse_associations_schema(payload, path=path)

    if "associations" in payload:
        raise ValueError(f"{path}: associations schema must contain only the key 'associations'")

    return _parse_flat_mapping_schema(payload, path=path)


def _load_extraction_source_names(input_dir: Path) -> list[str]:
    metadata_path = input_dir / "extraction_metadata.json"
    if not metadata_path.exists():
        return []

    payload = read_json(metadata_path)
    if not isinstance(payload, dict):
        return []

    sources = payload.get("sources")
    if not isinstance(sources, list):
        return []

    names: list[str] = []
    for source_info in sources:
        if not isinstance(source_info, dict):
            continue
        source_name = source_info.get("source")
        if isinstance(source_name, str) and source_name.strip():
            names.append(source_name.strip())
    return names


def _resolve_source_name(value: str, *, default_value: str, source_names: Sequence[str], index: int) -> str:
    if value != default_value:
        return value
    if index < len(source_names):
        return source_names[index]
    if len(source_names) == 1:
        return source_names[0]
    return value


def correlate_from_artifacts(
        *,
        input_dir: Path,
        output_dir: Path,
        initiator_source: str,
        response_source: str,
        initiator_context_regex: str | None,
        response_context_regex: str | None,
        operation_association_file: Path | None = None,
) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)

    source_names = _load_extraction_source_names(input_dir)
    initiator_source = _resolve_source_name(
        initiator_source,
        default_value=DEFAULT_INITIATOR_SOURCE,
        source_names=source_names,
        index=0,
    )
    response_source = _resolve_source_name(
        response_source,
        default_value=DEFAULT_RESPONSE_SOURCE,
        source_names=source_names,
        index=1,
    )

    spans_path = input_dir / "spans.json"
    if not spans_path.exists():
        raise FileNotFoundError(f"Missing spans artifact: {spans_path}")

    all_spans = load_spans(spans_path)
    initiator_spans = [span for span in all_spans if span.source == initiator_source]
    response_spans = [span for span in all_spans if span.source == response_source]

    initiator_pattern = re.compile(initiator_context_regex) if initiator_context_regex else None
    response_pattern = re.compile(response_context_regex) if response_context_regex else None
    operation_association_rules = _load_operation_association_rules(operation_association_file)

    latency_samples = correlate_spans(
        initiator_spans,
        response_spans,
        initiator_context_pattern=initiator_pattern,
        response_context_pattern=response_pattern,
        operation_association_rules=operation_association_rules,
        correlation_mode=f"{initiator_source}_to_{response_source}_chronological",
    )
    latency_groups = build_latency_groups(latency_samples)

    write_json(output_dir / "latency_samples.json", [latency_sample_to_record(sample) for sample in latency_samples])
    write_csv(output_dir / "latency_samples.csv", [latency_sample_to_csv_row(sample) for sample in latency_samples])
    write_json(output_dir / "latency_groups.json", latency_groups)

    summary = {
        "format_version": 2,
        "input_dir": str(input_dir),
        "output_dir": str(output_dir),
        "initiator_source": initiator_source,
        "response_source": response_source,
        "initiator_context_regex": initiator_context_regex,
        "response_context_regex": response_context_regex,
        "operation_association_file": None if operation_association_file is None else str(operation_association_file),
        "operation_association_rules": operation_association_rules,
        "counts": {
            "all_spans": len(all_spans),
            "initiator_spans": len(initiator_spans),
            "response_spans": len(response_spans),
            "latency_samples": len(latency_samples),
            "latency_groups": len(latency_groups),
        },
    }
    write_json(output_dir / "correlation_metadata.json", summary)
    return summary


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Correlate two span sources from extraction artifacts.")
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=DEFAULT_BASE / "out/extract_out",
        help="Directory containing extraction artifacts (spans.json).",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_BASE / "out/correlate_out",
        help="Directory where correlation artifacts are written.",
    )
    parser.add_argument(
        "--initiator-source",
        default=DEFAULT_INITIATOR_SOURCE,
        help="Source id used as initiator in latency correlation. Defaults to the first source in the extraction artifacts.",
    )
    parser.add_argument(
        "--response-source",
        default=DEFAULT_RESPONSE_SOURCE,
        help="Source id used as response in latency correlation. Defaults to the second source in the extraction artifacts.",
    )
    parser.add_argument(
        "--initiator-context-regex",
        default=None,
        help="Optional regex to filter initiator contexts.",
    )
    parser.add_argument(
        "--response-context-regex",
        default=None,
        help="Optional regex to filter response contexts.",
    )
    parser.add_argument(
        "--operation-association-file",
        type=Path,
        default=None,
        help="Optional JSON file mapping initiator operations to allowed response operations.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    summary = correlate_from_artifacts(
        input_dir=args.input_dir,
        output_dir=args.output_dir,
        initiator_source=args.initiator_source,
        response_source=args.response_source,
        initiator_context_regex=args.initiator_context_regex,
        response_context_regex=args.response_context_regex,
        operation_association_file=args.operation_association_file,
    )
    print(f"Correlation output: {args.output_dir}")
    print(
        "Correlation counts -> "
        f"initiators: {summary['counts']['initiator_spans']}, "
        f"responses: {summary['counts']['response_spans']}, "
        f"samples: {summary['counts']['latency_samples']}, "
        f"groups: {summary['counts']['latency_groups']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
