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
  // What the last render drew, used to skip no-op redraws. 0 = never drawn.
  int rendered_menu_signature{0};
  std::array<int, MAX_CARDS> rendered_card_signature{};
};

struct TouchPress {
  int x{-1};
  int y{-1};
  int page{-1};
  bool valid{false};
};

// Per-card action lock. A card takes one action at a time and stays locked
// until the reported result is on screen or the action times out, so a second
// tap during the e-paper refresh can't undo the first.
struct PendingAction {
  uint32_t started_ms{0};
  bool active{false};
  bool sent{false};        // false while held for the API link
  bool verify{false};      // a state was shown, so the result can be checked
  bool expect_on{false};
  uint8_t expect_level{0}; // 25/50/100, or 0 for a toggle
};

constexpr uint32_t PENDING_CONFIRM_MS = 6000;
constexpr uint32_t PENDING_QUEUE_MS = 15000;
constexpr uint32_t PENDING_UNVERIFIED_MS = 1500;

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
    this->error_.clear();
    this->touch_ = {};
    this->pending_ = {};
    this->cache_ = 0;
    this->cache_active_ = false;
  }

  bool set_room(int slot, const char *name, std::string icon,
                std::initializer_list<CardBinding> cards) {
    if (slot < 0 || slot >= MAX_ROOMS || name == nullptr || name[0] == '\0' || icon.empty() ||
        cards.size() == 0 || cards.size() > MAX_CARDS) {
      ESP_LOGE(ROOM_MODEL_TAG, "Invalid room slot=%d cards=%u", slot,
               static_cast<unsigned>(cards.size()));
      this->fail_("Room slot " + std::to_string(slot) +
                  " needs a name, an icon, and one to three cards.");
      return false;
    }
    auto &room = this->rooms_[static_cast<std::size_t>(slot)];
    if (room.configured) {
      ESP_LOGE(ROOM_MODEL_TAG, "Room slot %d configured more than once", slot);
      this->fail_("Room slot " + std::to_string(slot) + " is used by two room packages.");
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
        this->fail_("Card " + std::to_string(index + 1) + " in room slot " +
                    std::to_string(slot) + " is missing a label, icon, or entity.");
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
          this->fail_("Room slots must start at 0 with no gaps. Slot " +
                      std::to_string(i) + " follows an empty slot.");
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
      if (configured_count == 0) {
        this->fail_("No rooms are configured.");
      } else {
        this->fail_(std::to_string(configured_count) + " rooms are configured. This layout fits " +
                    std::to_string(max_rooms) + ".");
      }
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
  // Why finalize() refused the layout; shown on the menu.
  const std::string &error() const { return this->error_; }
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

  // --- Saved snapshot ---------------------------------------------------
  // A card without a live state shows its on/off state from the last sleep
  // entry until the snapshot is dropped.

  // Snapshot to store at sleep entry; 0 when any live state is missing.
  uint32_t pack_cache() const {
    if (!this->all_known()) return 0;
    uint32_t packed = LIGHT_CACHE_VALID | this->layout_signature_();
    for (uint8_t room_index = 0; room_index < this->room_count_; room_index++) {
      const auto &room = this->rooms_[room_index];
      for (uint8_t card_index = 0; card_index < room.card_count; card_index++) {
        if (room.cards[card_index].any_on())
          packed |= light_cache_state_bit(room_index, card_index);
      }
    }
    return packed;
  }

  // Accept a stored snapshot only when it was taken with this exact layout.
  bool load_cache(uint32_t packed) {
    this->cache_ = 0;
    this->cache_active_ = false;
    if (!this->valid_ || (packed & LIGHT_CACHE_VALID) == 0) return false;
    if ((packed & LIGHT_CACHE_LAYOUT_MASK) != this->layout_signature_()) return false;
    this->cache_ = packed;
    this->cache_active_ = true;
    return true;
  }

  bool cache_active() const { return this->cache_active_; }

  // Returns true when a card was still showing a saved state and must redraw.
  bool drop_cache() {
    const bool was_showing = this->cache_active_ && !this->all_known();
    this->cache_active_ = false;
    return was_showing;
  }

  // Every card has a live or saved state to show.
  bool all_displayable() const {
    return this->valid_ && (this->cache_active_ || this->all_known());
  }

  // What the panel should show for one card.
  bool card_shown_known(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    return card != nullptr && (card->known() || this->cache_active_);
  }

  bool card_shown_on(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    if (card == nullptr) return false;
    if (card->known()) return card->any_on();
    return this->cache_active_ &&
           (this->cache_ & light_cache_state_bit(room_index, card_index)) != 0;
  }

  // Marked level (25/50/100); 0 when off or unknown. Never from the snapshot.
  int card_level(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    if (card == nullptr || !card->any_on()) return 0;
    return brightness_level_for_percent(static_cast<int>(card->brightness_percent()));
  }

  bool room_known(int room_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr) return false;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      if (!this->card_shown_known(room_index, card_index)) return false;
    }
    return true;
  }

  int room_on_count(int room_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr) return 0;
    int count = 0;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      if (this->card_shown_on(room_index, card_index)) count++;
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
      if (this->card_shown_on(room_index, card_index)) mask |= (1 << card_index);
    }
    return mask;
  }

  // Encodes what a menu tile shows: unknown, or its on-count.
  int menu_signature(int room_index) const {
    return this->room_known(room_index) ? 2 + this->room_on_count(room_index) : 1;
  }

  // Encodes what a light card shows: unknown, or on/off plus level.
  int card_signature(int room_index, int card_index) const {
    if (!this->card_shown_known(room_index, card_index)) return 1;
    const int level = this->card_level(room_index, card_index);
    return 2 + (this->card_shown_on(room_index, card_index) ? 4 : 0) +
           (level == 25 ? 1 : (level == 50 ? 2 : (level == 100 ? 3 : 0)));
  }

  int rendered_menu_mode(int room_index) const {
    const auto *room = this->room(room_index);
    return room == nullptr ? -1 : room->rendered_menu_mode;
  }
  void mark_menu_rendered(int room_index) {
    auto *room = this->room(room_index);
    if (room == nullptr) return;
    room->rendered_menu_mode = this->menu_mode(room_index);
    room->rendered_menu_signature = this->menu_signature(room_index);
  }
  // True when the tile already on the panel matches the current state.
  bool menu_tile_current(int room_index) const {
    const auto *room = this->room(room_index);
    return room != nullptr && room->rendered_menu_signature == this->menu_signature(room_index);
  }

  int rendered_card_mask(int room_index) const {
    const auto *room = this->room(room_index);
    return room == nullptr ? -1 : room->rendered_card_mask;
  }
  void mark_cards_rendered(int room_index) {
    auto *room = this->room(room_index);
    if (room == nullptr) return;
    room->rendered_card_mask = this->card_mask(room_index);
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++)
      room->rendered_card_signature[card_index] = this->card_signature(room_index, card_index);
  }
  bool card_current(int room_index, int card_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr || card_index < 0 || card_index >= room->card_count) return false;
    return room->rendered_card_signature[card_index] ==
           this->card_signature(room_index, card_index);
  }

  std::string service_targets(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    return card == nullptr ? std::string() : card->service_targets();
  }

  // Every light entity in a room, for the menu's whole-room switch.
  std::string room_targets(int room_index) const {
    const auto *room = this->room(room_index);
    std::string result;
    if (room == nullptr) return result;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      const std::string targets = room->cards[card_index].service_targets();
      if (targets.empty()) continue;
      if (!result.empty()) result += ',';
      result += targets;
    }
    return result;
  }

  std::string toggle_service(int room_index, int card_index) const {
    const auto *card = this->card(room_index, card_index);
    if (card == nullptr) return "light.turn_on";
    if (card->member_count == 1) return "light.toggle";
    return card->any_known_on() ? "light.turn_off" : "light.turn_on";
  }

  // --- Action locks -----------------------------------------------------

  bool card_pending(int room_index, int card_index) const {
    return this->card(room_index, card_index) != nullptr &&
           this->pending_[room_index][card_index].active;
  }

  bool room_pending(int room_index) const {
    const auto *room = this->room(room_index);
    if (room == nullptr) return false;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      if (this->pending_[room_index][card_index].active) return true;
    }
    return false;
  }

  // Lock a card for a toggle; false if already locked. The target state comes
  // from what the panel shows, so a held tap still does what the user saw.
  bool begin_card_toggle(int room_index, int card_index, uint32_t now) {
    if (this->card(room_index, card_index) == nullptr) return false;
    auto &pending = this->pending_[room_index][card_index];
    if (pending.active) return false;
    pending = {};
    pending.active = true;
    pending.started_ms = now;
    pending.verify = this->card_shown_known(room_index, card_index);
    pending.expect_on = !this->card_shown_on(room_index, card_index);
    return true;
  }

  // Lock a card for a brightness level (25/50/100).
  bool begin_card_level(int room_index, int card_index, int level, uint32_t now) {
    if (this->card(room_index, card_index) == nullptr) return false;
    auto &pending = this->pending_[room_index][card_index];
    if (pending.active) return false;
    pending = {};
    pending.active = true;
    pending.started_ms = now;
    pending.verify = true;
    pending.expect_on = true;
    pending.expect_level = static_cast<uint8_t>(level);
    return true;
  }

  // Lock every card in a room. Any light shown on turns the room off,
  // otherwise all turn on. False if any card is already locked.
  bool begin_room_toggle(int room_index, uint32_t now) {
    const auto *room = this->room(room_index);
    if (room == nullptr || this->room_pending(room_index)) return false;
    const bool known = this->room_known(room_index);
    const bool target_on = this->room_on_count(room_index) == 0;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++) {
      auto &pending = this->pending_[room_index][card_index];
      pending = {};
      pending.active = true;
      pending.started_ms = now;
      pending.verify = known;
      pending.expect_on = target_on;
    }
    return true;
  }

  // Service for a locked toggle; toggle_service() when no state was shown.
  std::string card_service(int room_index, int card_index) const {
    if (this->card(room_index, card_index) == nullptr) return "light.turn_on";
    const auto &pending = this->pending_[room_index][card_index];
    if (pending.active && pending.verify)
      return pending.expect_on ? "light.turn_on" : "light.turn_off";
    return this->toggle_service(room_index, card_index);
  }

  // Service for a locked whole-room toggle.
  std::string room_service(int room_index) const {
    const auto *room = this->room(room_index);
    const bool target_on = room == nullptr || this->pending_[room_index][0].expect_on;
    return target_on ? "light.turn_on" : "light.turn_off";
  }

  int pending_level(int room_index, int card_index) const {
    return this->card(room_index, card_index) == nullptr
               ? 0
               : this->pending_[room_index][card_index].expect_level;
  }

  // The action was sent; start the confirmation window.
  void mark_card_sent(int room_index, int card_index, uint32_t now) {
    if (this->card(room_index, card_index) == nullptr) return;
    auto &pending = this->pending_[room_index][card_index];
    if (!pending.active) return;
    pending.sent = true;
    pending.started_ms = now;
  }

  void mark_room_sent(int room_index, uint32_t now) {
    const auto *room = this->room(room_index);
    if (room == nullptr) return;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++)
      this->mark_card_sent(room_index, card_index, now);
  }

  void cancel_card(int room_index, int card_index) {
    if (this->card(room_index, card_index) != nullptr) this->pending_[room_index][card_index] = {};
  }

  void cancel_room(int room_index) {
    const auto *room = this->room(room_index);
    if (room == nullptr) return;
    for (uint8_t card_index = 0; card_index < room->card_count; card_index++)
      this->pending_[room_index][card_index] = {};
  }

  // Release cards whose expected state has been reported. Call only while the
  // panel is idle with nothing queued, so that state is already on screen.
  // Unverified actions are released after PENDING_UNVERIFIED_MS.
  void settle_pending(uint32_t now) {
    for (uint8_t room_index = 0; room_index < this->room_count_; room_index++) {
      const auto &room = this->rooms_[room_index];
      for (uint8_t card_index = 0; card_index < room.card_count; card_index++) {
        auto &pending = this->pending_[room_index][card_index];
        if (!pending.active || !pending.sent) continue;
        const auto &card = room.cards[card_index];
        if (!pending.verify) {
          if (static_cast<uint32_t>(now - pending.started_ms) >= PENDING_UNVERIFIED_MS)
            pending = {};
          continue;
        }
        if (!card.known() || card.any_on() != pending.expect_on) continue;
        if (pending.expect_level != 0 &&
            this->card_level(room_index, card_index) != pending.expect_level)
          continue;
        pending = {};
      }
    }
  }

  // Release timed-out locks. Returns true if a sent action's light never
  // reached the expected on/off state.
  bool expire_pending(uint32_t now) {
    bool unanswered = false;
    for (uint8_t room_index = 0; room_index < this->room_count_; room_index++) {
      const auto &room = this->rooms_[room_index];
      for (uint8_t card_index = 0; card_index < room.card_count; card_index++) {
        auto &pending = this->pending_[room_index][card_index];
        if (!pending.active) continue;
        const uint32_t age = static_cast<uint32_t>(now - pending.started_ms);
        if (age <= (pending.sent ? PENDING_CONFIRM_MS : PENDING_QUEUE_MS)) continue;
        const auto &card = room.cards[card_index];
        if (pending.sent && pending.verify && card.known() &&
            card.any_on() != pending.expect_on)
          unanswered = true;
        pending = {};
      }
    }
    return unanswered;
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

  // Keep the first reason; later failures usually follow from it.
  void fail_(const std::string &message) {
    this->registration_error_ = true;
    if (this->error_.empty()) this->error_ = message;
  }

  uint32_t layout_signature_() const {
    uint32_t signature = 0;
    for (uint8_t room_index = 0; room_index < this->room_count_; room_index++)
      signature |= light_cache_layout_bits(room_index, this->rooms_[room_index].card_count);
    return signature;
  }

  std::array<RoomBinding, MAX_ROOMS> rooms_{};
  uint8_t room_count_{0};
  int active_room_{0};
  bool camera_enabled_{true};
  bool assistant_enabled_{false};
  bool valid_{false};
  bool registration_error_{false};
  std::string error_{};
  TouchPress touch_{};
  std::array<std::array<PendingAction, MAX_CARDS>, MAX_ROOMS> pending_{};
  uint32_t cache_{0};
  bool cache_active_{false};
};

inline RoomModel &room_model() {
  static RoomModel model;
  return model;
}

}  // namespace panel_ui
