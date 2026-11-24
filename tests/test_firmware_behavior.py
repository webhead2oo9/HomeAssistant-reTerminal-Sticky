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

              // Menu tiles: the lower strip of each row switches the room.
              using panel_ui::menu_press_in_quick_strip;
              assert(!menu_press_in_quick_strip(78));
              assert(!menu_press_in_quick_strip(165));
              assert(menu_press_in_quick_strip(166));
              assert(menu_press_in_quick_strip(238));
              assert(!menu_press_in_quick_strip(256));
              assert(menu_press_in_quick_strip(344));
              assert(menu_press_in_quick_strip(416));

              // Light cards: three direct brightness levels across 240px.
              using panel_ui::brightness_level_at;
              using panel_ui::brightness_level_for_percent;
              assert(brightness_level_at(79) == 25);
              assert(brightness_level_at(80) == 50);
              assert(brightness_level_at(159) == 50);
              assert(brightness_level_at(160) == 100);
              assert(brightness_level_for_percent(0) == 0);
              assert(brightness_level_for_percent(25) == 25);
              assert(brightness_level_for_percent(37) == 25);
              assert(brightness_level_for_percent(38) == 50);
              assert(brightness_level_for_percent(74) == 50);
              assert(brightness_level_for_percent(75) == 100);

              // Footer: connecting during the grace period, then offline.
              using panel_ui::link_state_for;
              assert(link_state_for(true, 0) == panel_ui::LINK_ONLINE);
              assert(link_state_for(false, 0) == panel_ui::LINK_CONNECTING);
              assert(link_state_for(false, 20000) == panel_ui::LINK_OFFLINE);

              // Header: only a change in the printed whole degree repaints.
              using panel_ui::same_whole_degrees;
              assert(same_whole_degrees(70.2f, 69.6f));
              assert(!same_whole_degrees(70.6f, 70.2f));
              assert(same_whole_degrees(NAN, NAN));
              assert(!same_whole_degrees(NAN, 70.0f));
              return 0;
            }
            """
        )
        self.run_native(source, "firmware_logic_test")

    def run_native(
        self, source: str, name: str, stubs: dict[str, str] | None = None
    ) -> None:
        """Compile and run a C++17 program against the firmware headers."""

        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(
            compiler, "a C++17 compiler is required for native logic tests"
        )
        with tempfile.TemporaryDirectory(prefix="sticky-logic-test-") as temp:
            temp_path = Path(temp)
            for relative, text in (stubs or {}).items():
                stub = temp_path / "stubs" / relative
                stub.parent.mkdir(parents=True, exist_ok=True)
                stub.write_text(textwrap.dedent(text), encoding="utf-8")
            cpp = temp_path / f"{name}.cpp"
            executable = temp_path / (f"{name}.exe" if os.name == "nt" else name)
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
                    "-I",
                    str(temp_path / "stubs"),
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

    def test_native_room_model_actions_and_snapshot(self) -> None:
        # room_model.h only uses has_state()/state from ESPHome's entity classes.
        stubs = {
            "esphome/components/binary_sensor/binary_sensor.h": """
                #pragma once
                namespace esphome { namespace binary_sensor {
                struct BinarySensor {
                  bool state{false};
                  bool known{false};
                  bool has_state() const { return known; }
                  void set(bool on) { state = on; known = true; }
                };
                } }
            """,
            "esphome/components/sensor/sensor.h": """
                #pragma once
                namespace esphome { namespace sensor {
                struct Sensor {
                  float state{0.0f};
                  bool known{false};
                  bool has_state() const { return known; }
                  void set(float value) { state = value; known = true; }
                };
                } }
            """,
            "esphome/core/log.h": """
                #pragma once
                namespace esphome { template <typename... Args> void log_sink(Args &&...) {} }
                #define ESP_LOGE(...) ::esphome::log_sink(__VA_ARGS__)
                #define ESP_LOGI(...) ::esphome::log_sink(__VA_ARGS__)
            """,
        }
        source = textwrap.dedent(
            r"""
            #include "room_model.h"

            #include <cassert>
            #include <string>

            using esphome::binary_sensor::BinarySensor;
            using esphome::sensor::Sensor;

            struct Lights {
              BinarySensor power[4];
              Sensor level[4];
            };

            // Room 0: one single light. Room 1: a single light and a pair.
            static void build(panel_ui::RoomModel &rooms, Lights &l) {
              rooms.reset();
              assert(rooms.set_room(0, "Den", "i", {
                  panel_ui::make_card("Main", "a", "b", "light.den", &l.power[0], &l.level[0]),
              }));
              assert(rooms.set_room(1, "Bed", "i", {
                  panel_ui::make_card("Lamp", "a", "b", "light.lamp", &l.power[1], &l.level[1]),
                  panel_ui::make_paired_card("Pair", "a", "b",
                                             "light.left", &l.power[2], &l.level[2],
                                             "light.right", &l.power[3], &l.level[3]),
              }));
              assert(rooms.finalize(true, true));
            }

            int main() {
              panel_ui::RoomModel rooms;
              Lights l;
              build(rooms, l);
              for (auto &power : l.power) power.set(false);

              // A second tap can't undo the first while e-paper catches up.
              assert(rooms.begin_card_toggle(0, 0, 1000));
              assert(!rooms.begin_card_toggle(0, 0, 1200));
              assert(!rooms.begin_card_level(0, 0, 50, 1200));
              assert(rooms.card_service(0, 0) == "light.turn_on");
              rooms.mark_card_sent(0, 0, 1000);
              rooms.settle_pending(1500);
              assert(rooms.card_pending(0, 0));       // result not reported yet
              l.power[0].set(true);
              rooms.settle_pending(3500);
              assert(!rooms.card_pending(0, 0));      // confirmed and on screen
              assert(rooms.begin_card_toggle(0, 0, 4000));
              assert(rooms.card_service(0, 0) == "light.turn_off");

              // Sent but never confirmed: released and reported after 6 s.
              rooms.mark_card_sent(0, 0, 4000);
              assert(!rooms.expire_pending(4000 + panel_ui::PENDING_CONFIRM_MS));
              assert(rooms.expire_pending(4001 + panel_ui::PENDING_CONFIRM_MS));
              assert(!rooms.card_pending(0, 0));

              // Held for the link and never sent: released quietly.
              assert(rooms.begin_card_toggle(0, 0, 20000));
              assert(!rooms.expire_pending(20000 + panel_ui::PENDING_QUEUE_MS));
              assert(!rooms.expire_pending(20001 + panel_ui::PENDING_QUEUE_MS));
              assert(!rooms.card_pending(0, 0));

              // A brightness tap stays locked until that level is reported.
              l.level[0].set(64.0f);
              assert(rooms.card_level(0, 0) == 25);
              assert(rooms.begin_card_level(0, 0, 100, 40000));
              assert(rooms.pending_level(0, 0) == 100);
              rooms.mark_card_sent(0, 0, 40000);
              rooms.settle_pending(40500);
              assert(rooms.card_pending(0, 0));
              l.level[0].set(255.0f);
              rooms.settle_pending(41000);
              assert(!rooms.card_pending(0, 0));
              // A light that can't reach the level is released without a warning.
              assert(rooms.begin_card_level(0, 0, 50, 50000));
              rooms.mark_card_sent(0, 0, 50000);
              assert(!rooms.expire_pending(50001 + panel_ui::PENDING_CONFIRM_MS));
              assert(!rooms.card_pending(0, 0));

              // Whole-room switch: any light on turns the room off.
              l.power[3].set(true);
              assert(rooms.room_on_count(1) == 1);
              assert(rooms.begin_room_toggle(1, 60000));
              assert(!rooms.begin_room_toggle(1, 60100));
              assert(!rooms.begin_card_toggle(1, 0, 60100));
              assert(rooms.room_service(1) == "light.turn_off");
              assert(rooms.room_targets(1) == "light.lamp,light.left,light.right");
              rooms.mark_room_sent(1, 60000);
              l.power[3].set(false);
              rooms.settle_pending(62000);
              assert(!rooms.room_pending(1));
              assert(rooms.begin_room_toggle(1, 63000));
              assert(rooms.room_service(1) == "light.turn_on");
              rooms.cancel_room(1);
              assert(!rooms.room_pending(1));

              // Redraws are skipped when nothing the panel shows has changed.
              rooms.mark_menu_rendered(0);
              rooms.mark_cards_rendered(0);
              assert(rooms.menu_tile_current(0) && rooms.card_current(0, 0));
              l.level[0].set(128.0f);
              assert(rooms.menu_tile_current(0) && !rooms.card_current(0, 0));
              l.power[0].set(false);
              assert(!rooms.menu_tile_current(0));

              // Sleep snapshot: restored only for the same layout, replaced
              // by live states as they arrive.
              l.power[0].set(true);
              l.power[2].set(true);
              const uint32_t packed = rooms.pack_cache();
              assert(packed != 0);

              panel_ui::RoomModel woke;
              Lights fresh;
              build(woke, fresh);
              assert(!woke.all_displayable());
              assert(woke.pack_cache() == 0);        // nothing live to save
              assert(!woke.load_cache(0));
              assert(!woke.load_cache(packed ^ panel_ui::light_cache_layout_bits(1, 3)));
              assert(woke.load_cache(packed));
              assert(woke.all_displayable() && !woke.all_known());
              assert(woke.card_shown_on(0, 0) && !woke.card_shown_on(1, 0));
              assert(woke.card_shown_on(1, 1) && woke.room_on_count(1) == 1);
              assert(woke.card_level(0, 0) == 0);    // brightness is never restored
              // A tap on a restored state means what the panel showed.
              assert(woke.begin_card_toggle(1, 1, 100));
              assert(woke.card_service(1, 1) == "light.turn_off");
              woke.cancel_card(1, 1);
              fresh.power[0].set(false);             // live state wins
              assert(!woke.card_shown_on(0, 0));
              assert(woke.drop_cache());             // some cards never answered
              assert(!woke.card_shown_known(1, 1) && woke.menu_mode(1) == -1);
              // With nothing shown, a tap falls back to the stateless toggle.
              assert(woke.begin_card_toggle(1, 0, 200));
              assert(woke.card_service(1, 0) == "light.toggle");
              woke.mark_card_sent(1, 0, 200);
              woke.settle_pending(300);
              assert(woke.card_pending(1, 0));
              woke.settle_pending(200 + panel_ui::PENDING_UNVERIFIED_MS);
              assert(!woke.card_pending(1, 0));

              // A refused layout explains itself.
              panel_ui::RoomModel broken;
              Lights unused;
              broken.reset();
              assert(broken.set_room(1, "Den", "i", {
                  panel_ui::make_card("Main", "a", "b", "light.den", &unused.power[0],
                                      &unused.level[0]),
              }));
              assert(!broken.finalize(true, true));
              assert(!broken.valid() && !broken.all_displayable());
              assert(broken.error().find("start at 0") != std::string::npos);
              return 0;
            }
            """
        )
        self.run_native(source, "room_model_test", stubs)

    def test_light_actions_are_locked_and_held_for_the_link(self) -> None:
        text = CONFIG_PATH.read_text(encoding="utf-8")
        self.assertNotIn("allow_toggle", text)
        self.assertNotIn("cycle_room_card_brightness", text)

        for script, gate in (
            ("toggle_room_card", "begin_card_toggle"),
            ("set_room_card_brightness", "begin_card_level"),
            ("toggle_room", "begin_room_toggle"),
        ):
            with self.subTest(script=script):
                branch = self.scripts[script]["then"][0]["if"]
                self.assertIn(gate, branch["condition"]["lambda"])
                self.assertEqual(
                    branch["then"][0]["script.execute"]["id"], "send_light_action"
                )

        send = self.scripts["send_light_action"]["then"]
        self.assertEqual(send[0], {"script.execute": "touch_tick"})
        hold = send[2]["if"]
        self.assertIn("!panel_ui::ha_ready()", hold["condition"]["lambda"])
        self.assertEqual(hold["then"][0]["wait_until"]["timeout"], "10s")
        deliver = send[3]["if"]
        self.assertIn("panel_ui::ha_ready()", deliver["condition"]["lambda"])
        self.assertIn("mark_card_sent", deliver["then"][0]["lambda"])
        self.assertIn("cancel_card", deliver["else"][1]["lambda"])
        self.assertEqual(deliver["else"][-1], {"script.execute": "touch_reject"})

    def test_feedback_sounds_never_overlap_voice_capture(self) -> None:
        for script in ("touch_tick", "touch_reject"):
            with self.subTest(script=script):
                condition = self.scripts[script]["then"][0]["if"]["condition"]["lambda"]
                self.assertIn("!id(sticky_voice).is_running()", condition)
                self.assertIn("!id(start_sticky_assistant).is_running()", condition)

    def test_redesign_options_have_safe_defaults(self) -> None:
        substitutions = self.config["substitutions"]
        self.assertEqual(substitutions["menu_quick_toggle"], "true")
        self.assertEqual(substitutions["touch_tick_level"], "30%")
        self.assertEqual(substitutions["idle_clock_seconds"], "30")
        self.assertEqual(substitutions["idle_sleep_seconds"], "180")
        self.assertIn(substitutions["wifi_fast_connect"], ("true", "false"))

        small_icons = next(
            font for font in self.config["font"] if font["id"] == "icons_sm"
        )
        self.assertIn("\U000F0425", small_icons["glyphs"])

        # The snapshot survives deep sleep and is written only at sleep entry.
        cache = next(
            entry for entry in self.config["globals"] if entry["id"] == "light_cache"
        )
        self.assertTrue(cache["restore_value"])
        sleep_lambdas = [
            step["lambda"]
            for step in self.scripts["enter_deep_sleep"]["then"]
            if "lambda" in step
        ]
        self.assertTrue(any("pack_cache()" in body for body in sleep_lambdas))

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
