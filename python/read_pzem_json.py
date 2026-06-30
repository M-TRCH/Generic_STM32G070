import argparse
import csv
import json
import math
from pathlib import Path
import sys
import time
from typing import Any

try:
    import serial
    from serial import SerialException
except ImportError as exc:
    raise SystemExit(
        "pyserial is required. Install it with: pip install pyserial"
    ) from exc


#   Example command line usage:
#   python read_pzem_json.py COM5 --baudrate 115200 --timeout 1.0 --raw

DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 1.0
DEFAULT_LOG_FILENAME = "pzem_log.csv"


def get_nested(packet: dict[str, Any], *keys: str) -> Any:
    value: Any = packet
    for key in keys:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def build_log_path() -> Path:
    return Path(__file__).resolve().with_name(DEFAULT_LOG_FILENAME)


def build_csv_row(packet: dict[str, Any]) -> dict[str, Any]:
    return {
        "pc_time": time.strftime("%Y-%m-%d %H:%M:%S"),
        "type": packet.get("type"),
        "ok": packet.get("ok"),
        "millis": packet.get("millis"),
        "boot_source": packet.get("boot_source"),
        "live_source": packet.get("live_source"),
        "filtered_voltage_v": packet.get("filtered_voltage_v"),
        "degraded_mode": packet.get("degraded_mode"),
        "charge_voltage": get_nested(packet, "charge", "voltage"),
        "charge_current": get_nested(packet, "charge", "current"),
        "charge_power": get_nested(packet, "charge", "power"),
        "charge_energy_wh": get_nested(packet, "charge", "energy_wh"),
        "charge_state": get_nested(packet, "charge", "state"),
        "discharge_voltage": get_nested(packet, "discharge", "voltage"),
        "discharge_current": get_nested(packet, "discharge", "current"),
        "discharge_power": get_nested(packet, "discharge", "power"),
        "discharge_energy_wh": get_nested(packet, "discharge", "energy_wh"),
        "discharge_state": get_nested(packet, "discharge", "state"),
        "soc_percent": packet.get("soc_percent"),
        "remaining_ah": packet.get("remaining_ah"),
        "time_remaining_h": packet.get("time_remaining_h"),
        "time_to_full_h": packet.get("time_to_full_h"),
        "event": packet.get("event"),
        "status": packet.get("status"),
        "message": packet.get("message"),
        "address": packet.get("address"),
        "sensor": packet.get("sensor"),
        "reason": packet.get("reason"),
        "error": packet.get("error"),
    }


def open_csv_logger() -> tuple[Any, csv.DictWriter]:
    log_path = build_log_path()
    fieldnames = list(build_csv_row({}).keys())
    file_handle = log_path.open("a", newline="", encoding="utf-8")
    writer = csv.DictWriter(file_handle, fieldnames=fieldnames)

    if log_path.stat().st_size == 0:
        writer.writeheader()
        file_handle.flush()

    return file_handle, writer


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Read PZEM-017 JSON lines from the STM32 serial port."
    )
    parser.add_argument(
        "port",
        help="Serial port name, for example COM5 on Windows.",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=DEFAULT_BAUDRATE,
        help=f"Serial baudrate, default is {DEFAULT_BAUDRATE}.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help=f"Serial read timeout in seconds, default is {DEFAULT_TIMEOUT}.",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Print raw JSON objects instead of formatted summaries.",
    )
    return parser


def format_float(value: Any, digits: int = 2) -> str:
    if value is None:
        return "null"

    try:
        numeric = float(value)
        if not math.isfinite(numeric):
            return "null"
        return f"{numeric:.{digits}f}"
    except (TypeError, ValueError):
        return str(value)


def format_duration_hours(value: Any) -> str:
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return "N/A"

    if not math.isfinite(numeric) or numeric < 0.0:
        return "N/A"

    total_minutes = int(numeric * 60.0)
    hours, minutes = divmod(total_minutes, 60)
    return f"{hours}h {minutes}m"


def format_pair_packet(packet: dict[str, Any]) -> str:
    millis = packet.get("millis", "?")
    charge_state = get_nested(packet, "charge", "state") or "?"
    discharge_state = get_nested(packet, "discharge", "state") or "?"
    charge_voltage = format_float(get_nested(packet, "charge", "voltage"), 2)
    charge_current = format_float(get_nested(packet, "charge", "current"), 2)
    discharge_voltage = format_float(get_nested(packet, "discharge", "voltage"), 2)
    discharge_current = format_float(get_nested(packet, "discharge", "current"), 2)
    soc = format_float(packet.get("soc_percent"), 1)
    remaining = format_float(packet.get("remaining_ah"), 1)
    time_remaining = format_duration_hours(packet.get("time_remaining_h"))
    time_to_full = format_duration_hours(packet.get("time_to_full_h"))
    filtered_voltage = format_float(packet.get("filtered_voltage_v"), 3)
    boot_source = packet.get("boot_source", "?")
    live_source = packet.get("live_source", "?")
    degraded = "Y" if packet.get("degraded_mode") else "N"

    return (
        f"[{millis}] "
        f"CH({charge_state})={charge_voltage}V/{charge_current}A "
        f"DS({discharge_state})={discharge_voltage}V/{discharge_current}A "
        f"SoC={soc}% Rem={remaining}Ah "
        f"Trem={time_remaining} Tfull={time_to_full} "
        f"Vflt={filtered_voltage}V boot={boot_source} live={live_source} degraded={degraded}"
    )


def format_event_packet(packet: dict[str, Any]) -> str:
    millis = packet.get("millis", "?")
    event = packet.get("event", "event")
    status = packet.get("status", "?")
    details: list[str] = []

    if packet.get("address") is not None:
        details.append(f"address={packet['address']}")
    if packet.get("soc_percent") is not None:
        details.append(f"soc={format_float(packet.get('soc_percent'), 2)}%")
    if packet.get("message"):
        details.append(f"message={packet['message']}")

    suffix = f" {' '.join(details)}" if details else ""
    return f"[{millis}] EVENT {event} status={status}{suffix}"


def format_read_error_packet(packet: dict[str, Any]) -> str:
    millis = packet.get("millis", "?")
    sensor = packet.get("sensor", "?")
    address = packet.get("address", "?")
    reason = packet.get("reason", packet.get("error", "unknown_error"))
    return f"[{millis}] READ_ERROR sensor={sensor} address={address} reason={reason}"


def format_packet(packet: dict[str, Any]) -> str:
    packet_type = packet.get("type")

    if packet_type == "pzem017_pair":
        return format_pair_packet(packet)

    if packet_type == "battery_soc_event":
        return format_event_packet(packet)

    if packet_type in {"pzem017_read_error", "pzem017"} or packet.get("ok") is False:
        return format_read_error_packet(packet)

    return json.dumps(packet, ensure_ascii=True)


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        log_file, log_writer = open_csv_logger()
        with serial.Serial(args.port, args.baudrate, timeout=args.timeout) as port, log_file:
            print(f"Listening on {port.port} @ {port.baudrate} baud...")
            print(f"Logging CSV to {build_log_path()}")
            while True:
                line = port.readline()
                if not line:
                    continue

                try:
                    decoded = line.decode("utf-8").strip()
                except UnicodeDecodeError:
                    continue

                if not decoded:
                    continue

                try:
                    packet = json.loads(decoded)
                except json.JSONDecodeError:
                    print(f"NON_JSON: {decoded}")
                    continue

                log_writer.writerow(build_csv_row(packet))
                log_file.flush()

                if args.raw:
                    print(json.dumps(packet, ensure_ascii=True))
                else:
                    print(format_packet(packet))

    except KeyboardInterrupt:
        print("\nStopped.")
        return 0
    except SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
