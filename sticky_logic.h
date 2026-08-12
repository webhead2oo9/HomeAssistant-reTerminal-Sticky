// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Dependency-free firmware decisions shared by ESPHome components and native
// tests. Keeping these helpers free of ESP-IDF/ESPHome types lets the tests run
// the exact URL, text, and menu logic used on the device.

#include <cstddef>
#include <cstdint>
#include <string>

namespace panel_ui {

struct CameraSnapshotUrl {
  std::string url{};
  bool rejected{false};
};

inline std::string url_origin(const std::string &url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) return {};
  const size_t authority = scheme + 3;
  const size_t end = url.find_first_of("/?#", authority);
  return end == std::string::npos ? url : url.substr(0, end);
}

inline CameraSnapshotUrl resolve_camera_snapshot_url(std::string base,
                                                     const std::string &supplied,
                                                     const std::string &fallback_path) {
  while (!base.empty() && base.back() == '/') base.pop_back();
  const std::string fallback =
      base + ((!fallback_path.empty() && fallback_path.front() == '/') ? "" : "/") +
      fallback_path;
  if (supplied.empty()) return {fallback, false};

  const std::string supplied_origin = url_origin(supplied);
  if (supplied_origin.empty())
    return {base + (supplied.front() == '/' ? "" : "/") + supplied, false};

  const std::string base_origin = url_origin(base);
  if (!base_origin.empty() && supplied_origin == base_origin) return {supplied, false};
  return {fallback, true};
}

constexpr int camera_slot_for_layout(bool valid, uint8_t room_count, bool camera_enabled) {
  return valid && camera_enabled ? room_count : -1;
}

constexpr int assistant_slot_for_layout(bool valid, uint8_t room_count, bool camera_enabled,
                                        bool assistant_enabled) {
  return valid && assistant_enabled ? room_count + (camera_enabled ? 1 : 0) : -1;
}

constexpr int menu_slot_for_layout(bool valid, uint8_t room_count, bool camera_enabled,
                                   bool assistant_enabled, int x, int y) {
  if (!valid) return -1;
  const int row = (y >= 70 && y <= 245) ? 0 : ((y >= 250 && y <= 425) ? 1 : -1);
  const int col = (x >= 20 && x <= 260)
                      ? 0
                      : ((x >= 280 && x <= 520) ? 1 : ((x >= 540 && x <= 780) ? 2 : -1));
  if (row < 0 || col < 0) return -1;
  const int slot = row * 3 + col;
  const int used = room_count + (camera_enabled ? 1 : 0) + (assistant_enabled ? 1 : 0);
  return slot < used ? slot : -1;
}

static_assert(camera_slot_for_layout(false, 0, true) == -1,
              "invalid layouts must not expose a camera slot");
static_assert(assistant_slot_for_layout(false, 0, true, true) == -1,
              "invalid layouts must not expose an Assist slot");
static_assert(menu_slot_for_layout(false, 0, true, true, 20, 70) == -1,
              "invalid layouts must not expose touch targets");

inline size_t utf8_sequence_length(uint8_t lead) {
  if ((lead & 0x80) == 0) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

inline void trim_incomplete_utf8_tail(std::string &text) {
  if (text.empty()) return;
  size_t lead = text.size() - 1;
  while (lead > 0 && (static_cast<uint8_t>(text[lead]) & 0xC0) == 0x80) lead--;
  const size_t expected = utf8_sequence_length(static_cast<uint8_t>(text[lead]));
  if (expected > text.size() - lead) text.erase(lead);
}

// Voice text comes from Home Assistant, but it's still dynamic content. Keep
// it bounded, collapse whitespace/control characters, and never split UTF-8.
inline std::string clean_assistant_text(const std::string &input, size_t max_bytes = 320) {
  std::string result;
  result.reserve(input.size() < max_bytes ? input.size() : max_bytes);
  bool pending_space = false;
  for (unsigned char ch : input) {
    if (ch < 0x20 || ch == 0x7F || ch == ' ') {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      if (result.size() >= max_bytes) break;
      result.push_back(' ');
      pending_space = false;
    }
    if (result.size() >= max_bytes) break;
    result.push_back(static_cast<char>(ch));
  }
  trim_incomplete_utf8_tail(result);
  while (!result.empty() && result.back() == ' ') result.pop_back();
  return result;
}

inline void pop_assistant_utf8_codepoint(std::string &text) {
  if (text.empty()) return;
  size_t offset = text.size() - 1;
  while (offset > 0 && (static_cast<uint8_t>(text[offset]) & 0xC0) == 0x80) offset--;
  text.erase(offset);
}

}  // namespace panel_ui
