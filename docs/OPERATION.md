# Operation and power behavior

The Sticky has two practical personalities: an interactive Home Assistant panel
while it's awake, and a retained clock while it's quiet. This document explains
what the firmware does after installation, so normal e-paper and deep-sleep
behavior doesn't get mistaken for faults.

## Controls

### Touchscreen

- Tap the retained clock to open the menu and enable Wi-Fi.
- Tap a room tile to open that room.
- Tap the switch along a room tile's lower edge to turn the whole room on or
  off without opening it: if any light in the room is on, they all turn off;
  otherwise they all turn on. Set `menu_quick_toggle` to `"false"` to make the
  whole tile open the room instead.
- Tap the upper portion of a light card to toggle it.
- Tap **25**, **50**, or **100** at the bottom of a light card to set that
  brightness directly. This also turns the light on.
- Use the back label in a room to return to the menu.
- Use the clock label in the menu or a room to return to the retained clock.
- Tap a completed camera image to dismiss it.
- On the Assist page, use the wide lower control to start another request or
  cancel the active one.

A touch is routed using the page that was visible when the finger first went
down, so an image or page transition can't turn that same touch into a second
action on the destination page.

### Tap feedback

E-paper needs about two seconds to show a toggled light, so the buzzer is the
immediate acknowledgement:

- **One short tick:** the tap was accepted.
- **No tick:** the tap was ignored. A light card accepts one action at a time
  and stays locked until the result Home Assistant reports is on the panel, so
  a second tap during the refresh can't undo the first.
- **Two ticks:** the action couldn't be delivered, or Home Assistant never
  reported the result. The footer says which.

`touch_tick_level` sets the volume; `"0%"` mutes it. The buzzer stays silent
while the microphone is open.

A tap made before Home Assistant is reachable, typically in the first seconds
after a wake, is held and sent once the link is up. If no link arrives within
10 seconds the tap is dropped with two ticks.

### Physical buttons

All three buttons wake the device from deep sleep, enable Wi-Fi, and beep once.
A wake press beeps as soon as the panel boots, before the display changes. It
only returns to the menu; it won't also toggle a light.

- **Up** and **Down** return the clock to the menu. During ordinary interactive
  use they provide a short audible acknowledgement but do not navigate cards.
- **OK/Power** returns the clock to the menu. On a room page it toggles the
  first card. On the Assist page it starts a request when idle or cancels the
  active request.

The touchscreen is the primary navigation interface. The buttons stay
deliberately simple so an accidental wake press can't operate a light.

## Awake and sleep modes

The firmware classifies each boot into one of three modes:

| Mode | Typical cause | Network behavior | Awake time on battery |
| --- | --- | --- | --- |
| Interactive | Cold boot, button wake, OTA reboot, or USB detected | Wi-Fi enabled | Clock after `idle_clock_seconds` idle (default 30); deep sleep after `idle_sleep_seconds` idle (default 180) |
| Clock tick | Most scheduled wakes | Wi-Fi stays disabled | Sleeps again after the clock refresh completes, usually around 10 seconds |
| Home Assistant sync | Selected scheduled wakes | Wi-Fi connects and cached states refresh | Sleeps after a successful sync and at least 25 seconds, or after a 60-second cap; a camera recap may extend this to 120 seconds |

The device never enters deep sleep while GPIO9 reports external USB power.
Plugging in USB while it's asleep wakes it and turns the next timer-style boot
into an interactive window.

## Waking to the menu

Each time the panel enters deep sleep with a complete set of light states, it
saves their on/off values. The next wake paints the menu from that snapshot as
soon as the display is ready, while Wi-Fi and Home Assistant reconnect in the
background. Live states replace the snapshot as they arrive, and only tiles
that changed are redrawn.

The snapshot can be stale if lights changed while the panel slept. It is
discarded about eight seconds after Home Assistant connects, or once the link
is considered offline; any light that never reported then shows `--`.
Brightness levels are not saved. A cold boot, an OTA restart, or a changed room
layout has no usable snapshot, so those boots wait up to 12 seconds for Home
Assistant before the first menu.

The footer's left side reports the link:

| Footer | Meaning |
| --- | --- |
| *(empty)* | Connected to Home Assistant. |
| Connecting to Home Assistant... | No link yet, within 20 seconds of a wake or a dropped connection. |
| Home Assistant offline | No link after that grace period, or a held tap timed out. |
| No response from Home Assistant | An action was sent but the light's state never changed. Clears after 20 seconds. |

## Schedule

The RTC schedules the next wake on a wall-clock boundary:

- **07:00–22:59:** every 15 minutes;
- **23:00–06:59:** every hour;
- **Home Assistant refresh:** around the top of every daytime hour and every
  other hour overnight.

A few seconds are added after each boundary so the rendered time lands on the
new minute. If the RTC isn't valid yet, the firmware falls back to a 15-minute
wake and periodically attempts a Home Assistant sync.

The display retains its last image with no power at all. A state shown during
deep sleep is a snapshot, not proof that the Home Assistant entity still has
that state.

## E-paper refresh behavior

`display.update()` starts an asynchronous hardware operation. The firmware owns
a queue that waits for the display driver to go idle, merges dirty regions,
and preserves a pending full-refresh request.

- Small header, clock, and same-page state changes use regional partial
  updates. Choosing a different brightness level is one of these.
- A report that changes nothing the panel shows, such as a color change or a
  temperature that rounds to the same degree, doesn't refresh at all.
- Page changes and large black/white card inversions use a full cleaning
  waveform.
- After twelve accumulated partial refreshes, the next update is full.
- Assist waits for its Listening page to finish physically refreshing before
  starting the microphone. Its settled transcript and response get one full
  refresh because dense text looked gray with the fast waveform.
- A refresh that remains stuck for more than 20 seconds triggers a safe reboot,
  after which boot requests a full refresh.

A full-screen flash is expected. Judge ghosting over repeated toggles and
sleep cycles, not from a single frame.

## Camera lifecycle

Tapping the camera tile fetches a current frame from Home Assistant. The tile
reads **LOADING...** while the request is in flight. A completed image is shown
for up to 60 seconds and can be dismissed with a tap. A failed download leaves
the menu in place, sounds two ticks, shows **UNAVAILABLE** on the tile for a
few seconds, and logs a warning.

During a Home Assistant sync wake, a changed `last_triggered` value on the
configured doorbell automation can request the saved missed-ring image. A frame
already shown live within the previous five minutes suppresses the recap.

Camera downloads can take long enough that a sync wake stays active for up to
120 seconds. That's an upper bound, not the normal wake duration.

## Assist lifecycle

Assist is manual and one-shot:

1. A tap opens the Assist page and queues a full Listening refresh. If Home
   Assistant isn't connected yet, the page reads **CONNECTING...** and
   listening starts as soon as the link is up; after 10 seconds without one it
   reports that Home Assistant is offline.
2. After the display becomes idle, the device beeps, enables GPIO38, waits 250
   ms for the microphone rail, and starts capture.
3. The PDM stream is sent to Home Assistant over the encrypted ESPHome API.
4. When server-side voice activity detection ends the utterance, GPIO38 is
   turned off while transcription and intent handling continue.
5. The transcript and response are cleaned, bounded, stored in RAM, and drawn
   together.

Cancellation, API disconnect, pipeline error, completion, idle, and deep-sleep
entry also force the microphone rail off. Voice-assistant DEBUG logging is
overridden to WARN because DEBUG messages can contain verbatim household
speech.

## OTA updates

The native ESPHome OTA server is only reachable while Wi-Fi is active. The
most predictable way to update is to attach USB power, wake the device, and
run:

```shell
python -m esphome run reterminal-sticky.yaml
```

To catch a scheduled battery wake instead:

```shell
python tools/flash_on_wake.py reterminal-sticky.yaml
```

The helper resolves `reterminal-sticky.local` on each probe, optionally falls
back to a supplied IP address, waits for TCP port 3232, and starts `esphome
upload` from the configuration directory. A failed upload is retried until the
overall timeout expires.

Examples:

```shell
python tools/flash_on_wake.py reterminal-sticky.yaml --timeout 3600
python tools/flash_on_wake.py reterminal-sticky.yaml --hostname kitchen-panel.local
python tools/flash_on_wake.py reterminal-sticky.yaml --fallback-ip 192.0.2.10
```

Use your panel's real local address in place of the documentation-only example
IP. Don't remove USB power or interrupt the device during an upload.

## Battery and charging indicators

The BQ27220 fuel gauge supplies state of charge, voltage, and signed current.
The firmware throws out current readings outside ±1.5 A as implausible startup
or bus data.

**External Power** comes directly from GPIO9 and stays on when the battery is
full. **Battery Charging** additionally requires positive measured current over
20 mA, so it can turn off once a connected, full battery has tapered to nearly
zero current.

GPIO39 is the stock charger's active-low enable, and GPIO45 and GPIO46 hold
the device power path through boot and sleep. These signals are managed
explicitly; changing their polarity or startup behavior requires attended
hardware testing.

## Privacy expectations

The firmware keeps camera and voice data in volatile runtime buffers, except
for a missed-ring image that the user's Home Assistant automation intentionally
saves. It doesn't create recordings or transcript files.

Home Assistant and the selected voice providers still receive speech. A saved
file under Home Assistant's `/local` path may be reachable without
authentication. Review [Security](../SECURITY.md) for the complete trust and
credential boundaries.
