# Security policy

This is unofficial community firmware for a network-connected home-control
device. Treat every local build as sensitive: ESPHome resolves secrets into the
compiled firmware and leaves build artifacts on the development machine.

## Supported versions

Until the first tagged release, only the latest source on the maintained branch
receives security fixes. After a release is tagged, the latest source and most
recent tagged release will be supported. Older development snapshots are not
supported. No prebuilt firmware images are distributed.

Report suspected vulnerabilities through
[GitHub private vulnerability reporting](https://github.com/webhead2oo9/HomeAssistant-reTerminal-Sticky/security/advisories/new).
Do not open a public issue with exploit details or credentials. If private
reporting is unavailable, open a public issue containing no vulnerability
details and ask the maintainer to establish a private contact channel.

Include a concise description, affected revision, reproduction steps, impact,
and a proposed mitigation if known. Remove tokens, passwords, entity IDs,
camera images, network addresses, and identifying logs from the report.

## Secrets and generated artifacts

The following values must remain local and untracked:

- Wi-Fi credentials
- ESPHome native API encryption keys
- OTA passwords
- fallback access-point passwords
- Home Assistant bearer tokens
- private hostnames, IP addresses, and entity IDs
- `.esphome/` build output, logs, and downloaded snapshots

Use `secrets.example.yaml` only as a template. Keep real values in the ignored
`secrets.yaml`, and keep installation-specific YAML in the ignored local copy.
Before sharing a patch or archive, inspect both the working tree and Git history;
deleting a secret from the latest file doesn't remove it from earlier commits.
If a secret enters a commit or firmware artifact, rotate it rather than relying
on deletion alone.

## Camera request boundary

The camera component sends `ha_bearer` as an HTTP `Authorization` header. The
example permits the configured Home Assistant camera endpoint and relative
paths resolved against the same Home Assistant origin. Keep that origin check
in place.

Allowing a caller to supply an arbitrary absolute URL can disclose the bearer
token to an attacker-controlled server. Redirect behavior and URL parsing are
also security-sensitive; changes require tests that prove the final request
can't leave the configured scheme, host, and port. Never log the header or
token.

A long-lived Home Assistant token inherits its user's access. Prefer a
dedicated user with the least available access, store the token only in
`secrets.yaml`, and revoke it if a device or build artifact is lost. The
example verifies HTTPS certificates by default; don't disable that verification
on an untrusted network.

The optional missed-ring recap is a different boundary. Its example path is
served from Home Assistant's `/local` directory and may be readable without an
access token by anyone who can reach that URL. Do not save sensitive camera
frames there. Disable the recap and use only the authenticated camera proxy when
an unauthenticated saved image is unacceptable.

## Voice privacy

The optional Assist tile is manual, one-shot, and text-only. GPIO38 powers the
onboard microphone only after a tap and turns off as soon as Home
Assistant's silence detection ends the utterance. Cancellation, disconnect,
error, completion, idle, and sleep paths also force the rail off. No speaker is
configured, and the firmware does not persist raw audio, transcripts, or
responses.

Home Assistant and the selected Assist providers still receive household
speech. Review the privacy and retention behavior of the configured STT and
conversation providers. ESPHome's voice component can log verbatim transcripts
and responses at DEBUG level, so the public example pins that component to WARN.
Do not remove the override when collecting logs for a public report.

## Physical and power safety

This firmware targets the stock reTerminal Sticky hardware. Several GPIOs are
power-critical:

- GPIO39 controls the active-low charger enable.
- GPIO45 and GPIO46 hold the device power path.
- GPIO9 reports external power and participates in wake behavior.
- GPIO38 enables the active-high onboard microphone rail.

Changing their polarity, boot state, hold state, or wake configuration can
disable charging, prevent shutdown, or destabilize the device. Fuel-gauge range
checks improve telemetry quality but aren't a substitute for charger and cell
protection. Power changes require attended testing on the intended board and
stock-compatible battery hardware.

## Source-only distribution

Do not attach a generic firmware binary to a release. A compiled image can
contain credentials, tokens, private addresses, entity IDs, and recovery access
settings. Users must create a reviewed local configuration and build their own
image from the published source.
