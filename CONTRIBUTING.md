# Contributing

Thanks for helping make this firmware useful beyond one installation. You
don't need to understand the entire display scheduler to contribute:
corrections to documentation, reproducible bug reports, new test cases, and
focused code improvements are all valuable.

The project has one firm constraint: the public baseline must remain safe and
understandable on a stock reTerminal Sticky. Installation-specific choices
belong in local substitutions and packages rather than the shared example.

## Choose the right starting point

### Reporting a bug

Open an issue with:

- the Git commit and ESPHome version;
- whether the panel was on USB or battery;
- what you expected and what happened instead;
- exact steps that reproduce the problem;
- the relevant wake path, page, or feature; and
- sanitized logs from one complete attempt, if available.

Before posting, remove credentials, tokens, API keys, passwords, private entity
IDs, network addresses, personal names, camera frames, and spoken text. A GitHub
issue is public.

For a security vulnerability, do not open a normal issue. Follow the private
process in [SECURITY.md](SECURITY.md).

### Suggesting a feature

Describe the user problem before the proposed implementation. Include how the
feature should behave on battery, when Home Assistant is unavailable, and when
the six menu positions are already occupied.

Open an issue before investing in a large change to power management, display
scheduling, configuration structure, or the public extension model. It gives
maintainers and hardware owners a chance to agree on constraints before code
gets written.

### Fixing documentation

Small corrections can go straight to a pull request. Keep the README focused
on the shortest successful installation path and put detailed configuration,
operation, troubleshooting, or release material in `docs/`.

### Changing firmware behavior

Start with a focused issue or pull request that changes one behavior. Explain
which parts can be validated in software and which were exercised on a physical
Sticky. If you don't have the hardware, say so plainly; another contributor
can help with the device test.

## Ground rules

- Search existing issues and pull requests before opening another.
- Keep each change focused. Unrelated cleanup makes hardware regressions harder
  to review.
- Never commit `secrets.yaml`, a local `reterminal-sticky.yaml`, `.esphome/`,
  logs, snapshots, firmware binaries, or identifying household data.
- Don't add a generic prebuilt firmware image. ESPHome builds can contain
  credentials, entity IDs, addresses, and recovery settings.
- Preserve the camera same-origin boundary and manual, one-shot microphone
  behavior.
- Don't claim physical validation you didn't perform.
- Add only code and assets whose source and license can be documented.

Contributions are submitted under the project's
[GPL-3.0-or-later license](LICENSE). Bundled third-party assets retain their own
licenses as recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Development setup

Use the tested ESPHome version so your results are comparable with CI and
other contributors.

```shell
python -m venv .venv
```

Activate the environment with `. .venv/bin/activate` on macOS/Linux or
`.\.venv\Scripts\Activate.ps1` in PowerShell, then install ESPHome:

```shell
python -m pip install "esphome==2026.7.4"
```

Create ignored local files from the examples:

```shell
cp reterminal-sticky.example.yaml reterminal-sticky.yaml
cp secrets.example.yaml secrets.yaml
```

PowerShell users can use `Copy-Item` instead of `cp`. Fill the local files
with test-only values, and check `git status --short` before every commit to
make sure they stay untracked.

The native behavior tests require a C++17 compiler named `c++`, `g++`, or
`clang++` on `PATH`.

## What to run

Run the checks appropriate to the change. When in doubt, run the whole suite.

| Change | Minimum validation |
| --- | --- |
| Documentation only | `python tools/check_docs.py` and `git diff --check` |
| Python helper or native logic | Python syntax, unit tests, documentation links, and `git diff --check` |
| Room package, menu, or touch routing | Unit tests, all room topologies, ESPHome configuration, and full compile |
| Display, camera, Assist, sensor, sleep, or power behavior | Complete software suite plus the relevant physical-device checks |
| ESPHome version or dependency | Clean full compile and targeted hardware regression testing |

The complete software suite is:

```shell
python -m compileall -q tools tests
python -m unittest discover -s tests
python tools/check_docs.py
python tools/validate_room_topologies.py
python -m esphome config reterminal-sticky.yaml
python -m esphome compile reterminal-sticky.yaml
git diff --check
```

The topology validator covers one room, mixed card counts, four rooms with both
special tiles, both five-room/single-special combinations, and six rooms with
no special tiles.

A clean compile proves ESPHome accepted the configuration and C++ code. It
doesn't prove charger state, GPIO levels, microphone capture, wake sources,
touch alignment, or e-paper image quality.

## Configuration and architecture guardrails

The example is designed to be customized without copying its core UI logic:

- Keep public YAML generic. Installation-specific values belong in
  substitutions, secrets, or an ignored local configuration.
- Add rooms through the one-, two-, or three-card packages. Don't copy the
  shared room page or touch handlers.
- Keep room slots consecutive from zero and package keys unique. Keys must
  match `[a-z][a-z0-9_]*`.
- Do not exceed six combined room, camera, and Assist menu positions.
- Keep one Home Assistant light entity per card. Use a Home Assistant Light
  Group when several lamps should act as one.
- Treat rooms as build-time configuration, not runtime discovery.
- Preserve comments around power-critical GPIOs and asynchronous display code.

Display updates are asynchronous: `display.update()` starts work and returns,
and it doesn't mean the physical panel is idle. The shared queue must keep
serializing refreshes and own the dirty region.

- Invalidate the smallest safe rectangle for a small same-page change.
- Request a full cleaning waveform for page changes or large black/white
  inversions.
- Don't introduce a second path that calls the driver outside the queue.
- Test repeated transitions; one clean frame won't reveal accumulated
  ghosting.

Deep sleep turns off the CPU and radio. Don't add periodic work that
supposedly runs during sleep, or claim that Home Assistant can wake Wi-Fi. Keep
the power holds, charger enable, wake pins, and external-power detection
internally consistent.

Voice and camera changes must preserve their privacy boundaries:

- Keep voice capture manually initiated and one-shot.
- Keep GPIO38 off outside capture and on every error, cancel, disconnect, idle,
  and sleep path.
- Never log or persist raw audio, transcripts, or responses.
- Keep camera requests on the configured Home Assistant origin whenever the
  bearer header is attached.
- Don't follow camera redirects unless a tested design proves the final request
  can't leave that origin.

## Hardware validation

Use attended testing for every power change. For other runtime changes, select
the relevant checks and report exactly what was exercised:

- cold boot and first serial installation;
- API connection and first-frame Home Assistant state coalescing;
- touchscreen and all three physical buttons;
- first and last rooms, every card count used, toggle, brightness, offline
  rendering, and a long wrapped room name;
- regional partial refresh, full refresh, and page change;
- repeated wake cycles without stale clipping or gray tile remnants;
- display refresh-timeout recovery;
- battery telemetry, external-power state, and charging indication;
- interactive idle, deep sleep, scheduled wake, button wake, and USB wake;
- OTA while the device is intentionally awake;
- camera success, failure, dismissal, missed-ring recap, and same-origin URL
  rejection; and
- Assist start, speech capture, VAD mic-rail shutdown, result, cancellation,
  disconnect, and error paths.

Include the ESPHome version, commit, board revision if known, power state, and
observed result. Don't attach private logs without sanitizing them.

## Documentation style

Write for someone holding an unmodified Sticky and seeing the repository for
the first time.

- Explain observable behavior before implementation details.
- Keep commands runnable from the repository root.
- State whether a setting affects compilation, menu visibility, or runtime
  behavior; those are different in ESPHome.
- Link to primary Seeed Studio, ESPHome, Home Assistant, or dependency
  documentation for requirements that may change.
- Keep hardware claims scoped to the board and test actually performed.
- Update the README when behavior, setup, the supported ESPHome version, or a
  security assumption changes.
- Run `python tools/check_docs.py` after renaming a file or heading.

## Dependencies and assets

Prefer dependencies already provided by ESPHome. If a new font, icon set,
image, or library is necessary, record its exact upstream source, version,
copyright, and license in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
include any required license text. Being downloadable on the internet isn't,
by itself, permission to redistribute an asset.

If the tested ESPHome version changes, perform a clean compile and repeat the
hardware checks most likely to be affected, especially the e-paper scheduler,
deep sleep, I2C sensors, microphone, and runtime image handling.

## Pull request checklist

Before requesting review:

- [ ] The change addresses one clear problem.
- [ ] Public files contain no installation-specific or identifying data.
- [ ] New behavior has an automated test where practical.
- [ ] The appropriate software checks pass.
- [ ] Hardware testing is described accurately, including anything not tested.
- [ ] User-visible behavior changes are documented where needed.
- [ ] New dependencies or assets have complete notices and compatible licenses.
- [ ] The full diff is intentional and `git diff --check` is clean.

Use an imperative commit subject that describes the result, then explain the
reason and verification in the pull request. Generated build files and
formatting-only churn shouldn't share a commit with functional changes.
