# Touch fault runbook: the position axis is dead

Written **2026-10-21** from measurements on the single available board. It records
how the touch function broke, how that was separated from three firmware faults
that were hiding it, and what the physical repair is. Nothing here is inferred
from a datasheet; every number is a reading taken on this unit.

## 1. What broke, in one line

The XPT2046 still detects **contact** and still talks SPI, but the resistive
layers no longer divide: every press reports the same raw corner `(0, 1330)`
instead of a position. No calibration curve can exist for a panel that returns one
point for every touch.

## 2. Evidence

### 2.1 Position sensing worked on 2026-10-16

`docs/HARDWARE_REPORT.md` §4 records, from the probe run that closed GATE-02:

| Measurement (2026-10-16) | Value |
|---|---|
| Raw range during one touch | x `[2..2319]`, y `[1347..3620]` |

So the panel was a working 4-wire resistive touch screen five days before it
failed. The failure is a regression in the panel or its flex, not a
mis-configuration that was always there.

### 2.2 Every tap now returns the same point (2026-10-21)

Five wizard targets about 260 screen px apart, from
`docs/evidence/20261021-reflash/cal-live.log`:

```
CAL target=1 raw=(0,1327) screen=(30,30)   samples=12
CAL target=2 raw=(1,1332) screen=(290,30)  samples=9
CAL target=3 raw=(0,1332) screen=(160,120) samples=4
CAL target=4 raw=(0,1336) screen=(30,210)  samples=9
CAL target=5 raw=(0,1337) screen=(290,210) samples=16
CAL result=FAIL mean_px=60.57 max_px=141.95
```

Spread across the whole panel: **1 count in X, 10 counts in Y**. A working panel
spreads thousands of counts over that distance.

Repeated on the operator's own taps with the position-dead detector in place
(`docs/evidence/20261021-reflash/cal-live2.log`):

```
TOUCH position dead: 3 onsets, raw_x=0..0   raw_y=1317..1333
TOUCH position dead: 9 onsets, raw_x=0..34  raw_y=1317..1400
```

Nine presses spread over the panel never left a 34-count box in X.

### 2.3 Contact detection is still alive

`TOUCH channels` printed at boot (differential mode, 12-bit units):

```
idle    x=3771 y=2889 z1=2 z2=0 temp=340 vbat=0 aux=72   pen_irq=1
pressed x=14   y=1434 z1=0 z2=1 temp=446 vbat=0 aux=68   pen_irq=0
```

- **PENIRQ (GPIO36) follows the glass**: `1` at rest, `0` while a finger is down.
  That is the panel's own comparator, independent of SPI and of the ADC.
- **TEMP and AUX respond** (`298..446`, `68..86`), so the SPI link, the ADC and
  the controller's internal channels are healthy.
- **`z1` and `z2` are both ~0 on every read.** A healthy untouched panel reads
  `z1 ≈ 0, z2 ≈ 4095`. With both at 0 the driver's
  `z = z1 + 4095 − z2` saturates at ~4095, which is why `touched()` claimed a
  continuous press with no finger anywhere near the glass.

## 3. Cause

| Layer | Verdict | Proof |
|---|---|---|
| Firmware — display flush / polarity | **was broken, now fixed** | red probe rendered cyan with `INVOFF`, red with `INVON`; flush buffers held `0x0021` while the glass stayed white |
| Firmware — touch gate | **was broken, now fixed** | contact now comes from PENIRQ instead of the dead pressure channel |
| MCU pins (CS/DC/SCLK/MOSI, and the touch CSPI set) | **exonerated** | each line driven high then low reads back correctly; the controller answers on `sck=25 miso=39 mosi=32 cs=33` |
| NVS / calibration blob | **exonerated** | full `erase_flash` + reflash reproduced the fault from zero state |
| Touch panel / flex analog path | **FAULT** | position dead while contact and SPI work; `z1=z2=0` |

The physical signature — position pinned to one corner while PENIRQ still trips —
is consistent with one of the touch overlay's four analog connections (the drive
end of a plate) being open on the panel tail or in the flex connector. The same
flex assembly carries the display, and the display is fine, so this is localised
to the touch overlay pins.

**What is not the cause:** nothing in this repository's history. No commit changed
the touch pin map, the XPT2046 commands or the sampling code between the working
measurement on 2026-10-16 and the failure.

## 4. Repair procedure

Work with the board unplugged. The touch overlay is the thin layer on the glass;
its tail plugs into the same flex assembly as the display.

1. **Reseat the flex.** Release the connector latch, withdraw the tail, wipe the
   contacts with isopropyl alcohol on a lint-free swab, let it dry, reinsert fully
   and close the latch. This alone fixes a large share of "contact but no
   position" faults.
2. **Measure the overlay** with a multimeter at the tail (power off):
   | Between | Expected | Meaning if wrong |
   |---|---|---|
   | X+ and X− | 200–900 Ω | open → broken plate tail |
   | Y+ and Y− | 200–900 Ω | open → broken plate tail |
   | X and Y | > 1 MΩ untouched | under ~10 kΩ → layers shorted / crushed |
   | X and Y, finger pressing centre | a few hundred Ω to a few kΩ | no change → overlay dead |
3. **Check for mechanical damage**: a cracked overlay, a crease across the tail, or
   a case/bezel clamping the glass edge. A clamp marks the fault as "always
   pressed at one edge"; PENIRQ at rest rules that out here, so a clamp is
   unlikely on this unit.
4. **Replace** the panel or the whole CYD board if steps 1–3 do not restore a
   position spread. At this price point replacement is usually cheaper than
   re-bonding a flex.

## 5. Verifying the repair (no reflash needed)

Boot the board with serial attached. The firmware already measures everything the
repair needs to prove itself:

| Check | Before repair | After a good repair |
|---|---|---|
| `TOUCH channels ... z1=.. z2=..` | `z1=0 z2=0` | `z2` near 4095 at rest |
| `TOUCH health ... pen_irq=` | flips only with a finger | flips only with a finger (same) |
| Tap five wizard targets | `CAL spread raw_x=0..1 raw_y=1327..1337` | spread of hundreds to thousands of counts |
| `CAL result=` | `FAIL mean_px≈60` | `PASS mean ≤ 6 px, max ≤ 12 px` |

A passing calibration clears the latched fault permanently (`touch_hal_clear_fault`
runs on a valid blob), removes the red `TOUCH` badge, and restores pointer input.
The calibration wizard is also reachable from Settings, so a repaired panel does
not need a wipe.

## 6. What the firmware does in the meantime

- The wizard exits on its own after 45 s with no contact, or within 4 s of a
  detected fault, naming the reason on the glass.
- The status bar shows a red `TOUCH` badge while the fault is latched.
- The HUD stays fully usable as a display; only pointer input is absent.
- The board's BOOT button (GPIO0) now steps through the three functional tabs
  (media → nav → settings → media, one step per press, 250 ms lockout), because
  without it a HUD with no pointer input could not leave the media tab. It is a
  fallback control, not a replacement for the touch screen.
- Fault detection is measurement-based, not a timeout: three press onsets inside
  300 raw counts, a degenerate five-point fit, or 25 saturated presses with no
  contact ever seen.
