from __future__ import annotations

import argparse
from pathlib import Path
from typing import Sequence

from perf_core import parse_log_file, reconstruct_spans
from perf_artifacts import (
    span_records_from_spans,
    span_to_csv_row,
    warning_to_csv_row,
    warning_to_record,
    write_csv,
    write_json,
)

DEFAULT_BASE = Path(__file__).resolve().parent


def _parse_source_spec(item: str) -> tuple[str, Path]:
    source, sep, path_text = item.partition("=")
    if not sep or not source.strip() or not path_text.strip():
        raise ValueError(f"Invalid --log spec '{item}'. Expected format source=path")
    return source.strip(), Path(path_text.strip())


def _build_reset_trigger_map_payload(spans: Sequence[object]) -> dict[str, object]:
    by_request_id: dict[str, dict[str, object]] = {}
    for span in spans:
        if getattr(span, "context_text", "") != "[RESET_GPT_TRIGGER]" or getattr(span, "request_id", None) is None:
            continue

        key = str(span.request_id)
        entry = by_request_id.get(key)
        if entry is None:
            entry = {
                "reset_request_id": int(span.request_id),
                "reset_start": span.start_time.isoformat(timespec="microseconds"),
                "reset_end": span.end_time.isoformat(timespec="microseconds"),
                "spawned_internal_request_ids": [],
            }
            by_request_id[key] = entry
        else:
            entry["reset_start"] = min(entry["reset_start"], span.start_time.isoformat(timespec="microseconds"))
            entry["reset_end"] = max(entry["reset_end"], span.end_time.isoformat(timespec="microseconds"))

    for span in spans:
        reset_req = getattr(span, "reset_trigger_request_id", None)
        if reset_req is None or getattr(span, "request_id", None) is None:
            continue
        if getattr(span, "response_category", None) != "internal":
            continue
        key = str(reset_req)
        entry = by_request_id.get(key)
        if entry is None:
            continue
        spawned_ids = entry["spawned_internal_request_ids"]
        request_id = int(span.request_id)
        if request_id not in spawned_ids:
            spawned_ids.append(request_id)

    for entry in by_request_id.values():
        entry["spawned_internal_request_ids"] = sorted(entry["spawned_internal_request_ids"])

    return {
        "format_version": 1,
        "by_request_id": by_request_id,
    }


def extract_logs(
        *,
        source_logs: Sequence[tuple[str, Path]],
        output_dir: Path,
) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)

    all_spans = []
    all_warnings = []
    source_counts: dict[str, dict[str, int]] = {}
    for source, log_path in source_logs:
        parsed = parse_log_file(str(log_path))
        result = reconstruct_spans(parsed, source)
        all_spans.extend(result.spans)
        all_warnings.extend(result.warnings)
        source_counts[source] = {
            "spans": len(result.spans),
            "warnings": len(result.warnings),
        }

    all_spans.sort(key=lambda span: (span.start_time, span.thread_id, span.start_line_index))

    write_json(output_dir / "spans.json", span_records_from_spans(all_spans))
    write_csv(output_dir / "spans.csv", [span_to_csv_row(span) for span in all_spans])
    write_json(output_dir / "reset_trigger_map.json", _build_reset_trigger_map_payload(all_spans))

    write_json(output_dir / "warnings.json", [warning_to_record(w) for w in all_warnings])
    write_csv(output_dir / "warnings.csv", [warning_to_csv_row(w) for w in all_warnings])

    metadata = {
        "format_version": 3,
        "sources": [
            {
                "source": source,
                "log": str(log_path),
                "spans": source_counts[source]["spans"],
                "warnings": source_counts[source]["warnings"],
            }
            for source, log_path in source_logs
        ],
        "counts": {
            "total_spans": len(all_spans),
            "total_warnings": len(all_warnings),
        },
        "lineage_artifact": "reset_trigger_map.json",
    }
    write_json(output_dir / "extraction_metadata.json", metadata)
    return metadata


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Extract structured spans from arbitrary performance logs.")
    parser.add_argument(
        "--log",
        action="append",
        default=[],
        help="Source and file pair in form source=path. Repeat for multiple sources.",
    )
    parser.add_argument(
        "--client-log",
        type=Path,
        default=DEFAULT_BASE / "performance_trace_performance_testing.log",
        help="Compatibility option. Used as source 'client' when --log is not provided.",
    )
    parser.add_argument(
        "--server-log",
        type=Path,
        default=DEFAULT_BASE / "performance_trace_tsnctrld_app.log",
        help="Compatibility option. Used as source 'server' when --log is not provided.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_BASE / "extract_out",
        help="Directory where extraction artifacts are written.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)

    source_logs: list[tuple[str, Path]] = []
    if args.log:
        for item in args.log:
            source_logs.append(_parse_source_spec(item))
    else:
        source_logs = [("client", args.client_log), ("server", args.server_log)]

    for source, path in source_logs:
        if not path.exists():
            raise FileNotFoundError(f"Log for source '{source}' not found: {path}")

    metadata = extract_logs(source_logs=source_logs, output_dir=args.output_dir)
    print(f"Extraction output: {args.output_dir}")
    for source_info in metadata["sources"]:
        print(
            f"Source {source_info['source']}: spans={source_info['spans']}, "
            f"warnings={source_info['warnings']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
