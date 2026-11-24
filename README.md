# reTerminal Sticky for ESPHome

This project turns the 3.97-inch Seeed Studio reTerminal Sticky into a
battery-friendly Home Assistant panel. Most of the time it's a quiet e-paper
clock and status display; tap it to control lights, view a camera snapshot, or
fire off a one-shot voice request to Home Assistant Assist.

The firmware was built for one real panel. It's a solid starting point for a
room controller, not a general-purpose dashboard builder. Rooms are assembled from small ESPHome packages, while the
display, touch routing, power management, and refresh scheduling stay shared.

> **Project status:** early, source-only release. The current baseline is
> ESPHome **2026.7.4** on a stock reTerminal Sticky. Charging, sleep and wake,
> USB installation, OTA, touch, camera snapshots, Assist capture, and repeated
> full and partial refreshes have been exercised on hardware.

This is an unofficial community project, not affiliated with Seeed Studio,
ESPHome, or Home Assistant.

## At a glance

- Six touch-menu positions shared by one to six rooms and the optional camera
  and Assist tiles.
- One to three light cards per room, each with a toggle and direct 25/50/100%
  brightness levels, plus an optional whole-room switch on every menu tile.
- A menu that paints from saved light states right after a wake, with taps held
  until Home Assistant reconnects.
- Indoor and outdoor temperature, battery, charging state, time, and date.
- Battery-first deep sleep with timer, button, and USB wake sources.
- A dithered Home Assistant camera image sized for the monochrome panel.
- Manual, one-shot Home Assistant Assist using the onboard microphone. The
  transcript and response appear on screen; there's no speaker or wake word.
- Queued regional e-paper updates with periodic full cleaning refreshes.

The panel is not a real-time alarm or doorbell endpoint. In deep sleep the CPU
and Wi-Fi are off, so Home Assistant can't push anything to it. The retained
e-paper image may be intentionally stale.

## Before you start

You'll need:

- a stock reTerminal Sticky and a data-capable USB cable;
- a Home Assistant installation with the ESPHome integration;
- Python and ESPHome 2026.7.4 on the computer doing the build;
- one or more Home Assistant `light` entities; and
- an Assist pipeline and/or camera entity if you enable those tiles.

A few constraints are worth reading before you change the example:

- The interface displays temperatures in Fahrenheit. The Sticky's onboard
  SHT40 supplies the indoor reading; supply an outdoor Home Assistant sensor
  whose state is already in degrees Fahrenheit.
- Every card controls one Home Assistant light entity. Use a Home Assistant
  Light Group when one card should represent several lamps.
- Camera and Assist each occupy one of the six menu positions. Enabling both
  leaves room for four rooms.
- The power-related GPIOs are hardware-critical. Don't casually move or invert
  GPIO39, GPIO45, GPIO46, or GPIO38. See [Security and hardware
  safety](SECURITY.md#physical-and-power-safety).
- Builds contain local credentials and entity IDs, so this repository doesn't
  provide a generic firmware binary.

Seeed's [Sticky hardware overview](https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/)
describes the device. The GPIO mapping here is the one tested on the stock
board used for development. ESPHome lists the Sticky as a predefined
integrated board in its [e-paper display documentation](https://esphome.io/components/display/epaper_spi/#supported-integrated-display-boards).

## Quick start

### 1. Install the tested ESPHome release

Use a virtual environment so another ESPHome project can't silently change the
version used here.

```shell
python -m venv .venv
```

Activate it with `. .venv/bin/activate` on macOS/Linux or
`.\.venv\Scripts\Activate.ps1` in PowerShell, then install ESPHome:

```shell
python -m pip install "esphome==2026.7.4"
```

### 2. Create local configuration files

Run these commands from the repository root. Keeping the YAML here preserves
the relative paths to `packages/`, `fonts/`, and the C++ headers.

```shell
cp reterminal-sticky.example.yaml reterminal-sticky.yaml
cp secrets.example.yaml secrets.yaml
```

In PowerShell, use `Copy-Item` instead of `cp`. Both local files are ignored
by Git.

### 3. Fill in `secrets.yaml`

Replace every placeholder. This cross-platform Python command generates the
32-byte native API key:

```shell
python -c "import base64,secrets; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

Use unique passwords for OTA and the fallback access point. For camera access,
create a Home Assistant long-lived access token from your user profile and keep
the literal `Bearer ` prefix in `ha_bearer`. If the camera tile will stay
disabled, the placeholder token can stay too; just don't try to use the camera
feature with it.

### 4. Describe your installation

Edit `substitutions:` and `packages:` near the top of
`reterminal-sticky.yaml`:

- give the device a unique `device_name`;
- enter the Home Assistant origin and entity IDs;
- enable or disable the camera and Assist menu positions; and
- replace the example room packages with your rooms and lights.

The complete field reference, room examples, camera automation, and Assist
setup live in [Configuration](docs/CONFIGURATION.md).

### 5. Validate and compile

```shell
python -m esphome config reterminal-sticky.yaml
python -m esphome compile reterminal-sticky.yaml
```

Fix every configuration error before connecting the device.

### 6. Install over USB

Keep the Sticky on USB power for the first installation:

```shell
python -m esphome run reterminal-sticky.yaml
```

Pick the serial device when ESPHome asks. The board exposes UART0 through its
CH343P USB-to-UART bridge; if no serial port shows up, check the cable and host
driver before touching the YAML.

### 7. Finish the Home Assistant setup

Keep the panel on USB while pairing it. Home Assistant should discover it
automatically:

1. Open **Settings → Devices & services**.
2. Find the new ESPHome card under **Discovered** and select **Add**.
3. Confirm the device shown in the discovery dialog and select **Submit**.
4. When prompted, paste the value of `api_encryption_key` from `secrets.yaml`:
   only the key value, not the `api_encryption_key:` label and not the camera's
   `Bearer` token.
5. Finish the dialog and optionally assign the device to an area.

If the device isn't discovered, select **Add integration → ESPHome** and enter
its current hostname or IP address. The native ESPHome API port is `6053`.
Successful setup opens a device page with the Buzzer control and the panel's
battery, charging, button, environment, external-power, and Assist entities.

Home Assistant's generic “make sure the device's YAML configuration declares
an `api` section” error can also be caused by a stale or mistyped IP address.
It doesn't necessarily mean the checked-in `api:` block is missing. Use the
current address reported in the serial log and follow the focused checks in
[Troubleshooting](docs/TROUBLESHOOTING.md#home-assistant-says-the-device-has-no-api-section).

Finally, open the ESPHome integration entry, open its options (the cogwheel or
**Configure** control), and enable **Allow the device to perform Home Assistant
actions**; the panel's light controls need it. The API stays encrypted by the
key in `secrets.yaml`.

### 8. Smoke-test it while attached to USB

Before testing deep sleep, verify that:

1. the menu appears after Home Assistant connects;
2. every room opens and reports the expected light state;
3. toggle and brightness controls change the intended entity;
4. the three physical buttons and touchscreen respond;
5. the battery and external-power entities look plausible; and
6. any enabled camera or Assist tile completes one request.

USB power keeps the device awake, which makes logs and recovery much easier.
See [Troubleshooting](docs/TROUBLESHOOTING.md) if one of these checks fails.

## Everyday behavior

On battery, the interactive UI returns to the clock after 30 seconds and enters
deep sleep after three minutes without a touch or button press (both are
substitutions). The retained clock is updated every 15 minutes from 07:00
through 22:59 and hourly overnight. Most of those short wakes use the onboard
RTC and never even start Wi-Fi. Home
Assistant state is refreshed roughly hourly during the day and every other hour
overnight.

On USB power, the UI still returns to the clock after 30 seconds, but the device
never enters deep sleep. Touching the clock or pressing any physical button
brings back the menu and enables Wi-Fi.

Full-screen e-paper flashes are normal. Small same-page changes use faster
regional refreshes, but page changes and every thirteenth accumulated refresh
use a full cleaning waveform to control ghosting.

[Operation and power behavior](docs/OPERATION.md) explains the controls, wake
modes, camera dismissal, Assist lifecycle, and OTA workflow in detail.

## Updating a sleeping panel

With the panel awake on USB, use the normal ESPHome upload flow:

```shell
python -m esphome run reterminal-sticky.yaml
```

For a battery-powered panel, the helper waits for a scheduled wake and starts
the native ESPHome OTA upload as soon as port 3232 becomes reachable:

```shell
python tools/flash_on_wake.py reterminal-sticky.yaml
```

Run `python tools/flash_on_wake.py --help` for hostname, fallback IP, polling,
and timeout options. The helper can't wake the device itself; it just waits for
the next wake the firmware has already scheduled.

## Customizing the interface

Rooms use one of these package shapes:

- `packages/room-1-card.yaml`
- `packages/room-2-cards.yaml`
- `packages/room-3-cards.yaml`

Room slots must start at zero and stay consecutive, and package keys must be
unique and match `[a-z][a-z0-9_]*`. The package picks the number of cards; every
room still uses the same shared detail page and touch logic.

The shortest useful example looks like this:

```yaml
packages:
  living_room: !include
    file: packages/room-1-card.yaml
    vars:
      slot: "0"
      key: living_room
      label: "Living Room"
      icon: "F04B9"
      card_1_label: "Main Lights"
      card_1_entity: light.living_room
      card_1_icon_on: "F04B9"
      card_1_icon_off: "F156D"
```

The full rules, including menu capacity, icon glyphs, feature toggles, camera
authentication, and examples, are in [Configuration](docs/CONFIGURATION.md).

## Repository map

```text
reterminal-sticky.example.yaml    Sanitized ESPHome configuration to copy
secrets.example.yaml              Required secret names and safe placeholders
packages/                         One-, two-, and three-card room packages
panel_ui.h                        Drawing, dirty-region, and e-paper helpers
room_model.h                      Shared room state, actions, and touch routing
sticky_logic.h                    Dependency-free URL, text, and menu decisions
docs/                             User guides
fonts/                            Pinned Inter and Material Design Icons fonts
tools/flash_on_wake.py            Wait-for-wake OTA helper
tools/validate_room_topologies.py Supported-layout validation
tests/                            Native logic and parsed-configuration tests
.github/workflows/validate.yml    Privacy scan, tests, and firmware build
```

The fonts are vendored so a changing remote font can't alter future builds.
Versions, hashes, sources, and licenses are recorded in
[Third-party notices](THIRD_PARTY_NOTICES.md).

## Project scope and safety

This firmware targets the stock reTerminal Sticky power, display, microphone,
and battery hardware. It doesn't replace the charger or cell-protection
circuitry.

The optional camera request sends a Home Assistant bearer token only to the
configured Home Assistant origin, rejects cross-origin absolute URLs, refuses
redirects, and verifies HTTPS certificates. The optional microphone is powered
only for a manually initiated Assist request, and turns off as soon as voice
activity ends and on every cancellation, error, disconnect, idle, and sleep
path. Review [Security](SECURITY.md) before changing either boundary.

## Contributing and license

Bug reports and focused improvements are welcome. Changes to power, sleep,
touch, camera, microphone, or refresh behavior should include physical-device
results; a successful compile can't prove charging behavior or e-paper image
quality.

Read [Contributing](CONTRIBUTING.md) before submitting a change and report
vulnerabilities through the private process in [Security](SECURITY.md).

Project code is licensed under [GPL-3.0-or-later](LICENSE). Bundled fonts retain
their upstream licenses; see [Third-party notices](THIRD_PARTY_NOTICES.md).
