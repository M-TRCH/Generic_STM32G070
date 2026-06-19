import argparse
import csv
from datetime import datetime
from pathlib import Path
from typing import Any

# Example command to run this script:
# python plot_pzem_log.py --log-type discharge --output plot.png

TIME_FORMAT = "%Y-%m-%d %H:%M:%S"
DEFAULT_LOG_DIR = Path(__file__).resolve().parent / "log"
LOG_PRESETS = {
    "discharge": DEFAULT_LOG_DIR / "discharge_log.csv",
    "charge": DEFAULT_LOG_DIR / "charge_log.csv",
}
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
        type=Path,
        help="Optional CSV file path. Overrides --log-type when provided.",
    )
    parser.add_argument(
        "--log-type",
        choices=tuple(LOG_PRESETS.keys()),
        default="discharge",
        help="Select a preset log file from python/log. Default is discharge.",
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


def build_row(raw_row: dict[str, str]) -> dict[str, Any] | None:
    pc_time = raw_row.get("pc_time", "").strip()
    if not pc_time:
        return None

    voltage = parse_float(raw_row.get("voltage", ""))
    energy_wh = parse_float(raw_row.get("energy_wh", ""))
    soc_percent = parse_float(raw_row.get("soc_percent", ""))
    remaining_ah = parse_float(raw_row.get("remaining_ah", ""))
    if all(value is None for value in (voltage, energy_wh, soc_percent, remaining_ah)):
        return None

    try:
        timestamp = datetime.strptime(pc_time, TIME_FORMAT)
    except ValueError:
        return None

    return {
        "pc_time": timestamp,
        "voltage": voltage,
        "energy_wh": energy_wh,
        "soc_percent": soc_percent,
        "remaining_ah": remaining_ah,
    }


def load_rows(csv_path: Path, log_type: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []

    with csv_path.open("r", newline="", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        if log_type == "charge":
            collecting = False

            for raw_row in reader:
                if raw_row.get("error", "").strip() == "read_failed":
                    rows = []
                    collecting = True
                    continue

                if not collecting:
                    continue

                row = build_row(raw_row)
                if row is None:
                    continue

                rows.append(row)

                if row["soc_percent"] == 100.0:
                    break
        else:
            collecting = False

            for raw_row in reader:
                if raw_row.get("error", "").strip() == "read_failed":
                    if collecting:
                        break
                    continue

                row = build_row(raw_row)
                if row is None:
                    continue

                collecting = True
                rows.append(row)

    if not rows:
        if log_type == "charge":
            raise ValueError("No plottable rows were found from the latest read_failed entry to the first soc_percent=100.0 row.")

        raise ValueError("No plottable rows were found between the first valid data row and the next read_failed entry.")

    return rows


def resolve_csv_path(args: argparse.Namespace) -> Path:
    if args.csv_path is not None:
        return args.csv_path

    return LOG_PRESETS[args.log_type]


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
    csv_path = resolve_csv_path(args)

    try:
        if args.output or args.no_show:
            import matplotlib

            matplotlib.use("Agg")

        import matplotlib.dates as mdates
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit(
            "matplotlib is required. Install it with: "
            "C:\\Users\\mteer\\.platformio\\penv\\Scripts\\python.exe -m pip install matplotlib"
        ) from exc

    rows = load_rows(csv_path, args.log_type)
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
    if args.log_type == "charge":
        subtitle = f"{csv_path.name} ({len(rows)} rows from latest read_failed to first soc_percent=100.0)"
    else:
        subtitle = f"{csv_path.name} ({len(rows)} rows from first valid data to next read_failed)"

    figure.suptitle(subtitle, fontsize=14)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        figure.savefig(args.output, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {args.output}")

    if not args.no_show:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())