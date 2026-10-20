# Licences and attribution

Every dependency, font, icon and data provider, with its licence and what the product
must do to comply. §7.4 requires an in-app "Open-source licences & attribution" screen
built from this list, and the map views must carry the required attribution.

**Status:** the table below records the licence each project publishes. A licence text
must be captured **at the pinned commit/tag** before release (§7); that capture has not
been done yet for any entry, so the "text captured" column is `NOT DONE` throughout.

---

## 1. Firmware

| Component | Version | Licence | Obligation |
|---|---|---|---|
| TFT_eSPI | 2.5.43 | FreeBSD (per project) | include copyright notice in binary distributions |
| XPT2046_Touchscreen | v1.4 | MIT | include notice |
| LVGL | 9.6.0 | MIT | include notice; LVGL logo/attribution rules apply to the licence text only |
| NimBLE-Arduino | 2.5.1 | Apache-2.0 | include notice; state changes if modified |
| nanopb | 0.4.92 | zlib (per project) | include notice |
| ESP32 Arduino core | 2.0.17 (platformio 7.1.3) | LGPL-2.1 (core), Apache-2.0 (IDF) | LGPL: dynamic linking/relinkability expectations; note in the release report |
| Espressif IoT Development Framework | bundled with the core | Apache-2.0 | include notice |

## 2. Android

| Component | Version | Licence | Obligation |
|---|---|---|---|
| AndroidX (core, activity, lifecycle, navigation, datastore, room, work, test) | see [DEPENDENCIES.md](DEPENDENCIES.md) | Apache-2.0 | include notice |
| Jetpack Compose (BOM 2026.06.01) |, | Apache-2.0 | include notice |
| Kotlin standard library | 2.3.21 | Apache-2.0 | include notice |
| protobuf-javalite | 4.36.2 | BSD-3-Clause | include notice |
| MapLibre Native Android | 13.7.1 | BSD-2-Clause | include notice; **not** a map data licence |
| MockK, Robolectric, kotlinx-coroutines-test | see [DEPENDENCIES.md](DEPENDENCIES.md) | Apache-2.0 / MIT | test-only, not shipped in the APK |

## 3. Data and services (the user's choice, not bundled)

| Provider | Licence / terms | What the product must do |
|---|---|---|
| OpenStreetMap data | ODbL 1.0 | "© OpenStreetMap contributors" attribution wherever OSM-derived data appears |
| OSM tile server (development default) | OSMF tile usage policy | visible attribution, correct User-Agent, respect HTTP caching, **no** bulk or app-distributed use; public distribution requires a commercial/self-hosted tile source. The app warns in Settings and the README states this. |
| OSRM | BSD-2-Clause (software); the endpoint is user-supplied | attribution for the routing data; the endpoint operator's terms apply |
| Nominatim | GPL-2.0 (software); public instance has a usage policy | ≤ 1 request/s, descriptive User-Agent, caching, submit-only search (no autocomplete) |
| Any alternative tile style or routing host the user configures | unknown | the app cannot verify a third party's terms; it reports what is configured and links to this file |

Attribution is a **release blocker for M6/M10** and is checked by TC-UI-08 (attribution
visible on every map view).

## 4. Fonts and artwork

| Item | Source | Licence | Note |
|---|---|---|---|
| UI font | Inter (planned, OFL) | SIL Open Font License 1.1 | subset to Latin-1 + Latin Extended-A (DEC-21) and converted with `lv_font_conv`; the OFL requires the licence to travel with the font |
| Icons / logo | original artwork (§14) | the project's own | no third-party marks; the logo is generated from `branding/dashcue_logo.svg` |
| Manoeuvre icons | to be authored as original SVGs in `assets/icons/` | the project's own | if any icon is taken from an icon set instead, its licence must be added here first |

The font choice is not final until the glyph subset is measured against the flash
budget, and a font swap changes the OFL obligations, which is why this table lists the
planned font rather than pretending it is settled.

## 5. Verifying this list

```bash
python tools/evidence_report.py     # collects versions and cross-checks this file
```

At release time, confirm that:

* every entry has its licence text captured at the pinned revision;
* no licence in this file is `unknown`;
* the in-app licences screen lists the same set as the "shipped" rows above;
* the release report states the LGPL relinking position for the Arduino core.
