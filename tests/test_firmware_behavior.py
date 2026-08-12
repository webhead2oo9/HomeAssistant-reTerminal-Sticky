# SPDX-License-Identifier: GPL-3.0-or-later

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "reterminal-sticky.example.yaml"


class EspHomeLoader(yaml.SafeLoader):
    pass


def construct_esphome_tag(loader: EspHomeLoader, _suffix: str, node: yaml.Node):
    if isinstance(node, yaml.ScalarNode):
        return loader.construct_scalar(node)
    if isinstance(node, yaml.SequenceNode):
        return loader.construct_sequence(node)
    return loader.construct_mapping(node)


EspHomeLoader.add_multi_constructor("!", construct_esphome_tag)


class FirmwareBehaviorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.config = yaml.load(
            CONFIG_PATH.read_text(encoding="utf-8"), Loader=EspHomeLoader
        )
        cls.scripts = {script["id"]: script for script in cls.config["script"]}

    def test_native_firmware_decisions(self) -> None:
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(
            compiler, "a C++17 compiler is required for native logic tests"
        )
        source = textwrap.dedent(
            r"""
            #include "sticky_logic.h"

            #include <cassert>
            #include <string>

            int main() {
              using panel_ui::assistant_slot_for_layout;
              using panel_ui::camera_slot_for_layout;
              using panel_ui::clean_assistant_text;
              using panel_ui::menu_slot_for_layout;
              using panel_ui::pop_assistant_utf8_codepoint;
              using panel_ui::resolve_camera_snapshot_url;

              const auto fallback = resolve_camera_snapshot_url(
                  "https://ha.example/", "", "/api/camera_proxy/camera.front");
              assert(!fallback.rejected);
              assert(fallback.url == "https://ha.example/api/camera_proxy/camera.front");

              const auto relative = resolve_camera_snapshot_url(
                  "https://ha.example/", "api/camera_proxy/camera.side", "/fallback");
              assert(!relative.rejected);
              assert(relative.url == "https://ha.example/api/camera_proxy/camera.side");

              const auto same_origin = resolve_camera_snapshot_url(
                  "https://ha.example", "https://ha.example/snapshot?id=7", "/fallback");
              assert(!same_origin.rejected);
              assert(same_origin.url == "https://ha.example/snapshot?id=7");

              for (const std::string supplied : {
                       "https://evil.example/snapshot",
                       "https://ha.example.evil.test/snapshot",
                       "https://ha.example:8443/snapshot",
                   }) {
                const auto rejected = resolve_camera_snapshot_url(
                    "https://ha.example", supplied, "/api/camera_proxy/camera.front");
                assert(rejected.rejected);
                assert(rejected.url == "https://ha.example/api/camera_proxy/camera.front");
              }

              assert(camera_slot_for_layout(false, 0, true) == -1);
              assert(assistant_slot_for_layout(false, 0, true, true) == -1);
              assert(menu_slot_for_layout(false, 0, true, true, 20, 70) == -1);

              assert(camera_slot_for_layout(true, 4, true) == 4);
              assert(assistant_slot_for_layout(true, 4, true, true) == 5);
              assert(menu_slot_for_layout(true, 4, true, true, 20, 70) == 0);
              assert(menu_slot_for_layout(true, 4, true, true, 780, 425) == 5);
              assert(menu_slot_for_layout(true, 4, false, false, 20, 250) == 3);
              assert(menu_slot_for_layout(true, 4, false, false, 280, 250) == -1);
              assert(menu_slot_for_layout(true, 4, true, true, 270, 70) == -1);
              assert(menu_slot_for_layout(true, 4, true, true, 20, 247) == -1);

              assert(clean_assistant_text("  Hello\n\tworld\x7f  ") == "Hello world");
              const std::string e_acute = u8"\u00e9";
              assert(clean_assistant_text(e_acute + "!", 2) == e_acute);
              assert(clean_assistant_text(e_acute, 1).empty());
              assert(clean_assistant_text("a " + e_acute, 3) == "a");
              std::string phrase = std::string("A") + e_acute;
              pop_assistant_utf8_codepoint(phrase);
              assert(phrase == "A");
              return 0;
            }
            """
        )

        with tempfile.TemporaryDirectory(prefix="sticky-logic-test-") as temp:
            temp_path = Path(temp)
            cpp = temp_path / "firmware_logic_test.cpp"
            executable = temp_path / (
                "firmware_logic_test.exe" if os.name == "nt" else "firmware_logic_test"
            )
            cpp.write_text(source, encoding="utf-8")
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-I",
                    str(ROOT),
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run(
                [str(executable)], check=False, capture_output=True, text=True
            )
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_voice_capture_is_manual_text_only_and_private(self) -> None:
        voice = self.config["voice_assistant"]
        self.assertFalse(voice["use_wake_word"])
        self.assertNotIn("speaker", voice)
        self.assertNotIn("media_player", voice)
        self.assertEqual(self.config["logger"]["logs"]["voice_assistant"], "WARN")

        start_actions = self.scripts["start_sticky_assistant"]["then"]
        start_branch = start_actions[0]["if"]["then"]
        self.assertEqual(start_branch[-3], {"output.turn_on": "microphone_enable"})
        self.assertEqual(start_branch[-2], {"delay": "250ms"})
        self.assertEqual(
            start_branch[-1],
            {
                "voice_assistant.start": {
                    "id": "sticky_voice",
                    "silence_detection": True,
                }
            },
        )

    def test_microphone_power_ends_at_vad_and_on_every_terminal_path(self) -> None:
        voice = self.config["voice_assistant"]
        vad_actions = voice["on_stt_vad_end"]["then"]
        self.assertEqual(vad_actions[0], {"output.turn_off": "microphone_enable"})
        self.assertNotIn(
            {"script.execute": "refresh_assistant_full_if_visible"}, vad_actions
        )

        for callback in (
            "on_client_disconnected",
            "on_error",
            "on_end",
            "on_idle",
        ):
            with self.subTest(callback=callback):
                self.assertEqual(
                    voice[callback]["then"][0],
                    {"output.turn_off": "microphone_enable"},
                )

        stop_actions = self.scripts["stop_sticky_assistant"]["then"]
        self.assertIn({"output.turn_off": "microphone_enable"}, stop_actions)
        failed_start = self.scripts["start_sticky_assistant"]["then"][0]["if"]["else"]
        self.assertEqual(failed_start[0], {"output.turn_off": "microphone_enable"})

    def test_display_completion_gates_capture(self) -> None:
        queue_condition = self.scripts["queue_sticky_assistant"]["then"][0]["if"][
            "condition"
        ]
        self.assertIn("!id(assist_start_requested)", queue_condition["lambda"])

        start_condition = self.scripts["start_sticky_assistant"]["then"][0]["if"][
            "condition"
        ]
        condition = start_condition["lambda"]
        self.assertIn("!id(draw_busy)", condition)
        self.assertIn("!id(display_dirty)", condition)
        self.assertIn("panel_ui::refresh_idle(id(epaper_display))", condition)
        self.assertIn("id(page_assistant)", condition)

    def test_three_card_labels_are_width_bounded(self) -> None:
        panel = (ROOT / "panel_ui.h").read_text(encoding="utf-8")
        helper_start = panel.index("inline void draw_card_name")
        helper_end = panel.index("// Light card:", helper_start)
        helper = panel[helper_start:helper_end]
        card_start = panel.index("inline void draw_card(")
        card_end = panel.index("inline void draw_menu_name", card_start)
        card = panel[card_start:card_end]

        self.assertIn("MAX_WIDTH = 152", helper)
        self.assertIn("split_menu_name(c, c.value_f", helper)
        self.assertIn("split_menu_name(c, c.small_f", helper)
        self.assertIn("fit_text(c, c.small_f", helper)
        self.assertIn("draw_card_name(c, x, name, fg)", card)

        room_three = (
            ROOT / "packages" / "room-3-cards.yaml"
        ).read_text(encoding="utf-8")
        self.assertIn("card_1_label", room_three)
        self.assertIn("card_2_label", room_three)
        self.assertIn("card_3_label", room_three)

    def test_indoor_temperature_uses_onboard_sht40(self) -> None:
        self.assertNotIn("indoor_temp_entity", self.config["substitutions"])

        sht40 = next(
            sensor
            for sensor in self.config["sensor"]
            if sensor.get("platform") == "sht4x"
        )
        temperature = sht40["temperature"]
        self.assertEqual(temperature["id"], "indoor_temp")
        self.assertEqual(temperature["unit_of_measurement"], "°F")
        self.assertEqual(temperature["accuracy_decimals"], 1)
        self.assertIn("9.0f / 5.0f + 32.0f", temperature["filters"][0]["lambda"])
        self.assertEqual(
            temperature["on_value"]["then"],
            [{"script.execute": "invalidate_temperature_if_visible"}],
        )

        imported_ids = {
            sensor.get("id")
            for sensor in self.config["sensor"]
            if sensor.get("platform") == "homeassistant"
        }
        self.assertNotIn("indoor_temp", imported_ids)


if __name__ == "__main__":
    unittest.main()
