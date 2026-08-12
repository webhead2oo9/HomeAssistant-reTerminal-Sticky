// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Runtime room/card model for the Sticky dashboard.
//
// ESPHome still creates the concrete Home Assistant state components, while
// this model gives the display, touch router, and action scripts one reusable
// view of them. Capacities are fixed at compile time; configured counts are
// runtime values so the same UI supports one to six rooms and one to three
// cards per room. The optional camera and Assist tiles each reserve one of the
// six menu slots.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/log.h"
#include "sticky_logic.h"

namespace panel_ui {

static const char *const ROOM_MODEL_TAG = "room_model";

constexpr uint8_t MAX_ROOMS = 6;
constexpr uint8_t MAX_CARDS = 3;
constexpr uint8_t MAX_MEMBERS = 2;

constexpr uint8_t max_room_count(bool camera_enabled, bool assistant_enabled = false) {
  return MAX_ROOMS - (camera_enabled ? 1 : 0) - (assistant_enabled ? 1 : 0);
}

constexpr bool valid_room_count(uint8_t count, bool camera_enabled,
                                bool assistant_enabled = false) {
  return count >= 1 && count <= max_room_count(camera_enabled, assistant_enabled);
}

static_assert(valid_room_count(1, true, true), "one room plus camera and Assist must fit");
static_assert(valid_room_count(4, true, true), "four rooms plus camera and Assist must fit");
static_assert(!valid_room_count(5, true, true), "two special tiles reserve two slots");
static_assert(valid_room_count(5, true, false), "five rooms plus camera must fit");
static_assert(valid_room_count(5, false, true), "five rooms plus Assist must fit");
static_assert(valid_room_count(6, false, false), "six rooms must fit without special tiles");
static_assert(!valid_room_count(7, false, false), "the menu has only six slots");
static_assert(MAX_CARDS == 3, "the detail layout supports three cards");

// ESPHome package variables pass MDI glyphs as hexadecimal code points. Build
// the UTF-8 text at runtime so YAML/Jinja never has to serialize a private-use
// character through JSON surrogate escapes.
inline std::string mdi_icon(uint32_t codepoint) {
  if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return {};
  std::string result;
  if (codepoint <= 0x7F) {
    result.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return result;
}

struct MemberBinding {
  const char *entity_id{nullptr};
  esphome::binary_sensor::BinarySensor *power{nullptr};
  esphome::sensor::Sensor *brightness{nullptr};
};

struct CardBinding {
  const char *name{nullptr};
  std::string icon_on{};
  std::string icon_off{};
  std::array<MemberBinding, MAX_MEMBERS> members{};
  uint8_t member_count{0};

  bool configured() const {
    if (this->name == nullptr || this->name[0] == '\0' || this->icon_on.empty() ||
        this->icon_off.empty() ||
        this->member_count == 0 || this->member_count > MAX_MEMBERS)
      return false;
    for (uint8_t i = 0; i < this->member_count; i++) {
      const auto &member = this->members[i];
      if (member.entity_id == nullptr || member.entity_id[0] == '\0' || member.power == nullptr ||
          member.brightness == nullptr)
        return false;
    }
    return true;
  }

  bool known() const {
    if (!this->configured()) return false;
    for (uint8_t i = 0; i < this->member_count; i++) {
      const auto &member = this->members[i];
      if (member.power == nullptr || !member.power->has_state()) return false;
    }
    return true;
  }

  bool any_on() const {
    if (!this->known()) return false;
    for (uint8_t i = 0; i < this->member_count; i++) {
      if (this->members[i].power->state) return true;
    }
    return false;
  }

  // Action choice can still be safe while a paired card is only partly
  // initialized: one known ON member is enough to choose turn_off for both.
  bool any_known_on() const {
    for (uint8_t i = 0; i < this->member_count; i++) {
      auto *power = this->members[i].power;
      if (power != nullptr && power->has_state() && power->state) return true;
    }
    return false;
  }

  float brightness_percent() const {
    float raw = NAN;
    for (uint8_t i = 0; i < this->member_count; i++) {
      auto *sensor = this->members[i].brightness;
      if (sensor == nullptr || !sensor->has_state() || std::isnan(sensor->state)) continue;
      raw = std::isnan(raw) ? sensor->state : fmaxf(raw, sensor->state);
    }
    return std::isnan(raw) ? -1.0f : roundf(raw / 2.55f);
  }

  std::string service_targets() const {
    std::string result;
    for (uint8_t i = 0; i < this->member_count; i++) {
      const char *entity_id = this->members[i].entity_id;
      if (entity_id == nullptr || entity_id[0] == '\0') continue;
      if (!result.empty()) result += ',';
      result += entity_id;
    }
    return result;
  }
};

inline CardBinding make_card(const char *name, std::string icon_on, std::string icon_off,
                             const char *entity_id, esphome::binary_sensor::BinarySensor *power,
                             esphome::sensor::Sensor *brightness) {
  CardBinding card;
  card.name = name;
  card.icon_on = icon_on;
  card.icon_off = icon_off;
  card.members[0] = {entity_id, power, brightness};
  card.member_count = 1;
  return card;
}

inline CardBinding make_paired_card(const char *name, std::string icon_on, std::string icon_off,
                                    const char *entity_1, esphome::binary_sensor::BinarySensor *power_1,
                                    esphome::sensor::Sensor *brightness_1, const char *entity_2,
                                    esphome::binary_sensor::BinarySensor *power_2,
                                    esphome::sensor::Sensor *brightness_2) {
  CardBinding card;
  card.name = name;
  card.icon_on = icon_on;
  card.icon_off = icon_off;
  card.members[0] = {entity_1, power_1, brightness_1};
  card.members[1] = {entity_2, power_2, brightness_2};
  card.member_count = 2;
  return card;
}

struct RoomBinding {
  const char *name{nullptr};
  std::string icon{};
  std::array<CardBinding, MAX_CARDS> cards{};
  uint8_t card_count{0};
  bool configured{false};
  int rendered_menu_mode{-1};
  int rendered_card_mask{-1};
};

struct TouchPress {
  int x{-1};
  int y{-1};
  int page{-1};
  bool valid{false};
};

class RoomModel {
 public:
  void reset() {
    this->rooms_ = {};
    this->room_count_ = 0;
    this->active_room_ = 0;
    this->camera_enabled_ = true;
    this->assistant_enabled_ = false;
    this->valid_ = false;
    this->registration_error_ = false;
    this->touch_ = {};
    this->last_toggle_ms_ = {};
    this->last_brightness_ms_ = {};
  }

  bool set_room(int slot, const char *name, std::string icon,
                std::initializer_list<CardBinding> cards) {
    if (slot < 0 || slot >= MAX_ROOMS || name == nullptr || name[0] == '\0' || icon.empty() ||
        cards.size() == 0 || cards.size() > MAX_CARDS) {
      ESP_LOGE(ROOM_MODEL_TAG, "Invalid room slot=%d cards=%u", slot,
               static_cast<unsigned>(cards.size()));
      this->registration_error_ = true;
      return false;
    }
    auto &room = this->rooms_[static_cast<std::size_t>(slot)];
    if (room.configured) {
      ESP_LOGE(ROOM_MODEL_TAG, "Room slot %d configured more than once", slot);
      this->registration_error_ = true;
      return false;
    }
    room.name = name;
    room.icon = icon;
    room.card_count = static_cast<uint8_t>(cards.size());
    room.configured = true;
    uint8_t index = 0;
    for (const auto &card : cards) {
      if (!card.configured()) {
        ESP_LOGE(ROOM_MODEL_TAG, "Invalid card %u in room slot %d", index, slot);
        room = {};
        this->registration_error_ = true;
        return false;
      }
      room.cards[index++] = card;
    }
    return true;
  }

  bool finalize(bool camera_enabled, bool assistant_enabled = false) {
    this->camera_enabled_ = camera_enabled;
    this->assistant_enabled_ = assistant_enabled;
    this->room_count_ = 0;
    this->valid_ = false;
    if (this->registration_error_) {
      ESP_LOGE(ROOM_MODEL_TAG, "Room registration failed; refusing a partial layout");
      return false;
    }
    uint8_t configured_count = 0;
    bool gap = false;
    for (uint8_t i = 0; i < MAX_ROOMS; i++) {
      if (this->rooms_[i].configured) {
        if (gap) {
          ESP_LOGE(ROOM_MODEL_TAG, "Room slots must be contiguous from zero (gap before %u)", i);
          return false;
        }
        configured_count = i + 1;
      } else if (configured_count > 0 || i == 0) {
        gap = true;
      }
    }
    const uint8_t max_rooms = max_room_count(camera_enabled, assistant_enabled);
    if (!valid_room_count(configured_count, camera_enabled, assistant_enabled)) {
      ESP_LOGE(ROOM_MODEL_TAG,
               "Configured %u rooms; maximum is %u with camera=%s assist=%s",
               configured_count, max_rooms, camera_enabled ? "yes" : "no",
               assistant_enabled ? "yes" : "no");
      return false;
    }
    this->room_count_ = configured_count;
    this->valid_ = true;
    ESP_LOGI(ROOM_MODEL_TAG, "Configured %u rooms, camera=%s assist=%s",
             this->room_count_, camera_enabled ? "yes" : "no",
             assistant_enabled ? "yes" : "no");
    return true;
  }

  bool valid() const { return this->valid_; }
  uint8_t room_count() const { return this->room_count_; }
  bool camera_enabled() const { return this->valid_ && this->camera_enabled_; }
  int camera_slot() const {
    return camera_slot_for_layout(this->valid_, this->room_count_, this->camera_enabled_);
  }
  bool assistant_enabled() const { return this->valid_ && this->assistant_enabled_; }
  int assistant_slot() const {
    return assistant_slot_for_layout(this->valid_, this->room_count_, this->camera_enabled_,
                                     this->assistant_enabled_);
  }

  bool set_active_room(int room) {
    if (!this->room_valid(room)) return false;
    this->active_room_ = room;
    return true;
  }
  int active_room() const { return this->active_room_; }

  const RoomBinding *room(int index) const {
    return this->room_valid(index) ? &this->rooms_[index] : nullptr;
  }
  RoomBinding *room(int index) { return this->room_valid(index) ? &this->rooms_[index] : nullptr; }

  const CardBinding *card(int room_index, int card_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr || card_index < 0 || card_index >= room->card_count) return nullptr;
    return &room->cards[card_index];
  }

  bool all_known() const {
    if (!this->valid_) return false;
    for (uint8_t room_index = 0; room_index < this->room_count_; room_index++) {
      const auto &room = this->rooms_[room_index];
      for (uint8_t card_index = 0; card_index < room.card_count; card_index++) {
        if (!room.cards[card_index].known()) return false;
      }
    }
    return true;
  }

  bool room_known(int room_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr) return false;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      if (!room->cards[card_index].known()) return false;
    }
    return true;
  }

  int room_on_count(int room_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr) return 0;
    int count = 0;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      if (room->cards[card_index].any_on()) count++;
    }
    return count;
  }

  int menu_mode(int room_index) const {
    if (!this->room_known(room_index)) return -1;
    return this->room_on_count(room_index) > 0 ? 1 : 0;
  }

  int card_mask(int room_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr) return 0;
    int mask = 0;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      if (room->cards[card_index].any_on()) mask |= (1 << card_index);
    }
    return mask;
  }

  int rendered_menu_mode(int room_index) const {
    const auto *room = this->room(room_index);
    return room == nullptr ? -1 : room->rendered_menu_mode;
  }
  void mark_menu_rendered(int room_index) {
    auto *room = this->room(room_index);
    if (room != nullptr) room->rendered_menu_mode = this->menu_mode(room_index);
  }

  int rendered_card_mask(int room_index) const {
    const auto *room = this->room(room_index);
    return room == nullptr ? -1 : room->rendered_card_mask;
  }
  void mark_cards_rendered(int room_index) {
    auto *room = this->room(room_index);
    if (room != nullptr) room->rendered_card_mask = this->card_mask(room_index);
  }

  std::string service_targets(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    return card == nullptr ? std::string() : card->service_targets();
  }

  std::string toggle_service(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    if (card == nullptr) return "light.turn_on";
    if (card->member_count == 1) return "light.toggle";
    return card->any_known_on() ? "light.turn_off" : "light.turn_on";
  }

  int next_brightness_percent(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    const float pct = card == nullptr ? -1.0f : card->brightness_percent();
    return pct < 13.0f ? 25 : (pct < 38.0f ? 50 : (pct < 75.0f ? 100 : 25));
  }

  bool allow_toggle(int room_index, int card_index, uint32_t now) {
    return this->allow_action_(this->last_toggle_ms_, room_index, card_index, now, 1500);
  }

  bool allow_brightness(int room_index, int card_index, uint32_t now) {
    return this->allow_action_(this->last_brightness_ms_, room_index, card_index, now, 800);
  }

  static int menu_x(int slot) { return 20 + (slot % 3) * 260; }
  static int menu_y(int slot) { return 78 + (slot / 3) * 178; }

  int menu_slot_at(int x, int y) const {
    return menu_slot_for_layout(this->valid_, this->room_count_, this->camera_enabled_,
                                this->assistant_enabled_, x, y);
  }

  int card_x(int room_index, int card_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr || card_index < 0 || card_index >= room->card_count) return -1;
    if (room->card_count == 1) return 280;
    if (room->card_count == 2) return card_index == 0 ? 140 : 420;
    return 20 + card_index * 260;
  }

  int card_at_x(int room_index, int x) const {
    const auto *room = this->room(room_index);
    if (room == nullptr) return -1;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      const int left = this->card_x(room_index, card_index);
      if (x >= left && x <= left + 240) return card_index;
    }
    return -1;
  }

  void begin_touch(int x, int y, int page) { this->touch_ = {x, y, page, true}; }

  TouchPress take_touch() {
    const TouchPress result = this->touch_;
    this->touch_ = {};
    return result;
  }

 private:
  bool room_valid(int index) const {
    return this->valid_ && index >= 0 && index < this->room_count_ && this->rooms_[index].configured;
  }

  bool allow_action_(std::array<std::array<uint32_t, MAX_CARDS>, MAX_ROOMS> &timestamps,
                     int room_index, int card_index, uint32_t now, uint32_t debounce_ms) {
    if (this->card(room_index, card_index) == nullptr) return false;
    uint32_t &last = timestamps[room_index][card_index];
    if (last != 0 && static_cast<uint32_t>(now - last) < debounce_ms) return false;
    last = now == 0 ? 1 : now;
    return true;
  }

  std::array<RoomBinding, MAX_ROOMS> rooms_{};
  uint8_t room_count_{0};
  int active_room_{0};
  bool camera_enabled_{true};
  bool assistant_enabled_{false};
  bool valid_{false};
  bool registration_error_{false};
  TouchPress touch_{};
  std::array<std::array<uint32_t, MAX_CARDS>, MAX_ROOMS> last_toggle_ms_{};
  std::array<std::array<uint32_t, MAX_CARDS>, MAX_ROOMS> last_brightness_ms_{};
};

inline RoomModel &room_model() {
  static RoomModel model;
  return model;
}

}  // namespace panel_ui
