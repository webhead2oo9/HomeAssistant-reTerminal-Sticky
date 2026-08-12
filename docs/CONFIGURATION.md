# Configuration guide

Start by copying `reterminal-sticky.example.yaml` to
`reterminal-sticky.yaml`. All installation-specific changes go in that local
file and in `secrets.yaml`; both are ignored by Git.

The example is deliberately complete: it includes one room of each supported
size and enables both special tiles. Replace those examples rather than adding
your own rooms beneath them.

## Substitutions

All the ordinary installation values live under `substitutions:` at the top
of the YAML.

| Name | Purpose | Important details |
| --- | --- | --- |
| `device_name` | ESPHome node name and hostname | Must be unique on the network. Lowercase letters, numbers, and hyphens are the safest choice. Changing it also changes the generated Home Assistant action name. |
| `friendly_name` | Human-readable Home Assistant device name | May contain spaces and punctuation. |
| `home_label` | Title shown on the menu | Keep it short enough for the header. |
| `camera_label` | Camera menu-tile label | Used only when the camera tile is visible. |
| `camera_title` | Title above a live camera image | Uppercase is easiest to read on the panel. |
| `assistant_label` | Assist menu-tile label | Used only when the Assist tile is visible. |
| `missed_ring_title` | Title above a saved doorbell frame | Used by the optional missed-ring recap. |
| `missed_ring_snapshot_path` | URL path to the saved recap image | Must be relative to `ha_base_url`, normally a `/local/...jpg` path. |
| `camera_menu_enabled` | Reserves a menu position for the camera | Use the quoted string `"true"` or `"false"`. Hiding the tile does not remove the compiled camera component. |
| `voice_assistant_enabled` | Reserves a menu position for Assist | Use the quoted string `"true"` or `"false"`. Hiding the tile does not remove the compiled voice component. |
| `outdoor_temp_entity` | Outdoor temperature shown in the header and clock | Its numeric state must already be in degrees Fahrenheit. |
| `camera_entity` | Camera used by the camera tile | Use the full Home Assistant entity ID, such as `camera.front_door`. |
| `doorbell_automation_entity` | Automation whose `last_triggered` marks a ring | Use the automation that saves `missed_ring_snapshot_path`. |
| `ha_base_url` | Home Assistant origin reachable from the Sticky | Include scheme, host, and optional port only, with no path or trailing slash; for example, `https://ha.example.net` or `http://homeassistant.local:8123`. |

The onboard SHT40 supplies the panel's indoor temperature and humidity device
entities. Its Celsius reading is converted to Fahrenheit in the sensor filter
before the header, clock, cache, or Home Assistant ever sees it. USB power,
charging, and heat inside the enclosure can push this local reading above a
room thermostat's, so let the panel settle before judging calibration.

If a hidden optional feature still points at a placeholder entity, the firmware
will compile, but Home Assistant may report that entity as unavailable. Replace
the entity IDs for every feature you actually intend to use.

## Secrets

`secrets.yaml` must contain every key listed in `secrets.example.yaml`:
`wifi_ssid`, `wifi_password`, `api_encryption_key`, `ota_password`,
`fallback_ap_password`, and `ha_bearer`. Copy that file so the exact names and
safe placeholders remain intact, then replace only the values you need.

Generate the API key with:

```shell
python -c "import base64,secrets; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

Use a different `api_encryption_key` for each panel. This is the native ESPHome
API key Home Assistant asks for during pairing, not the OTA password, the
fallback access-point password, or the `ha_bearer` camera token.

Create the camera token in the **Long-lived access tokens** section of your Home
Assistant user profile. Home Assistant only shows the token once, so copy it
right away. Preserve the literal `Bearer ` prefix when putting it in
`secrets.yaml`. See Home Assistant's
[authentication documentation](https://developers.home-assistant.io/docs/auth_api/#long-lived-access-token)
for the current token workflow.

The token is only needed for camera downloads, but the `ha_bearer` key must
still exist because the camera component is present in the example. If you hide
the camera tile and never call its action, the harmless placeholder from
`secrets.example.yaml` can stay.

## Rooms and menu capacity

The menu always has six physical positions. Camera and Assist only consume a
position when their corresponding substitution is `"true"`.

| Camera | Assist | Maximum rooms |
| --- | --- | ---: |
| off | off | 6 |
| on | off | 5 |
| off | on | 5 |
| on | on | 4 |

At least one room is required. Room slots must be consecutive and zero-based:
`0`, `1`, `2`, and so on. A gap or duplicate slot invalidates the whole room
model, and the firmware would rather suppress room and special-tile actions
than route a touch to the wrong target.

Choose a package based on the number of cards in the room:

- `packages/room-1-card.yaml`
- `packages/room-2-cards.yaml`
- `packages/room-3-cards.yaml`

Each package include also needs a unique YAML mapping name and `key`. The key
builds ESPHome component IDs and must match `[a-z][a-z0-9_]*`.

```yaml
packages:
  living_room: !include
    file: packages/room-2-cards.yaml
    vars:
      slot: "0"
      key: living_room
      label: "Living Room"
      icon: "F04B9"
      card_1_label: "Ceiling"
      card_1_entity: light.living_room_ceiling
      card_1_icon_on: "F0769"
      card_1_icon_off: "F17C7"
      card_2_label: "Floor Lamp"
      card_2_entity: light.living_room_floor_lamp
      card_2_icon_on: "F08DD"
      card_2_icon_off: "F17C8"
```

Room names wrap inside menu tiles, but short labels are still easier to read at
e-paper size. One- and two-card rooms are centered automatically on the shared
detail page.

Every card expects one Home Assistant `light` entity. The card toggles that
entity and cycles its brightness through 25%, 50%, and 100%. For several lamps,
create a Home Assistant Light Group and use the group entity. A non-dimmable
light can usually still be toggled, but it won't provide useful brightness
state.

## Icons

Package icons are hexadecimal Material Design Icons code points without the
`0x` or `U+` prefix. `F04B9`, for example, is encoded in the font list as
`\U000F04B9`.

The firmware only includes glyphs named under `icons_lg` in the YAML. When you
pick a code point the example doesn't already use:

1. set the package variable to the short form, such as `"F1234"`; and
2. add the corresponding eight-digit `"\U000F1234"` value to the `icons_lg`
   `glyphs:` list.

Miss the second step and ESPHome can't render the icon, even though the room
package itself is valid.

## Pair with Home Assistant

Keep the Sticky connected to USB for first-time pairing; USB keeps deep sleep
from interrupting discovery or diagnostics.

### Automatic discovery

1. Open **Settings → Devices & services** in Home Assistant.
2. Under **Discovered**, locate the card whose integration is **ESPHome** and
   whose name matches `friendly_name`.
3. Select **Add**, confirm the device in the discovery dialog, and select
   **Submit**.
4. Paste only the value of `api_encryption_key` from `secrets.yaml` into the
   encryption-key prompt.
5. Finish setup and optionally assign the panel to an area.

Don't paste `ha_bearer` into the encryption dialog; that bearer token is only
used by the panel's camera HTTP request. Home Assistant should never ask for
the Wi-Fi password, OTA password, or fallback access-point password while
adding the ESPHome integration.

### Manual setup

If no discovery card appears, select **Add integration**, choose **ESPHome**,
and enter the panel's current hostname or IP address. Use port `6053` if Home
Assistant asks for one. The default hostname comes from `device_name`, for
example `reterminal-sticky.local`.

The current IP address appears in serial logs after Wi-Fi connects:

```shell
python -m esphome logs reterminal-sticky.yaml
```

Successful pairing opens a Home Assistant device page. With the stock example
it has a Buzzer control; battery, charging, button, temperature, humidity, and
external-power entities; and an Assist satellite when Assist is enabled. Room
light states are subscriptions rather than separate device-page entities, so
verify those on the panel itself.

This discovery and encrypted pairing flow has been exercised end to end on a
stock reTerminal Sticky with ESPHome 2026.7.4 and Home Assistant. See
[Troubleshooting](TROUBLESHOOTING.md#home-assistant-says-the-device-has-no-api-section)
if the setup dialog reports that the YAML has no `api` section.

### Allow panel actions

After pairing, open the ESPHome integration entry and its options (the cogwheel
or **Configure** control), then enable **Allow the device to perform Home
Assistant actions**.

The panel uses that permission for `light.turn_on`, `light.turn_off`, and
brightness calls. State display works without it, which makes this easy to
miss: the tiles can show correct states while every tap is denied.
The option and its trust implications are described in the official [ESPHome
integration documentation](https://www.home-assistant.io/integrations/esphome/#options).

## Camera setup

The camera tile requests:

```text
<ha_base_url>/api/camera_proxy/<camera_entity>
```

`ha_base_url` is always the Home Assistant origin; the Sticky never contacts
Frigate directly. If `camera_entity` is provided by the Frigate integration,
the effective path is **Sticky → Home Assistant camera proxy → Frigate →
camera**. Camera selection and authentication stay at the Home Assistant
boundary regardless of the camera's upstream provider.

The request includes `ha_bearer`, so prefer a dedicated Home Assistant user
with the least access available for the installation. Use correctly validated
HTTPS when the request crosses an untrusted network; plain HTTP is only
appropriate on a network you already trust.

The configured Home Assistant origin is a security boundary. The custom camera
action accepts an empty URL, a relative URL, or an absolute URL with exactly the
same scheme, host, and port. Cross-origin URLs are rejected, redirects aren't
followed, and the configured camera proxy is the fallback.

### Show a snapshot from a Home Assistant automation

The ESPHome API action name is derived from `device_name`. With the default
`reterminal-sticky`, it is:

```yaml
actions:
  - action: esphome.reterminal_sticky_show_camera
    data:
      snapshot_url: "/api/camera_proxy/camera.front_door"
```

An empty `snapshot_url` uses `camera_entity`. The action can show a different
same-origin camera or saved image, but it can't make a sleeping panel connect,
so use it only when the device is awake. ESPHome documents the generated action
naming under [User-defined actions](https://esphome.io/components/api/#user-defined-actions).

### Optional missed-ring recap

The firmware can show the last saved doorbell frame on a later Home Assistant
sync wake. It watches the `last_triggered` attribute of
`doorbell_automation_entity`, so that entity must be the automation that writes
the image named by `missed_ring_snapshot_path`.

For Home Assistant OS, `/config/www/reterminal_sticky_last_ring.jpg` is normally
served as `/local/reterminal_sticky_last_ring.jpg`. A minimal automation is:

```yaml
alias: Save front door snapshot for Sticky
triggers:
  - trigger: state
    entity_id: binary_sensor.front_door_ding
    to: "on"
actions:
  - action: camera.snapshot
    target:
      entity_id: camera.front_door
    data:
      filename: /config/www/reterminal_sticky_last_ring.jpg
mode: restart
```

The upstream [Camera documentation](https://www.home-assistant.io/integrations/camera/#action-snapshot)
lists the `camera.snapshot` action. If Home Assistant rejects the destination,
follow its error message to allow the intended directory for your installation.

After Home Assistant creates the automation, copy its entity ID into
`doorbell_automation_entity`, and keep the default `/local/...` substitution in
sync with the filename.

Files under Home Assistant's `www` directory are served through `/local` and
shouldn't be treated as private: anyone who can reach that URL may be able to
view the saved frame without the bearer token. If that's unacceptable, don't
configure the missed-ring recap; use only the authenticated camera proxy.

## Assist setup

The Assist tile depends on Home Assistant's server-side pipeline:

1. Open **Settings → Voice assistants** in Home Assistant.
2. Configure a pipeline with speech-to-text, a conversation agent, and
   text-to-speech. The Sticky doesn't play the generated audio, but it does use
   the response text delivered at that stage.
3. Test the pipeline in Home Assistant before testing the panel.
4. Flash the configuration with `voice_assistant_enabled: "true"` and wait for
   the ESPHome API connection.

For pipeline setup and component behavior, see Home Assistant's [Assist
documentation](https://www.home-assistant.io/voice_control/) and ESPHome's
[Voice Assistant component](https://esphome.io/components/voice_assistant/).

Tapping the tile first completes the e-paper Listening frame, then enables the
microphone rail and starts a single request. Home Assistant handles silence
detection and processing. The rail turns off when voice activity ends, while
the transcript and response carry on through the remote pipeline.

There's no wake word, no continuous listening, no speaker, and no local
transcript storage. **Cancel Request** stops the entire pipeline, not just
capture.

The hardware-tested audio configuration is:

- PDM clock: GPIO19
- PDM data: GPIO20
- active-high microphone rail enable: GPIO38
- left channel, 16 kHz, with automatic gain and noise suppression

Don't change these pins based on documentation for other reTerminal models;
the larger reTerminal E-series products use different hardware.

## What is safe to customize

Labels, entity IDs, room packages, menu features, and icon glyphs are all
expected customizations. Refresh timing and power behavior can be changed too,
but they need physical testing.

Treat these areas as hardware-sensitive:

- GPIO39 charger enable polarity;
- GPIO45/GPIO46 power hold and lock;
- GPIO9 external-power detection and wake behavior;
- GPIO38 microphone rail control;
- display pins and the `seeed-reterminal-sticky` model preset; and
- the asynchronous refresh queue and deep-sleep rail holds.

Read [Security](../SECURITY.md) and [Operation](OPERATION.md) before modifying
those sections.
