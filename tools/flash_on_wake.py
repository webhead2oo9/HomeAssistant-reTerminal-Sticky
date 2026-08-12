# SPDX-License-Identifier: GPL-3.0-or-later

"""Wait for a sleeping Sticky to wake, then upload firmware over ESPHome OTA."""

from __future__ import annotations

import argparse
import ipaddress
from pathlib import Path
import socket
import subprocess
import sys
import time


DEFAULT_HOSTNAME = "reterminal-sticky.local"
DEFAULT_OTA_PORT = 3232
DEFAULT_TIMEOUT_SECONDS = 2 * 60 * 60


def positive_float(value: str) -> float:
    """Parse a strictly positive command-line number."""
    number = float(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def port_number(value: str) -> int:
    """Parse a valid TCP port."""
    port = int(value)
    if not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError("must be between 1 and 65535")
    return port


def ip_address(value: str) -> str:
    """Validate an optional fallback IP without accepting another hostname."""
    try:
        return str(ipaddress.ip_address(value))
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Probe a deep-sleeping reTerminal Sticky and run an ESPHome OTA "
            "upload as soon as its OTA port becomes reachable."
        )
    )
    parser.add_argument(
        "config",
        nargs="?",
        default="reterminal-sticky.yaml",
        type=Path,
        help="ESPHome YAML path, relative to the current directory by default",
    )
    parser.add_argument(
        "--hostname",
        default=DEFAULT_HOSTNAME,
        help=f"mDNS hostname to resolve (default: {DEFAULT_HOSTNAME})",
    )
    parser.add_argument(
        "--fallback-ip",
        type=ip_address,
        help="IP address to try when mDNS resolution fails",
    )
    parser.add_argument(
        "--timeout",
        type=positive_float,
        default=DEFAULT_TIMEOUT_SECONDS,
        metavar="SECONDS",
        help="overall probing deadline (default: 7200 seconds)",
    )
    parser.add_argument(
        "--interval",
        type=positive_float,
        default=5.0,
        metavar="SECONDS",
        help="delay between probes (default: 5 seconds)",
    )
    parser.add_argument(
        "--connect-timeout",
        type=positive_float,
        default=1.5,
        metavar="SECONDS",
        help="timeout for each TCP probe (default: 1.5 seconds)",
    )
    parser.add_argument(
        "--ota-port",
        type=port_number,
        default=DEFAULT_OTA_PORT,
        help=f"ESPHome OTA TCP port (default: {DEFAULT_OTA_PORT})",
    )
    return parser.parse_args()


def resolve_device(hostname: str, fallback_ip: str | None) -> str | None:
    """Resolve mDNS on every probe so DHCP changes are picked up."""
    try:
        return socket.gethostbyname(hostname)
    except OSError:
        return fallback_ip


def ota_port_is_open(address: str, port: int, timeout: float) -> bool:
    try:
        with socket.create_connection((address, port), timeout=timeout):
            return True
    except OSError:
        return False


def upload(config: Path, address: str) -> int:
    """Run ESPHome from the config directory, avoiding machine-specific paths."""
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "esphome",
            "upload",
            config.name,
            "--device",
            address,
        ],
        cwd=config.parent,
        check=False,
    )
    return result.returncode


def main() -> int:
    args = parse_args()
    config = args.config.expanduser().resolve()
    if not config.is_file():
        print(f"configuration file not found: {config}", file=sys.stderr)
        return 2

    deadline = time.monotonic() + args.timeout
    probe = 0

    try:
        while time.monotonic() < deadline:
            probe += 1
            address = resolve_device(args.hostname, args.fallback_ip)
            if address and ota_port_is_open(
                address, args.ota_port, args.connect_timeout
            ):
                stamp = time.strftime("%H:%M:%S")
                print(
                    f"[{stamp}] OTA port open at {address} "
                    f"(probe {probe}); uploading",
                    flush=True,
                )
                return_code = upload(config, address)
                if return_code == 0:
                    print("upload completed successfully", flush=True)
                    return 0
                print(
                    f"upload exited with status {return_code}; resuming probes",
                    file=sys.stderr,
                    flush=True,
                )

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            time.sleep(min(args.interval, remaining))
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return 130

    print(
        f"device did not become uploadable within {args.timeout:g} seconds",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
