from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
import re
from typing import Sequence

from perf_core import LatencySample, correlate_spans
from perf_artifacts import (
    latency_sample_to_csv_row,
    latency_sample_to_record,
    load_spans,
    write_csv,
    write_json,
)

DEFAULT_BASE = Path(__file__).resolve().parent


def _latency_group_key(sample: LatencySample) -> tuple[str, str, str, int, str]:
    return (
        sample.operation,
        sample.correlation_mode,
        sample.initiator_group_key,
        sample.initiator_index,
        sample.initiator_start.isoformat(timespec="microseconds"),
    )


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
                "combined_response_request_ids": combined_ids,
                "member_count": len(members),
            }
        )

    return rows


def correlate_from_artifacts(
        *,
        input_dir: Path,
        output_dir: Path,
        initiator_source: str,
        response_source: str,
        initiator_context_regex: str | None,
        response_context_regex: str | None,
) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)

    spans_path = input_dir / "spans.json"
    if not spans_path.exists():
        raise FileNotFoundError(f"Missing spans artifact: {spans_path}")

    all_spans = load_spans(spans_path)
    initiator_spans = [span for span in all_spans if span.source == initiator_source]
    response_spans = [span for span in all_spans if span.source == response_source]

    initiator_pattern = re.compile(initiator_context_regex) if initiator_context_regex else None
    response_pattern = re.compile(response_context_regex) if response_context_regex else None

    latency_samples = correlate_spans(
        initiator_spans,
        response_spans,
        initiator_context_pattern=initiator_pattern,
        response_context_pattern=response_pattern,
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
        default=DEFAULT_BASE / "extract_out",
        help="Directory containing extraction artifacts (spans.json).",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_BASE / "correlate_out",
        help="Directory where correlation artifacts are written.",
    )
    parser.add_argument(
        "--initiator-source",
        default="client",
        help="Source id used as initiator in latency correlation.",
    )
    parser.add_argument(
        "--response-source",
        default="server",
        help="Source id used as response in latency correlation.",
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
