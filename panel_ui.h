// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Shared drawing helpers for the reTerminal Sticky ePaper panel.
//
// On this panel COLOR_OFF renders as ink (black) and COLOR_ON as paper
// (white); the YAML passes them in as ctx.ink / ctx.paper.

#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "esphome/components/display/display.h"
#include "esphome/core/color.h"
#include "esphome/components/epaper_spi/epaper_spi.h"
#include "esphome/components/runtime_image/runtime_image.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "sticky_logic.h"

namespace panel_ui {

using esphome::Color;
using esphome::display::BaseFont;
using esphome::display::Display;
using esphome::display::TextAlign;

struct Ctx {
  Display &it;
  BaseFont *small_f;   // 20px text
  BaseFont *value_f;   // 24px text
  BaseFont *medium_f;  // 32px text
  BaseFont *icons_sm;  // 28px icons
  BaseFont *icons_lg;  // 40px icons
  Color ink;
  Color paper;
};

// Logical dirty rectangle, using exclusive right/bottom coordinates. ESPHome's
// monochrome e-paper driver already transfers only the pixel bounds touched by
// the page lambda; keeping this region explicit lets the scheduler clear and
// clip that lambda so unchanged controller RAM isn't retransmitted.
struct DirtyRegion {
  int left{800};
  int top{480};
  int right{0};
  int bottom{0};

  bool empty() const { return left >= right || top >= bottom; }

  void clear() {
    left = 800;
    top = 480;
    right = 0;
    bottom = 0;
  }

  void include(int x1, int y1, int x2, int y2) {
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > 800) x2 = 800;
    if (y2 > 480) y2 = 480;
    if (x1 >= x2 || y1 >= y2) return;
    if (empty()) {
      left = x1;
      top = y1;
      right = x2;
      bottom = y2;
      return;
    }
    if (x1 < left) left = x1;
    if (y1 < top) top = y1;
    if (x2 > right) right = x2;
    if (y2 > bottom) bottom = y2;
  }
};

inline void include_dirty_region(int &left, int &top, int &right, int &bottom,
                                 int x1, int y1, int x2, int y2) {
  DirtyRegion region;
  region.left = left;
  region.top = top;
  region.right = right;
  region.bottom = bottom;
  region.include(x1, y1, x2, y2);
  left = region.left;
  top = region.top;
  right = region.right;
  bottom = region.bottom;
}

// The epaper_spi driver exposes only the partial/full cadence, not a way to
// pick the next waveform on demand, so set its protected update counter via
// the derived-class access idiom (adds no members; cast is layout-safe).
// Counter == 0 means the next refresh uses the full flash-clean waveform.
inline void set_update_count(esphome::epaper_spi::EPaperBase *d, uint8_t n) {
  struct Access : esphome::epaper_spi::EPaperBase {
    using esphome::epaper_spi::EPaperBase::update_count_;
  };
  static_cast<Access *>(d)->update_count_ = n;
}

// EPaperBase::update() only starts an asynchronous state machine. Expose its
// protected state so the YAML scheduler can keep requests queued until the
// previous physical refresh has actually reached IDLE.
inline bool refresh_idle(esphome::epaper_spi::EPaperBase *d) {
  struct Access : esphome::epaper_spi::EPaperBase {
    using esphome::epaper_spi::EPaperBase::state_;
  };
  return static_cast<Access *>(d)->state_ == esphome::epaper_spi::EPaperState::IDLE;
}

// Select the waveform immediately before starting a queued refresh.
inline void force_full_refresh(esphome::epaper_spi::EPaperBase *d) { set_update_count(d, 0); }

// The YAML scheduler owns the ghost-clean cadence, so an explicit partial
// request bypasses the driver's volatile automatic counter.
inline void mark_partial_refresh(esphome::epaper_spi::EPaperBase *d) { set_update_count(d, 1); }

// GPIO45 (PWR_HOLD) and GPIO46 (PWR_LOCK) keep the battery power path
// latched. GPIO39 is the active-low charger enable and must also remain LOW
// so plugging in while the MCU sleeps can charge the battery. GPIO38 keeps the
// active-high microphone rail disabled. The button pins get RTC pullups so the
// ext1 ANY_LOW wake mask never watches a floating input.
inline void hold_power_rails_for_sleep() {
  gpio_hold_en(GPIO_NUM_45);
  gpio_hold_en(GPIO_NUM_46);
  gpio_hold_en(GPIO_NUM_39);
  gpio_hold_en(GPIO_NUM_38);
  gpio_deep_sleep_hold_en();
  for (auto pin : {GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6}) {
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
  }
  // Internal RTC pullups only hold in deep sleep while the RTC_PERIPH power
  // domain stays on (~20 uA); otherwise the ext1 ANY_LOW mask can watch
  // floating lines and phantom-wake the panel.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
}

// Boot path: the pads are still held from the previous sleep. Callers MUST
// re-drive PWR_HOLD/PWR_LOCK high and CHG_EN low first so releasing the holds
// cannot glitch either the power latch or charger.
inline void release_power_rail_hold() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_45);
  gpio_hold_dis(GPIO_NUM_46);
  gpio_hold_dis(GPIO_NUM_39);
  gpio_hold_dis(GPIO_NUM_38);
}

// Rounded rectangles composed from core primitives (no native support).
inline void fill_rrect(Ctx &c, int x, int y, int w, int h, int r, Color col) {
  if (w > 2 * r) c.it.filled_rectangle(x + r, y, w - 2 * r, h, col);
  if (h > 2 * r) c.it.filled_rectangle(x, y + r, w, h - 2 * r, col);
  c.it.filled_circle(x + r, y + r, r, col);
  c.it.filled_circle(x + w - 1 - r, y + r, r, col);
  c.it.filled_circle(x + r, y + h - 1 - r, r, col);
  c.it.filled_circle(x + w - 1 - r, y + h - 1 - r, r, col);
}

inline void outline_rrect(Ctx &c, int x, int y, int w, int h, int r, int t, Color fg, Color bg) {
  fill_rrect(c, x, y, w, h, r, fg);
  fill_rrect(c, x + t, y + t, w - 2 * t, h - 2 * t, r - t, bg);
}

// NaN-safe pair max converted from a 0-255 brightness attribute to percent.
// Returns -1 when no value is known.
inline float bri_pct(float a, float b) {
  const float m = std::isnan(a) ? b : (std::isnan(b) ? a : fmaxf(a, b));
  return std::isnan(m) ? -1.0f : roundf(m / 2.55f);
}

// Header bar: optional back chevron, title, outdoor/indoor temps, battery
// gauge with charging bolt. Temps in Fahrenheit; NAN renders as "--".
inline void draw_header(Ctx &c, const char *title, bool back, float out_f, float in_f, float batt,
                        bool charging) {
  c.it.filled_rectangle(0, 0, 800, 64, c.ink);
  int tx = 20;
  if (back) {
    c.it.printf(12, 32, c.icons_sm, c.paper, TextAlign::CENTER_LEFT, "\U000F0141");
    tx = 46;
  }
  c.it.printf(tx, 32, c.medium_f, c.paper, TextAlign::CENTER_LEFT, "%s", title);

  outline_rrect(c, 384, 12, 56, 40, 12, 2, c.paper, c.ink);
  c.it.printf(412, 32, c.icons_sm, c.paper, TextAlign::CENTER, "\U000F04B2");

  c.it.printf(456, 32, c.icons_sm, c.paper, TextAlign::CENTER_LEFT, "\U000F0599");
  if (!std::isnan(out_f)) {
    c.it.printf(488, 32, c.value_f, c.paper, TextAlign::CENTER_LEFT, "%.0f\u00b0F", out_f);
  } else {
    c.it.printf(488, 32, c.value_f, c.paper, TextAlign::CENTER_LEFT, "--\u00b0F");
  }
  c.it.printf(596, 32, c.icons_sm, c.paper, TextAlign::CENTER_LEFT, "\U000F06A1");
  if (!std::isnan(in_f)) {
    c.it.printf(628, 32, c.value_f, c.paper, TextAlign::CENTER_LEFT, "%.0f\u00b0F", in_f);
  } else {
    c.it.printf(628, 32, c.value_f, c.paper, TextAlign::CENTER_LEFT, "--\u00b0F");
  }

  if (!std::isnan(batt)) {
    c.it.printf(734, 32, c.value_f, c.paper, TextAlign::CENTER_RIGHT, "%.0f", batt);
  } else {
    c.it.printf(734, 32, c.value_f, c.paper, TextAlign::CENTER_RIGHT, "--");
  }
  c.it.rectangle(740, 22, 36, 20, c.paper);
  c.it.rectangle(741, 23, 34, 18, c.paper);
  c.it.filled_rectangle(776, 28, 4, 8, c.paper);
  int fill_w = 0;
  if (charging) {
    fill_w = 30;
  } else if (!std::isnan(batt)) {
    const float b = batt < 0.0f ? 0.0f : (batt > 100.0f ? 100.0f : batt);
    fill_w = (int) (30.0f * b / 100.0f);
  }
  if (fill_w > 0) c.it.filled_rectangle(743, 25, fill_w, 14, c.paper);
  if (charging) {
    c.it.filled_triangle(762, 23, 751, 34, 759, 34, c.ink);
    c.it.filled_triangle(754, 40, 765, 29, 757, 29, c.ink);
  }
}

inline int text_width(Ctx &c, BaseFont *font, const std::string &text) {
  int x1, y1, width, height;
  c.it.get_text_bounds(0, 0, text.c_str(), font, TextAlign::TOP_LEFT,
                       &x1, &y1, &width, &height);
  return width;
}

inline bool split_menu_name(Ctx &c, BaseFont *font, const std::string &text,
                            int max_width, std::string &first, std::string &second) {
  size_t best = std::string::npos;
  int best_width = 0x7FFFFFFF;
  for (size_t pos = text.find(' '); pos != std::string::npos;
       pos = text.find(' ', pos + 1)) {
    size_t next = pos + 1;
    while (next < text.size() && text[next] == ' ') next++;
    if (pos == 0 || next >= text.size()) continue;
    const std::string left = text.substr(0, pos);
    const std::string right = text.substr(next);
    const int left_width = text_width(c, font, left);
    const int right_width = text_width(c, font, right);
    const int widest = left_width > right_width ? left_width : right_width;
    if (left_width <= max_width && right_width <= max_width && widest < best_width) {
      best = pos;
      best_width = widest;
      first = left;
      second = right;
    }
  }
  return best != std::string::npos;
}

inline std::string fit_text(Ctx &c, BaseFont *font, const std::string &text,
                            int max_width) {
  if (text_width(c, font, text) <= max_width) return text;
  std::string clipped = text;
  while (!clipped.empty() && text_width(c, font, clipped + "...") > max_width)
    pop_assistant_utf8_codepoint(clipped);
  return clipped + "...";
}

// Keep a light name inside the 168px title area to the right of its icon.
// Prefer the normal face and a natural two-line split; use the smaller face
// only for labels that still don't fit. This matters most in three-card rooms,
// where an overflowing label immediately crosses into the adjacent card.
inline void draw_card_name(Ctx &c, int x, const char *name, Color color) {
  constexpr int MAX_WIDTH = 152;
  const std::string text = name == nullptr ? std::string() : std::string(name);
  if (text_width(c, c.value_f, text) <= MAX_WIDTH) {
    c.it.printf(x + 72, 118, c.value_f, color, TextAlign::CENTER_LEFT,
                "%s", text.c_str());
    return;
  }

  std::string first;
  std::string second;
  if (split_menu_name(c, c.value_f, text, MAX_WIDTH, first, second)) {
    c.it.printf(x + 72, 101, c.value_f, color, TextAlign::CENTER_LEFT,
                "%s", first.c_str());
    c.it.printf(x + 72, 135, c.value_f, color, TextAlign::CENTER_LEFT,
                "%s", second.c_str());
    return;
  }

  if (text_width(c, c.small_f, text) <= MAX_WIDTH) {
    c.it.printf(x + 72, 118, c.small_f, color, TextAlign::CENTER_LEFT,
                "%s", text.c_str());
    return;
  }
  if (split_menu_name(c, c.small_f, text, MAX_WIDTH, first, second)) {
    c.it.printf(x + 72, 103, c.small_f, color, TextAlign::CENTER_LEFT,
                "%s", first.c_str());
    c.it.printf(x + 72, 133, c.small_f, color, TextAlign::CENTER_LEFT,
                "%s", second.c_str());
    return;
  }

  const std::string clipped = fit_text(c, c.small_f, text, MAX_WIDTH);
  c.it.printf(x + 72, 118, c.small_f, color, TextAlign::CENTER_LEFT,
              "%s", clipped.c_str());
}

// Light card: tap area on top (toggle), brightness cycle button below.
// ON cards invert to solid black. bri < 0 renders "--".
inline void draw_card(Ctx &c, int x, const char *name, const char *icon_on, const char *icon_off,
                      bool known, bool on, float bri) {
  const int CY = 78, CW = 240, CH = 352, R = 18;
  const int cx = x + CW / 2;
  const bool solid = known && on;
  const Color fg = solid ? c.paper : c.ink;
  const Color bg = solid ? c.ink : c.paper;
  if (solid) {
    fill_rrect(c, x, CY, CW, CH, R, c.ink);
  } else {
    outline_rrect(c, x, CY, CW, CH, R, 3, c.ink, c.paper);
  }
  c.it.printf(x + 40, 118, c.icons_lg, fg, TextAlign::CENTER, "%s", solid ? icon_on : icon_off);
  draw_card_name(c, x, name, fg);

  const char *label = !known ? "--" : (on ? "ON" : "OFF");
  const int pw = 110, ph = 44, py = 204, px = cx - pw / 2;
  if (solid) {
    fill_rrect(c, px, py, pw, ph, ph / 2, c.paper);
    c.it.printf(cx, py + ph / 2, c.value_f, c.ink, TextAlign::CENTER, "%s", label);
  } else {
    outline_rrect(c, px, py, pw, ph, ph / 2, 3, c.ink, c.paper);
    c.it.printf(cx, py + ph / 2, c.value_f, c.ink, TextAlign::CENTER, "%s", label);
  }

  const int bx = x + 40, by = 320, bw = 160, bh = 72;
  outline_rrect(c, bx, by, bw, bh, 16, 3, fg, bg);
  c.it.printf(cx - 54, by + 36, c.icons_sm, fg, TextAlign::CENTER, "\U000F00DF");
  if (bri >= 0.0f) {
    c.it.printf(cx + 12, by + 36, c.medium_f, fg, TextAlign::CENTER, "%.0f%%", bri);
  } else {
    c.it.printf(cx + 12, by + 36, c.medium_f, fg, TextAlign::CENTER, "--");
  }
}

// Draw a room name in the title area beside the icon. Prefer the normal 24px
// face, wrap at a word boundary when necessary, and fall back to the 20px face
// for unusually long names. Only an unbroken overlong word is ellipsized.
inline void draw_menu_name(Ctx &c, int x, int y, const char *name, Color color) {
  constexpr int MAX_WIDTH = 146;
  const std::string text = name == nullptr ? std::string() : std::string(name);
  if (text_width(c, c.value_f, text) <= MAX_WIDTH) {
    c.it.printf(x + 78, y + 52, c.value_f, color, TextAlign::CENTER_LEFT,
                "%s", text.c_str());
    return;
  }

  std::string first;
  std::string second;
  if (split_menu_name(c, c.value_f, text, MAX_WIDTH, first, second)) {
    c.it.printf(x + 78, y + 34, c.value_f, color, TextAlign::CENTER_LEFT,
                "%s", first.c_str());
    c.it.printf(x + 78, y + 68, c.value_f, color, TextAlign::CENTER_LEFT,
                "%s", second.c_str());
    return;
  }

  if (text_width(c, c.small_f, text) <= MAX_WIDTH) {
    c.it.printf(x + 78, y + 52, c.small_f, color, TextAlign::CENTER_LEFT,
                "%s", text.c_str());
    return;
  }
  if (split_menu_name(c, c.small_f, text, MAX_WIDTH, first, second)) {
    c.it.printf(x + 78, y + 36, c.small_f, color, TextAlign::CENTER_LEFT,
                "%s", first.c_str());
    c.it.printf(x + 78, y + 66, c.small_f, color, TextAlign::CENTER_LEFT,
                "%s", second.c_str());
    return;
  }

  std::string clipped = text;
  while (clipped.size() > 1 && text_width(c, c.small_f, clipped + "...") > MAX_WIDTH)
    clipped.pop_back();
  clipped += "...";
  c.it.printf(x + 78, y + 52, c.small_f, color, TextAlign::CENTER_LEFT,
              "%s", clipped.c_str());
}

// Menu tile: room icon + auto-wrapped name + "N OF M ON" summary; inverts
// when any configured card is on.
inline void draw_menu_tile(Ctx &c, int x, int y, const char *name, const char *icon, bool known,
                           int on_count, int total) {
  const int W = 240, H = 160, R = 18;
  const int cx = x + W / 2;
  const bool solid = known && on_count > 0;
  const Color fg = solid ? c.paper : c.ink;
  if (solid) {
    fill_rrect(c, x, y, W, H, R, c.ink);
  } else {
    outline_rrect(c, x, y, W, H, R, 3, c.ink, c.paper);
  }
  c.it.printf(x + 44, y + 52, c.icons_lg, fg, TextAlign::CENTER, "%s", icon);
  draw_menu_name(c, x, y, name, fg);
  if (!known) {
    c.it.printf(cx, y + 112, c.small_f, fg, TextAlign::CENTER, "--");
  } else if (on_count == 0) {
    c.it.printf(cx, y + 112, c.small_f, fg, TextAlign::CENTER, "OFF");
  } else {
    c.it.printf(cx, y + 112, c.small_f, fg, TextAlign::CENTER, "%d OF %d ON", on_count, total);
  }
}

// Footer: last-update time (12h) and an offline warning when the HA API
// link is down. hour24 < 0 skips the timestamp.
inline void draw_footer(Ctx &c, int hour24, int minute, bool api_online) {
  if (hour24 >= 0) {
    int h12 = hour24 % 12;
    if (h12 == 0) h12 = 12;
    c.it.printf(780, 434, c.small_f, c.ink, TextAlign::TOP_RIGHT, "Updated %d:%02d %s", h12, minute,
                hour24 < 12 ? "AM" : "PM");
  }
  if (!api_online) {
    c.it.printf(20, 428, c.icons_sm, c.ink, TextAlign::TOP_LEFT, "\U000F0026");
    c.it.printf(52, 434, c.small_f, c.ink, TextAlign::TOP_LEFT, "Home Assistant offline");
  }
}
namespace detail {
static const char *const DOW_NAME[8] = {"",       "Sunday",   "Monday", "Tuesday",
                                        "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const MONTH_NAME[13] = {"",     "January", "February", "March",  "April",
                                           "May",  "June",    "July",     "August", "September",
                                           "October", "November", "December"};
}  // namespace detail

// Sleep face: big left-column temperatures (readable across the room), giant
// clock, date, and a small battery gauge (fills solid while charging). The
// day/night icon rides the outdoor value.
inline void draw_sleep_face(Ctx &c, BaseFont *clock_f, BaseFont *temp_f, bool time_valid,
                            int hour24, int minute, int dow, int month, int dom, float out_f,
                            float in_f, bool sun_up, float batt, bool charging) {
  c.it.rectangle(752, 16, 30, 16, c.ink);
  c.it.rectangle(753, 17, 28, 14, c.ink);
  c.it.filled_rectangle(782, 21, 3, 6, c.ink);
  int fw = 0;
  if (charging) {
    fw = 24;
  } else if (!std::isnan(batt)) {
    const float b = batt < 0.0f ? 0.0f : (batt > 100.0f ? 100.0f : batt);
    fw = (int) (24.0f * b / 100.0f);
  }
  if (fw > 0) c.it.filled_rectangle(755, 19, fw, 10, c.ink);

  int x1, y1, w, h;
  char tbuf[16];
  c.it.printf(56, 140, c.small_f, c.ink, TextAlign::TOP_LEFT, "OUTSIDE");
  if (!std::isnan(out_f)) {
    snprintf(tbuf, sizeof tbuf, "%.0f\u00b0F", out_f);
  } else {
    snprintf(tbuf, sizeof tbuf, "--\u00b0F");
  }
  c.it.printf(56, 172, temp_f, c.ink, TextAlign::TOP_LEFT, "%s", tbuf);
  c.it.get_text_bounds(56, 172, tbuf, temp_f, TextAlign::TOP_LEFT, &x1, &y1, &w, &h);
  c.it.printf(x1 + w + 12, y1 + h / 2, c.icons_sm, c.ink, TextAlign::CENTER_LEFT, "%s",
              sun_up ? "\U000F0599" : "\U000F0594");
  c.it.printf(56, 268, c.small_f, c.ink, TextAlign::TOP_LEFT, "INSIDE");
  if (!std::isnan(in_f)) {
    c.it.printf(56, 300, temp_f, c.ink, TextAlign::TOP_LEFT, "%.0f\u00b0F", in_f);
  } else {
    c.it.printf(56, 300, temp_f, c.ink, TextAlign::TOP_LEFT, "--\u00b0F");
  }

  if (time_valid) {
    int h12 = hour24 % 12;
    if (h12 == 0) h12 = 12;
    char buf[8];
    snprintf(buf, sizeof buf, "%d:%02d", h12, minute);
    c.it.get_text_bounds(500, 215, buf, clock_f, TextAlign::CENTER, &x1, &y1, &w, &h);
    c.it.printf(500, 215, clock_f, c.ink, TextAlign::CENTER, "%s", buf);
    c.it.printf(x1 + w + 14, y1 + h - 12, c.medium_f, c.ink, TextAlign::BOTTOM_LEFT,
                hour24 < 12 ? "AM" : "PM");
    if (dow >= 1 && dow <= 7 && month >= 1 && month <= 12) {
      c.it.printf(500, 330, c.medium_f, c.ink, TextAlign::TOP_CENTER, "%s, %s %d",
                  detail::DOW_NAME[dow], detail::MONTH_NAME[month], dom);
    }
  } else {
    c.it.printf(500, 215, c.medium_f, c.ink, TextAlign::CENTER, "--:--");
  }
}

// --- Camera snapshot ---------------------------------------------------

// Grayscale buffer access on RuntimeImage (protected; same derived-class
// idiom as force_full_refresh above).
inline const uint8_t *grayscale_buffer(esphome::runtime_image::RuntimeImage *img) {
  struct Access : esphome::runtime_image::RuntimeImage {
    using esphome::runtime_image::RuntimeImage::buffer_;
  };
  return static_cast<Access *>(img)->buffer_;
}

// Dither the grayscale snapshot into rect (dx,dy,dw,dh): cover-scale with
// bilinear sampling, 1% histogram stretch for contrast (IR night frames are
// low-contrast), then serpentine Floyd-Steinberg. Only ink pixels are drawn;
// the page starts as clean paper. ~640x480 costs a few tens of ms, dwarfed
// by the 2 s panel update.
inline void draw_camera_image(Ctx &c, esphome::runtime_image::RuntimeImage *img, int dx, int dy,
                              int dw, int dh) {
  const uint8_t *buf = grayscale_buffer(img);
  const int sw = img->get_buffer_width(), sh = img->get_buffer_height();
  if (buf == nullptr || sw < 2 || sh < 2 || dw < 1 || dw > 800 || dh < 1) return;

  const float scale = fmaxf((float) dw / sw, (float) dh / sh);
  const float ox = (sw - dw / scale) * 0.5f, oy = (sh - dh / scale) * 0.5f;

  // Percentile stretch bounds from a 1/16 subsampled histogram.
  uint32_t hist[256] = {0};
  uint32_t n = 0;
  for (int y = 0; y < sh; y += 4) {
    const uint8_t *row = buf + (size_t) y * sw;
    for (int x = 0; x < sw; x += 4, n++) hist[row[x]]++;
  }
  int lo = 0, hi = 255;
  const uint32_t cut = n / 100;
  for (uint32_t acc = 0; lo < 256; lo++) {
    acc += hist[lo];
    if (acc > cut) break;
  }
  for (uint32_t acc = 0; hi >= 0; hi--) {
    acc += hist[hi];
    if (acc > cut) break;
  }
  if (hi - lo < 32) { lo = 0; hi = 255; }  // near-flat frame: don't amplify noise
  const float gain = 255.0f / (float) (hi - lo);

  // Error rows carry diffusion scaled by 16; +2 index guard skips bound checks.
  static int16_t err_rows[2][804];
  memset(err_rows, 0, sizeof err_rows);

  for (int y = 0; y < dh; y++) {
    int16_t *cur = err_rows[y & 1];
    int16_t *nxt = err_rows[(y + 1) & 1];
    memset(nxt, 0, sizeof err_rows[0]);
    float sy = oy + (y + 0.5f) / scale - 0.5f;
    if (sy < 0) sy = 0;
    if (sy > sh - 1.001f) sy = sh - 1.001f;
    const int y0 = (int) sy;
    const float fy = sy - y0;
    const uint8_t *r0 = buf + (size_t) y0 * sw;
    const uint8_t *r1 = r0 + sw;
    const bool ltr = (y & 1) == 0;
    for (int i = 0; i < dw; i++) {
      const int x = ltr ? i : dw - 1 - i;
      float sx = ox + (x + 0.5f) / scale - 0.5f;
      if (sx < 0) sx = 0;
      if (sx > sw - 1.001f) sx = sw - 1.001f;
      const int x0 = (int) sx;
      const float fx = sx - x0;
      float g = (1 - fy) * ((1 - fx) * r0[x0] + fx * r0[x0 + 1]) +
                fy * ((1 - fx) * r1[x0] + fx * r1[x0 + 1]);
      g = (g - lo) * gain;
      const int e_i = x + 2;
      const float v = g + cur[e_i] * (1.0f / 16.0f);
      const bool black = v < 128.0f;
      if (black) c.it.draw_pixel_at(dx + x, dy + y, c.ink);
      const int e = (int) (v - (black ? 0.0f : 255.0f));
      if (ltr) {
        cur[e_i + 1] += e * 7;
        nxt[e_i - 1] += e * 3;
        nxt[e_i] += e * 5;
        nxt[e_i + 1] += e;
      } else {
        cur[e_i - 1] += e * 7;
        nxt[e_i + 1] += e * 3;
        nxt[e_i] += e * 5;
        nxt[e_i - 1] += e;
      }
    }
  }
}

// Camera page: snapshot fills the left 640px at native 1:1 (the doorbell's
// 4:3 frame matches the 480px height exactly); right sidebar carries icon,
// title, capture time, and a close affordance (any tap dismisses).
inline void draw_camera_page(Ctx &c, esphome::runtime_image::RuntimeImage *img, const char *title,
                             bool time_valid, int hour24, int minute) {
  const int IMG_W = 640;
  if (img->is_loaded()) {
    draw_camera_image(c, img, 0, 0, IMG_W, 480);
  } else {
    c.it.printf(IMG_W / 2, 205, c.icons_lg, c.ink, TextAlign::CENTER, "\U000F07AE");
    c.it.printf(IMG_W / 2, 258, c.value_f, c.ink, TextAlign::CENTER, "No snapshot yet");
  }
  c.it.filled_rectangle(IMG_W, 0, 2, 480, c.ink);

  const int cx = IMG_W + 81;
  c.it.printf(cx, 44, c.icons_lg, c.ink, TextAlign::CENTER, "\U000F0869");
  char buf[32];
  snprintf(buf, sizeof buf, "%s", title);
  int line_y = 88;
  for (char *tok = strtok(buf, " "); tok != nullptr; tok = strtok(nullptr, " ")) {
    c.it.printf(cx, line_y, c.medium_f, c.ink, TextAlign::TOP_CENTER, "%s", tok);
    line_y += 38;
  }
  if (time_valid) {
    int h12 = hour24 % 12;
    if (h12 == 0) h12 = 12;
    c.it.printf(cx, 216, c.value_f, c.ink, TextAlign::TOP_CENTER, "%d:%02d %s", h12, minute,
                hour24 < 12 ? "AM" : "PM");
  }
  outline_rrect(c, IMG_W + 17, 408, 128, 44, 12, 2, c.ink, c.paper);
  c.it.printf(cx, 430, c.value_f, c.ink, TextAlign::CENTER, "CLOSE");
}

// Menu tile for the camera: outline style, icon + name + hint.
inline void draw_camera_tile(Ctx &c, int x, int y, const char *name, const char *icon) {
  const int W = 240, H = 160, R = 18;
  outline_rrect(c, x, y, W, H, R, 3, c.ink, c.paper);
  c.it.printf(x + 44, y + 52, c.icons_lg, c.ink, TextAlign::CENTER, "%s", icon);
  c.it.printf(x + 78, y + 52, c.value_f, c.ink, TextAlign::CENTER_LEFT, "%s", name);
  c.it.printf(x + W / 2, y + 112, c.small_f, c.ink, TextAlign::CENTER, "TAP TO VIEW");
}

inline void draw_assistant_tile(Ctx &c, int x, int y, const char *name) {
  const int W = 240, H = 160, R = 18;
  outline_rrect(c, x, y, W, H, R, 3, c.ink, c.paper);
  c.it.printf(x + 44, y + 52, c.icons_lg, c.ink, TextAlign::CENTER, "\U000F036C");
  c.it.printf(x + 78, y + 52, c.value_f, c.ink, TextAlign::CENTER_LEFT, "%s", name);
  c.it.printf(x + W / 2, y + 112, c.small_f, c.ink, TextAlign::CENTER, "TAP TO TALK");
}

inline std::string fit_assistant_text(Ctx &c, BaseFont *font,
                                      const std::string &text, int max_width) {
  return fit_text(c, font, text, max_width);
}

inline std::vector<std::string> wrap_assistant_text(Ctx &c, BaseFont *font,
                                                     const std::string &input,
                                                     int max_width, size_t max_lines) {
  std::vector<std::string> words;
  const std::string text = clean_assistant_text(input);
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find(' ', start);
    words.push_back(text.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }

  std::vector<std::string> lines;
  size_t word = 0;
  while (word < words.size() && lines.size() < max_lines) {
    if (lines.size() + 1 == max_lines) {
      std::string remainder = words[word++];
      while (word < words.size()) remainder += " " + words[word++];
      lines.push_back(fit_assistant_text(c, font, remainder, max_width));
      break;
    }

    std::string line = words[word++];
    while (word < words.size()) {
      const std::string candidate = line + " " + words[word];
      if (text_width(c, font, candidate) > max_width) break;
      line = candidate;
      word++;
    }
    lines.push_back(fit_assistant_text(c, font, line, max_width));
  }
  return lines;
}

inline const char *assistant_status(int phase, bool connected) {
  if (!connected) return "HOME ASSISTANT OFFLINE";
  switch (phase) {
    case 1:
      return "LISTENING";
    case 2:
      return "TRANSCRIBING";
    case 3:
      return "DONE";
    case 4:
      return "TRY AGAIN";
    default:
      return "READY";
  }
}

inline void draw_assistant_page(Ctx &c, int phase, bool connected,
                                const std::string &heard, const std::string &response) {
  outline_rrect(c, 20, 78, 760, 56, 14, 2, c.ink, c.paper);
  c.it.printf(50, 106, c.icons_sm, c.ink, TextAlign::CENTER, "\U000F036C");
  c.it.printf(82, 106, c.value_f, c.ink, TextAlign::CENTER_LEFT, "%s",
              assistant_status(phase, connected));
  const char *hint = "Tap START to speak";
  if (phase == 1)
    hint = "Speak now - I'll stop after a pause";
  else if (phase == 2)
    hint = "Microphone off - waiting for text";
  else if (phase == 3)
    hint = "Tap ASK AGAIN for another";
  c.it.printf(760, 106, c.small_f, c.ink, TextAlign::CENTER_RIGHT, "%s", hint);

  outline_rrect(c, 20, 146, 760, 92, 14, 2, c.ink, c.paper);
  c.it.printf(42, 154, c.small_f, c.ink, TextAlign::TOP_LEFT, "YOU");
  const std::string heard_text = heard.empty() ? "Your words will appear here" : heard;
  const auto heard_lines = wrap_assistant_text(c, c.value_f, heard_text, 716, 2);
  for (size_t line = 0; line < heard_lines.size(); line++)
    c.it.printf(42, 190 + static_cast<int>(line) * 28, c.value_f, c.ink,
                TextAlign::CENTER_LEFT, "%s", heard_lines[line].c_str());

  outline_rrect(c, 20, 248, 760, 100, 14, 2, c.ink, c.paper);
  c.it.printf(42, 258, c.small_f, c.ink, TextAlign::TOP_LEFT, "ASSIST");
  const std::string answer_text = response.empty() ? "The answer will appear here" : response;
  const auto answer_lines = wrap_assistant_text(c, c.small_f, answer_text, 716, 3);
  for (size_t line = 0; line < answer_lines.size(); line++)
    c.it.printf(42, 290 + static_cast<int>(line) * 24, c.small_f, c.ink,
                TextAlign::CENTER_LEFT, "%s", answer_lines[line].c_str());

  // Keep the same white/outline polarity in every phase. Large black-to-white
  // button inversions are unreliable with this panel's fast partial waveform.
  outline_rrect(c, 20, 358, 760, 58, 14, 3, c.ink, c.paper);
  if (phase == 1 || phase == 2) {
    c.it.printf(400, 387, c.value_f, c.ink, TextAlign::CENTER, "CANCEL REQUEST");
  } else {
    c.it.printf(400, 387, c.value_f, c.ink, TextAlign::CENTER, "%s",
                (heard.empty() && response.empty()) ? "START LISTENING" : "ASK AGAIN");
  }
}

}  // namespace panel_ui
