# Troubleshooting

Start with the first section that matches what you're seeing. Keep the Sticky
on USB power while diagnosing it so deep sleep doesn't interrupt logs or OTA.

## Build or validation fails

Confirm that the pinned release is active:

```shell
python -m esphome version
```

It should report 2026.7.4. Then run the cheaper configuration check before a
full compile:

```shell
python -m esphome config reterminal-sticky.yaml
```

Common causes are:

- the local YAML was moved away from the repository root, breaking relative
  package, font, or header paths;
- a room package variable is missing or misspelled;
- two packages use the same `key`;
- room `slot` values are duplicated or not consecutive from zero;
- more than six total room/camera/Assist positions are enabled; or
- `secrets.yaml` is missing a key from `secrets.example.yaml`.

The `seeed-reterminal-sticky` display model requires ESPHome 2026.7.4 or newer.
This project pins 2026.7.4 because its e-paper helpers have been compiled and
tested against exactly that release.

The stock configuration produces ESPHome warnings for GPIO0, GPIO3, GPIO19,
GPIO20, GPIO45, and GPIO46 because several are ESP32-S3 strapping or
USB-Serial-JTAG pins. Those warnings are expected for this tested board
wiring, not permission to reuse the pins or suppress warnings after changing
the hardware configuration.

## No serial port appears

The Sticky exposes UART0 through a CH343P USB-to-UART bridge.

1. Use a known data-capable USB cable; plenty of charging cables have no data wires.
2. Try another direct USB port rather than an unpowered hub.
3. Check the operating system's device list for a CH343/USB serial device.
4. Install the appropriate host driver if the bridge is present but has no
   serial port.

Don't change the ESPHome UART or board definition to compensate for a missing
host driver.

## Home Assistant says the device has no `api` section

The setup dialog's “make sure the device's YAML configuration declares an
`api` section” message is generic. It usually just means Home Assistant
couldn't open an API connection at all; it doesn't prove the YAML is missing
`api:`.

The public configuration already contains the required encrypted API server:

```yaml
api:
  encryption:
    key: !secret api_encryption_key
```

Check the connection in this order:

1. Keep the Sticky on USB so it stays awake.
2. Read the serial log and find the current **IP Address** and `Noise
   encryption: YES` lines:

   ```shell
   python -m esphome logs reterminal-sticky.yaml
   ```

3. Retry **Settings → Devices & services → Add integration → ESPHome** using
   that exact IP address and port `6053`. A discovery card can be visible even
   when a cached, selected, or manually entered address has gone stale.
4. At the encryption prompt, enter only `api_encryption_key` from
   `secrets.yaml`. Do not use `ha_bearer`, `ota_password`, or
   `fallback_ap_password`.
5. From another machine on the same network, test the encrypted endpoint:

   ```shell
   python -m esphome logs reterminal-sticky.yaml --device DEVICE_IP
   ```

   A `Successful handshake` message proves the firmware's API block and
   encryption key work.
6. If that direct test succeeds but Home Assistant still can't add the device,
   inspect Home Assistant's system logs for the address it actually tried.
   Correct stale or mistyped addresses and confirm the Home Assistant host can
   reach the panel on TCP port `6053`; VLAN or firewall rules can block the
   connection even when mDNS discovery works.

Using the same valid encryption key on another ESPHome device doesn't by
itself prevent pairing, though unique per-device keys are strongly recommended.

## The device powers off or does not charge

Go back to the unmodified power configuration before any further testing. The
critical signals are:

- GPIO45: power hold, active high;
- GPIO46: power lock, active high;
- GPIO39: charger enable, active low;
- GPIO9: external-power detection; and
- GPIO38: microphone rail enable, active high.

A compile can't verify these electrical states, so test attended on stock
hardware.

## The menu stays unknown or a room does not appear

A wake from sleep paints the menu from the light states saved at the previous
sleep entry, then replaces them with live states. A cold boot or OTA restart
has no snapshot and gives Home Assistant up to 12 seconds before drawing the
first menu. If states remain unknown (`--`):

1. confirm the ESPHome device is connected in Home Assistant;
2. verify every `card_N_entity` exists and is a `light` entity;
3. verify `outdoor_temp_entity` has a numeric Fahrenheit state and the onboard
   **Temperature** entity has a plausible reading;
4. check room slots start at `0` with no gaps; and
5. inspect logs for messages tagged `room_model`.

An invalid room model disables room, camera, and Assist touch targets on
purpose, to keep a malformed layout from sending an action to the wrong
entity. The menu then shows a **Room setup error** panel naming the first
problem found, such as a gap in the slot numbers or more rooms than the layout
fits.

## Light states display, but taps do nothing

First listen for the tap feedback described in
[Operation](OPERATION.md#tap-feedback). No tick means the card is still locked
by an earlier tap, which clears within a few seconds. Two ticks about six
seconds after a tap, with **No response from Home Assistant** in the footer,
mean the action was sent but the light never changed.

This usually means the ESPHome integration can subscribe to states but isn't
allowed to call Home Assistant actions.

Open **Settings → Devices & services**, select the ESPHome integration entry,
open its options (the cogwheel or **Configure** control), and enable **Allow the
device to perform Home Assistant actions**. Then retry a single known light.

Also confirm the card points to a `light` entity. If several lamps should move
together, use a Home Assistant Light Group rather than putting multiple entity
IDs into one package variable.

## The display flashes, ghosts, or updates slowly

Full-screen black/white flashing is the e-paper cleaning waveform, and it's
normal after page changes and periodically between partial updates. Camera
decoding, the Home Assistant connection, and a full refresh can also make a
page transition take several seconds.

If part of a previous page hangs around after many operations, reproduce it
through several identical toggle and page-change cycles and record whether the
next full refresh clears it. A display operation stuck for more than 20 seconds
causes an intentional safe reboot and full-refresh request.

## The camera tile fails

Check these in order:

1. `ha_base_url` contains only scheme, host, and optional port, with no path or
   trailing slash.
2. The Sticky can resolve and reach that address from its Wi-Fi network.
3. `camera_entity` exists and Home Assistant can produce a still image for it.
4. `ha_bearer` begins with the literal `Bearer ` followed by a valid long-lived
   access token.
5. An HTTPS address has a certificate trusted by the device.
6. The requested snapshot URL is relative or has exactly the same scheme, host,
   and port as `ha_base_url`.

Redirects are deliberately disabled because forwarding the bearer header to a
different origin could expose the token. A rejected custom URL falls back to
the configured camera proxy and logs a warning tagged `camera`.

For missed-ring recap failures, verify the Home Assistant automation actually
wrote the file and that `/config/www/name.jpg` is reachable as
`<ha_base_url>/local/name.jpg`. Remember that `/local` files may be
accessible without authentication.

## Assist reports that Home Assistant is offline

A request made while the panel is still connecting shows **CONNECTING...** and
starts on its own once the link is up. This message means no connection
arrived within 10 seconds, or the link was already known to be down. Confirm
the panel is available in Home Assistant, and try again.

The device uses the server-side Assist pipeline. In **Settings → Voice
assistants**, test a pipeline with working speech-to-text, conversation, and
text-to-speech providers. If the panel captures speech but returns a service
error, inspect the Home Assistant pipeline trace before changing microphone
pins.

The on-screen diagnostic distinguishes several device-side failures:

- **Microphone produced no samples:** the I2S source delivered no chunks.
- **Microphone signal was silent:** chunks arrived but contained no meaningful
  activity.
- **Speech service unavailable:** capture reached the pipeline, but the remote
  STT stream failed.

The hardware-tested Sticky uses PDM clock GPIO19, data GPIO20, and microphone
rail GPIO38. Pin tables for the larger reTerminal E-series devices don't apply
to this 3.97-inch model.

Don't enable voice-assistant DEBUG logs in a report without reviewing them;
they can contain verbatim transcripts and responses.

## OTA cannot find the device

OTA only exists while the panel is awake with Wi-Fi enabled. The easiest fix
is to connect USB power, press a button or tap the screen, and immediately run:

```shell
python -m esphome run reterminal-sticky.yaml
```

For an unattended scheduled wake, use:

```shell
python tools/flash_on_wake.py reterminal-sticky.yaml --hostname reterminal-sticky.local
```

If mDNS is unreliable, add `--fallback-ip` with the device's current local IP.
The helper waits for the ESP32 native OTA port, 3232; it can't wake the panel
remotely.

## Time or wake schedule is wrong

The PCF8563 RTC is read at boot and written after Home Assistant time sync.
Leave the panel awake and connected to Home Assistant long enough for one time
sync, then check the retained clock again.

Scheduled wakes happen a few seconds after quarter-hour or hourly boundaries
by design. If the RTC has no valid time, the device temporarily falls back to a
15-minute schedule until Home Assistant supplies one.

## Collecting a useful report

Include:

- the Git commit and ESPHome version;
- whether the panel was on USB or battery;
- which wake path led to the problem;
- the smallest configuration change that reproduces it; and
- sanitized logs covering one complete attempt.

Remove Wi-Fi details, tokens, API keys, passwords, personal entity IDs, camera
frames, addresses, and spoken text. For security issues, use the private process
in [Security](../SECURITY.md), not a public issue.
