# Hardware recovery and memory budget

Updated 2026-10-16. The user reports that the ESP32 is blank and previously stopped
at calibration. This session has **not** observed or reflashed the physical panel.
The installed toolchain/USB access request failed automatic approval review even
after the user approved it. The failure was in the review service, not a finding
that the requested access was unsafe. Do not interpret host tests as a repaired HUD.

## Environment and target

| Item | Evidence and status |
|---|---|
| Board | Historical probe: ESP32-2432S028R, ESP32 revision 3, 4 MB flash, no PSRAM; see `HARDWARE_REPORT.md`. Current physical attachment not reverified. |
| Display | 320×240 landscape, TFT_eSPI `ILI9341_2_DRIVER`; controller read ID was inconclusive. GPIO SCK 14/MOSI 13/MISO 12/CS 15/DC 2/BL 21. |
| Touch | XPT2046, separate SPI SCK 25/MOSI 32/MISO 39/CS 33; no IRQ. Physical calibration must be measured on this panel. |
| Last known USB targets | COM4 and OnePlus CPH2585 (`acea07d9`), from historical setup output. Enumerate again before use. |
| Firmware build | PlatformIO espressif32 7.1.3; documented installed Arduino core 2.0.17, **not 3.x**. Bluedroid is the implemented BLE stack. Dependency compatibility still needs a new target build. |
| UI libraries | TFT_eSPI 2.5.43, XPT2046 v1.4, LVGL 9.6.0, Nanopb 0.4.92. No dependency upgrade in this repair. |
| Android | Gradle 8.14.6, AGP 8.13.2, Kotlin 2.3.21, SDK/target 36, min 26. Source configuration does not prove API 26 compatibility. |
| Accessible host tools | Current sandbox: Windows 11, bundled Python 3.12.14, JDK 17.0.20.1, MinGW g++6.3.0. Installed Python 3.11/PlatformIO and Android SDK cannot be used from this session. |
| Current environment gate | FAIL/BLOCKED, see `evidence/20261016/current-hardware/current-sandbox-setup.log`. Missing access is not proof that those tools are absent from the PC. |

## Memory decisions

The heap, largest contiguous block, stacks and flash partition are separate budgets.
The board has no external RAM. A 320×240 RGB565 framebuffer would cost 153,600 bytes;
the firmware uses two partial strips instead. Phone-side routing/map work remains
on Android. Wi-Fi/raster transport is not implemented or enabled.

| Allocation | Size / treatment |
|---|---|
| Draw buffers | 2 × 320 × 10 × 2 = **12,800 bytes**, static; retained. |
| Reassembly | Two bounded lanes, **8,288 bytes in the historical ELF**; retained. |
| UI event queue | Replaced repeated full state payloads with notifications and small critical events; state is read from locked snapshots. Fresh host layout measurements are in the firmware evidence. |
| Command and BLE RX queues | Keep bounded capacity for distinct user actions and received fragments. No silent command coalescing to save memory. |
| Screen objects | Only the current content screen is allocated. Leaving a screen destroys its LVGL subtree; returning reconstructs it from shared state. Status/tab bars remain. |
| Task stacks | UI 12,288 + protocol 8,192 + touch 4,096 = **24,576 bytes**, plus RTOS overhead. No unmeasured stack shrinking. |
| LVGL allocator | Retains existing allocator; no guessed private pool that could merely move the failure. |
| No PSRAM | Removed `BOARD_HAS_PSRAM=0`: code that tests whether the macro is defined may enable initialization even when its value is zero. |

Historical log `evidence/20261016/TC-HW-01/fw_optimized_stats.log` records free heap
27,884 bytes, minimum 26,364 and largest block 9,204. This fails the required
**minimum 40,960 / largest 20,480 bytes**. The new allocation scheme is intended to
recover headroom; it has not yet met those limits in a fresh device measurement.

The existing firmware ELF has **68,044 bytes of static DRAM**. Its binary is
1,732,160 bytes in the 3,145,728-byte factory partition: **44.94% flash headroom**.
These figures belong to the old artifact, not the new sources. Hashes, section
sizes and largest static objects are in
`evidence/20261016/current-hardware/existing-artifact-memory.json`.

## Calibration and blank-screen repair

The live touch HAL called the XPT2046 library before configuring the custom SPI
pins. The library calls `SPI.begin()` itself, which can leave ESP32 SPI on default
pins and make later initialization ineffective. The custom bus now starts first.
Short presses no longer leak samples into the next target. Unknown calibration
does not silently become a trusted factory mapping, and NVS save failure does not
unlock controls. The wizard requires actual target presses; a software click is
not calibration evidence. Startup failures are checked and explicitly reported.

Do not erase NVS to investigate the screen: that also removes calibration and
bonds. A normal program upload preserves NVS. Check the fresh serial log before
changing display drivers or wiring.

## Build and verify on the host with device access

The following steps are still outstanding for the repaired sources. Use the
Python installation with PlatformIO/pyserial and the configured JDK/Android SDK.
The last-known port is only an example; select the currently enumerated device.

```powershell
python tools/setup_check.py
python tools/test-only/serial_harness/capture.py --list
python -m platformio test -d firmware -e native
python -m platformio run -d firmware -e cyd_ili9341
python tools/memory_report.py firmware/.pio/build/cyd_ili9341/firmware.elf --binary firmware/.pio/build/cyd_ili9341/firmware.bin
python -m platformio run -d firmware -e cyd_ili9341 -t upload --upload-port COM4
python tools/run_hardware_checks.py --port COM4 --seconds 60
```

The last command resets the HUD and captures boot, fault, heap and transfer
counters plus basic ADB metadata in a new evidence directory. Supply `--adb` with
the installed executable path if it is not on PATH. It never installs, erases,
pairs, or grants permissions. Exit 2 means incomplete verification; exit 1 means an
observed failure. It deliberately cannot declare complete E2E PASS.

Confirm on glass that text/colors are correct, press each of the five crosshairs,
check reported calibration residuals, and reboot to verify persistence. Switch
every available screen repeatedly while recording heap. Require stable minimum
heap and a largest block above the thresholds before adding traffic. A static
screen's zero full-screen-equivalent FPS does not mean the display is blank.

Android build/install, from the `android` directory:

```powershell
.\gradlew.bat testDebugUnitTest :app:assembleRelease
adb install -r app/build/outputs/apk/release/app-release.apk
```

Then run real pairing, encrypted GATT/HELLO, ten reconnect cycles, observed media
commands, routing/GPS and the eight-hour soak only as their implementations and
configuration become available. Do not use the old APK/firmware files to claim
the new source was tested. A user-selected routing endpoint and physical touch,
road/readability and media-app observations cannot be supplied by a host simulator.
