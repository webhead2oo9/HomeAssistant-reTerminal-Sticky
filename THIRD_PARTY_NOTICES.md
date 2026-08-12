# Third-party notices

This file records third-party software and assets directly bundled by or
referenced from this source project. Those components remain under their own
licenses. The project-specific source is licensed separately under
[GPL-3.0-or-later](LICENSE).

The initial release is source-only. ESPHome/PlatformIO may download additional
frameworks and libraries during a local build; their package metadata and
license files remain authoritative for those transitive dependencies. A future
binary release would require a fresh audit of every linked component and all
corresponding source/offers and notices.

## ESPHome

- Project: ESPHome
- Upstream: <https://github.com/esphome/esphome>
- Version used for validation: 2026.7.4
- License: GNU General Public License, version 3
- License source: <https://github.com/esphome/esphome/blob/dev/LICENSE>

ESPHome generates and supplies the runtime incorporated into locally compiled
firmware. Distribution of a combined firmware image must satisfy the applicable
GPLv3 source and notice obligations. This repository distributes source only;
its project code is offered under GPL-3.0-or-later in [LICENSE](LICENSE).

## Material Design Icons Webfont

- Project: Material Design Icons Webfont
- Upstream: <https://github.com/Templarian/MaterialDesign-Webfont>
- Bundled version: 7.4.47
- Bundled file: `fonts/materialdesignicons-webfont.ttf`
- SHA-256: `61e8aba5a4e981fe22cf7c8e8bcdbea00476e75c62c37f01bf7ee33361d68428`
- License: Apache License 2.0
- Local license text: [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt)
- Versioned source: <https://github.com/Templarian/MaterialDesign-Webfont/tree/v7.4.47>

Only the glyphs listed by the ESPHome font configuration are compiled into a
device image, but the complete upstream webfont is present in the source tree.
Material Design Icons and its marks are the property of their respective
owners; the Apache-2.0 license covers the font software, not an endorsement.

## Inter

- Project: Inter
- Upstream: <https://github.com/rsms/inter>
- Bundled version: `4.001;git-66647c0bb`
- Bundled file: `fonts/Inter-Bold.ttf`
- SHA-256: `b37284b5701b6b168dfc770aa1a4ac492106422fd3ba76bc7641e37434e8019c`
- License: SIL Open Font License 1.1
- Local license text: [licenses/OFL-1.1.txt](licenses/OFL-1.1.txt)
- Google Fonts entry: <https://fonts.google.com/specimen/Inter>

The Bold face is vendored so a release build does not depend on the mutable
result of a Google Fonts request. ESPHome compiles only the configured glyphs
into the firmware. The font remains under the SIL Open Font License; retain its
copyright and OFL notices if it is updated or redistributed.

## Trademarks

reTerminal, Seeed Studio, ESPHome, Home Assistant, Material Design Icons, and
Inter are names or marks of their respective owners. Their mention identifies
compatibility or upstream components and does not imply sponsorship or
endorsement.
