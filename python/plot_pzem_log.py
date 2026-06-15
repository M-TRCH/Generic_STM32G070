import argparse
import csv
from datetime import datetime
from pathlib import Path
from typing import Any


TIME_FORMAT = "%Y-%m-%d %H:%M:%S"
GRAPH_1_SERIES = [
    ("voltage", "Voltage (V)", "tab:blue"),
    ("energy_wh", "Energy (Wh)", "tab:purple"),
]
GRAPH_2_SERIES = [
    ("soc_percent", "SoC (%)", "tab:orange"),
    ("remaining_ah", "Remaining (Ah)", "tab:brown"),
]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Plot PZEM CSV data from the first row until the first "
            "error=read_failed entry."
        )
    )
    parser.add_argument(
        "csv_path",
        nargs="?",
        default=Path(__file__).resolve().parent / "log" / "discharge_log.csv",
        type=Path,
        help="Path to the CSV file. Defaults to python/log/discharge_log.csv.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Optional image output path, for example plot.png.",
    )
    parser.add_argument(
        "--no-show",
        action="store_true",
        help="Do not open a plot window. Useful with --output.",
    )
    return parser


def parse_float(value: str) -> float | None:
    if value is None:
        return None

    stripped = value.strip()
    if not stripped:
        return None

    try:
        return float(stripped)
    except ValueError:
        return None


def load_rows(csv_path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []

    with csv_path.open("r", newline="", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        for raw_row in reader:
            if raw_row.get("error", "").strip() == "read_failed":
                break

            pc_time = raw_row.get("pc_time", "").strip()
            if not pc_time:
                continue

            try:
                timestamp = datetime.strptime(pc_time, TIME_FORMAT)
            except ValueError:
                continue

            row: dict[str, Any] = {"pc_time": timestamp}
            for key, _, _ in GRAPH_1_SERIES + GRAPH_2_SERIES:
                row[key] = parse_float(raw_row.get(key, ""))

            rows.append(row)

    if not rows:
        raise ValueError("No plottable rows were found before the first read_failed entry.")

    return rows


def mark_time_bounds(axis: Any, times: list[datetime]) -> None:
    start_time = times[0]
    end_time = times[-1]
    axis.axvline(start_time, color="tab:green", linestyle=":", linewidth=1.2)
    axis.axvline(end_time, color="tab:red", linestyle=":", linewidth=1.2)
    axis.annotate(
        f"Start {start_time:%H:%M:%S}",
        xy=(start_time, 1),
        xycoords=("data", "axes fraction"),
        xytext=(6, -8),
        textcoords="offset points",
        ha="left",
        va="top",
        color="tab:green",
        fontsize=9,
        bbox={"boxstyle": "round,pad=0.2", "fc": "white", "ec": "tab:green", "alpha": 0.85},
    )
    axis.annotate(
        f"End {end_time:%H:%M:%S}",
        xy=(end_time, 1),
        xycoords=("data", "axes fraction"),
        xytext=(-6, -8),
        textcoords="offset points",
        ha="right",
        va="top",
        color="tab:red",
        fontsize=9,
        bbox={"boxstyle": "round,pad=0.2", "fc": "white", "ec": "tab:red", "alpha": 0.85},
    )


def create_multi_axis_plot(axis: Any, times: list[datetime], rows: list[dict[str, Any]], series: list[tuple[str, str, str]], title: str) -> list[Any]:
    axes = [axis]
    axis.set_title(title)
    axis.grid(True, linestyle="--", alpha=0.3)
    mark_time_bounds(axis, times)

    for index, (column, label, color) in enumerate(series):
        current_axis = axis if index == 0 else axis.twinx()
        if index > 1:
            current_axis.spines["right"].set_position(("axes", 1 + (index - 1) * 0.12))
        if index > 0:
            axes.append(current_axis)

        values = [row[column] for row in rows]
        current_axis.plot(times, values, color=color, linewidth=1.4, label=label)
        current_axis.set_ylabel(label, color=color)
        current_axis.tick_params(axis="y", colors=color)

    lines = []
    labels = []
    for current_axis in axes:
        axis_lines, axis_labels = current_axis.get_legend_handles_labels()
        lines.extend(axis_lines)
        labels.extend(axis_labels)

    axis.legend(lines, labels, loc="lower left")
    return axes


def main() -> int:
    args = build_parser().parse_args()

    if args.output or args.no_show:
        import matplotlib

        matplotlib.use("Agg")

    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    rows = load_rows(args.csv_path)
    times = [row["pc_time"] for row in rows]

    figure, axes = plt.subplots(2, 1, figsize=(15, 10), sharex=True)
    figure.subplots_adjust(right=0.82, hspace=0.3)

    create_multi_axis_plot(
        axes[0],
        times,
        rows,
        GRAPH_1_SERIES,
        "Electrical Measurements: Voltage and Accumulated Energy",
    )
    create_multi_axis_plot(
        axes[1],
        times,
        rows,
        GRAPH_2_SERIES,
        "Battery Capacity Indicators: State of Charge and Remaining Capacity",
    )

    axes[1].set_xlabel("Time")
    axes[1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
    figure.autofmt_xdate()
    figure.suptitle(
        f"{args.csv_path.name} ({len(rows)} rows before first read_failed)",
        fontsize=14,
    )

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        figure.savefig(args.output, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {args.output}")

    if not args.no_show:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())