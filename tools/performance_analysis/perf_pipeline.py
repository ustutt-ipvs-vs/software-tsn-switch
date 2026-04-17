from __future__ import annotations

import argparse
from pathlib import Path
from typing import Sequence

from perf_correlate import correlate_from_artifacts
from perf_extract import extract_logs, _infer_source_name

DEFAULT_BASE = Path(__file__).resolve().parent


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run extraction + correlation pipeline without plots/metrics.")
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
        help="Compatibility option. Used to infer the source name from the executable header when --log is not provided.",
    )
    parser.add_argument(
        "--server-log",
        type=Path,
        default=DEFAULT_BASE / "performance_trace_tsnctrld_app.log",
        help="Compatibility option. Used to infer the source name from the executable header when --log is not provided.",
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
    parser.add_argument(
        "--operation-association-file",
        type=Path,
        default=DEFAULT_BASE / "operation_association.json",
        help="Optional JSON file mapping initiator operations to allowed response operations.",
    )
    parser.add_argument(
        "--extract-dir",
        type=Path,
        default=DEFAULT_BASE / "out/extract_out",
        help="Directory for extraction artifacts.",
    )
    parser.add_argument(
        "--correlate-dir",
        type=Path,
        default=DEFAULT_BASE / "out/correlate_out",
        help="Directory for correlation artifacts.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)

    if args.log:
        source_logs = []
        for item in args.log:
            source, sep, path_text = item.partition("=")
            if not sep or not source.strip() or not path_text.strip():
                raise ValueError(f"Invalid --log spec '{item}'. Expected format source=path")
            source_logs.append((source.strip(), Path(path_text.strip())))
    else:
        source_logs = [(_infer_source_name(args.client_log), args.client_log),
                       (_infer_source_name(args.server_log), args.server_log)]

    initiator_source = args.initiator_source
    response_source = args.response_source
    if initiator_source == "client" and response_source == "server" and source_logs:
        initiator_source = source_logs[0][0]
        response_source = source_logs[1][0] if len(source_logs) > 1 else initiator_source

    extraction_summary = extract_logs(
        source_logs=source_logs,
        output_dir=args.extract_dir,
    )
    correlation_summary = correlate_from_artifacts(
        input_dir=args.extract_dir,
        output_dir=args.correlate_dir,
        initiator_source=initiator_source,
        response_source=response_source,
        initiator_context_regex=args.initiator_context_regex,
        response_context_regex=args.response_context_regex,
        operation_association_file=args.operation_association_file,
    )

    print(f"Extract dir: {args.extract_dir}")
    print(f"Correlate dir: {args.correlate_dir}")
    print(f"Spans(total): {extraction_summary['counts']['total_spans']}")
    print(
        f"Latency(samples/groups): {correlation_summary['counts']['latency_samples']}/"
        f"{correlation_summary['counts']['latency_groups']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
