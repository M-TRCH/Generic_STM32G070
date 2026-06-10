import argparse
import json
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
#   python read_pzem_json.py COM5 --baudrate 115200 --timeout 1.0

DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 1.0


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
        return f"{float(value):.{digits}f}"
    except (TypeError, ValueError):
        return str(value)


def format_packet(packet: dict[str, Any]) -> str:
    if not packet.get("ok", False):
        error = packet.get("error", "unknown_error")
        millis = packet.get("millis", "?")
        return f"[{millis}] ERROR: {error}"

    millis = packet.get("millis", "?")
    voltage = format_float(packet.get("voltage"), 2)
    current = format_float(packet.get("current"), 2)
    power = format_float(packet.get("power"), 1)
    energy = format_float(packet.get("energy_wh"), 0)
    soc = format_float(packet.get("soc_percent"), 1)
    remaining = format_float(packet.get("remaining_ah"), 1)

    return (
        f"[{millis}] "
        f"V={voltage}V "
        f"I={current}A "
        f"P={power}W "
        f"E={energy}Wh "
        f"SoC={soc}% "
        f"Rem={remaining}Ah"
    )


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, args.baudrate, timeout=args.timeout) as port:
            print(f"Listening on {port.port} @ {port.baudrate} baud...")
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
