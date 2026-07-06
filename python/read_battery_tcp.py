import argparse
import math
import socket
import struct
import sys
import time
from dataclasses import dataclass


# Example command line usage:
# python read_battery_tcp.py 192.168.0.50 --port 5000

DEFAULT_PORT = 5000
DEFAULT_TIMEOUT = 5.0
DEFAULT_RECONNECT_DELAY = 2.0
PACKET_STRUCT = struct.Struct("<5f12s2f")


@dataclass(slots=True)
class BatteryTelemetryTcpPacket:
    voltage_v: float
    current_a: float
    battery_percent: float
    temperature_c: float
    humidity_percent: float
    bat_state: str
    remaining_usage_time_hours: float
    full_charge_estimate_time_hours: float


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Open a TCP client to the STM32 raw TCP server and parse "
            "BatteryTelemetryTcpPacket frames."
        )
    )
    parser.add_argument(
        "host",
        help="IP address of the STM32 board, for example 192.168.0.50.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help=f"TCP port, default is {DEFAULT_PORT}.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help=f"Socket timeout in seconds, default is {DEFAULT_TIMEOUT}.",
    )
    parser.add_argument(
        "--reconnect-delay",
        type=float,
        default=DEFAULT_RECONNECT_DELAY,
        help=(
            "Delay before reconnect when the server closes or the network "
            f"drops, default is {DEFAULT_RECONNECT_DELAY} seconds."
        ),
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Print the parsed dataclass repr instead of a formatted summary.",
    )
    return parser


def recv_exactly(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size

    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("Socket closed by remote host.")
        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


def decode_bat_state(raw_value: bytes) -> str:
    text = raw_value.split(b"\0", 1)[0].decode("ascii", errors="replace").strip()
    return text or "unknown"


def parse_packet(payload: bytes) -> BatteryTelemetryTcpPacket:
    unpacked = PACKET_STRUCT.unpack(payload)
    return BatteryTelemetryTcpPacket(
        voltage_v=unpacked[0],
        current_a=unpacked[1],
        battery_percent=unpacked[2],
        temperature_c=unpacked[3],
        humidity_percent=unpacked[4],
        bat_state=decode_bat_state(unpacked[5]),
        remaining_usage_time_hours=unpacked[6],
        full_charge_estimate_time_hours=unpacked[7],
    )


def format_float(value: float, digits: int = 2, unit: str = "") -> str:
    if not math.isfinite(value):
        return "N/A"
    return f"{value:.{digits}f}{unit}"


def format_duration_hours(value: float) -> str:
    if not math.isfinite(value) or value < 0.0:
        return "N/A"

    total_minutes = int(value * 60.0)
    hours, minutes = divmod(total_minutes, 60)
    return f"{hours}h {minutes}m"


def format_packet(packet: BatteryTelemetryTcpPacket) -> str:
    return (
        f"V={format_float(packet.voltage_v, 2, 'V')} "
        f"I={format_float(packet.current_a, 2, 'A')} "
        f"SoC={format_float(packet.battery_percent, 1, '%')} "
        f"T={format_float(packet.temperature_c, 2, 'C')} "
        f"RH={format_float(packet.humidity_percent, 2, '%')} "
        f"State={packet.bat_state} "
        f"UseLeft={format_duration_hours(packet.remaining_usage_time_hours)} "
        f"ToFull={format_duration_hours(packet.full_charge_estimate_time_hours)}"
    )


def connect(host: str, port: int, timeout: float) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    return sock


def main() -> int:
    args = build_parser().parse_args()

    print(
        f"Connecting to {args.host}:{args.port} "
        f"(packet size {PACKET_STRUCT.size} bytes)..."
    )

    while True:
        try:
            with connect(args.host, args.port, args.timeout) as sock:
                print("Connected. Waiting for packets...")

                while True:
                    payload = recv_exactly(sock, PACKET_STRUCT.size)
                    packet = parse_packet(payload)
                    if args.raw:
                        print(packet)
                    else:
                        print(format_packet(packet))

        except KeyboardInterrupt:
            print("\nStopped.")
            return 0
        except (ConnectionError, OSError, struct.error) as exc:
            print(f"Connection error: {exc}", file=sys.stderr)
            print(
                f"Reconnect in {args.reconnect_delay:.1f} seconds...",
                file=sys.stderr,
            )
            time.sleep(args.reconnect_delay)


if __name__ == "__main__":
    raise SystemExit(main())