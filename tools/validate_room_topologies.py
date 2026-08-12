#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the reusable room packages at the supported menu boundaries."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


CASES: dict[str, tuple[list[int], bool, bool]] = {
    "one-room-camera-assist": ([1], True, True),
    "two-rooms-mixed": ([1, 3], True, True),
    "four-rooms-camera-assist": ([1, 2, 3, 1], True, True),
    "five-rooms-camera": ([1, 2, 3, 1, 2], True, False),
    "five-rooms-assist": ([1, 2, 3, 1, 2], False, True),
    "six-rooms-no-specials": ([1, 2, 3, 1, 2, 3], False, False),
}

ROOM_ICON = "F04B9"
CARD_ICON_ON = "F1253"
CARD_ICON_OFF = "F1254"


def quoted(value: str) -> str:
    """Return a JSON string, which is also a safely quoted YAML scalar."""

    return json.dumps(value)


def room_package(repo: Path, slot: int, card_count: int) -> str:
    package = (
        repo
        / "packages"
        / f"room-{card_count}-card{'s' if card_count != 1 else ''}.yaml"
    )
    key = f"ci_room_{slot}"
    lines = [
        f"  {key}: !include",
        f"    file: {quoted(package.as_posix())}",
        "    vars:",
        f"      slot: {quoted(str(slot))}",
        f"      key: {key}",
        f"      label: {quoted(f'CI Room {slot + 1}')}",
        f"      icon: {quoted(ROOM_ICON)}",
    ]
    for card in range(1, card_count + 1):
        lines.extend(
            [
                f"      card_{card}_label: {quoted(f'Card {card}')}",
                f"      card_{card}_entity: light.{key}_card_{card}",
                f"      card_{card}_icon_on: {quoted(CARD_ICON_ON)}",
                f"      card_{card}_icon_off: {quoted(CARD_ICON_OFF)}",
            ]
        )
    return "\n".join(lines)


def configuration(
    repo: Path,
    case: str,
    card_counts: list[int],
    camera: bool,
    assistant: bool,
) -> str:
    packages = "\n\n".join(
        room_package(repo, slot, count) for slot, count in enumerate(card_counts)
    )
    camera_literal = "true" if camera else "false"
    assistant_literal = "true" if assistant else "false"
    return f"""packages:
{packages}

esphome:
  name: sticky-{case}
  includes:
    - {quoted((repo / "sticky_logic.h").as_posix())}
    - {quoted((repo / "room_model.h").as_posix())}
  on_boot:
    - priority: 550
      then:
        - lambda: 'panel_ui::room_model().reset();'
    - priority: 400
      then:
        - lambda: 'panel_ui::room_model().finalize({camera_literal}, {assistant_literal});'

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: arduino

wifi:
  ssid: "ci-network"
  enable_on_boot: false

api:

script:
  - id: invalidate_room_card_if_visible
    parameters:
      room_index: int
      card_index: int
    mode: parallel
    max_runs: 24
    then:
      - lambda: 'return;'
"""


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="sticky-room-topologies-") as temporary:
        temp = Path(temporary)
        for case, (card_counts, camera, assistant) in CASES.items():
            config = temp / f"{case}.yaml"
            config.write_text(
                configuration(repo, case, card_counts, camera, assistant),
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, "-m", "esphome", "config", str(config)],
                cwd=repo,
                text=True,
                capture_output=True,
                check=False,
            )
            if result.returncode != 0:
                sys.stdout.write(result.stdout)
                sys.stderr.write(result.stderr)
                print(f"Topology validation failed: {case}", file=sys.stderr)
                return result.returncode

            registrations = result.stdout.count("panel_ui::room_model().set_room(")
            if registrations != len(card_counts):
                print(
                    f"Topology {case} expanded {registrations} room registrations; "
                    f"expected {len(card_counts)}",
                    file=sys.stderr,
                )
                return 1
            print(
                f"Validated {case}: {len(card_counts)} room(s), "
                f"card counts {card_counts}, camera={feature_literal(camera)}, "
                f"assist={feature_literal(assistant)}"
            )
    return 0


def feature_literal(enabled: bool) -> str:
    return "enabled" if enabled else "disabled"


if __name__ == "__main__":
    raise SystemExit(main())
