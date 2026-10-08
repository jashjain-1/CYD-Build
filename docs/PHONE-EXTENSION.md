# Phone Extension — CUE protocol

The head unit accepts external turn-by-turn cues over the serial console.
There is **no Bluetooth transport and no companion app** in this project; any
transport that can write ASCII to the UART (USB host, BLE-UART bridge, an
future phone app) can speak the same protocol.

## Format

```
CUE <ICON>|<distance>|<street>|<eta>|<speed>
```

Exactly five `|`-separated ASCII fields (0x20–0x7E), for example:

```
CUE LEFT|250 m|MG Road|12 min|40
```

| Field | Rules |
|-------|-------|
| ICON | one of `STRAIGHT LEFT RIGHT SLEFT SRIGHT UTURN ROUND ARRIVE` |
| distance | 1–15 chars, non-empty (e.g. `250 m`, `1.2 km`) |
| street | 1–31 chars, non-empty |
| eta | 1–15 chars, non-empty (use `-` when unknown) |
| speed | decimal 0–300, no trailing junk, finite (rejects `nan`, `-1`, `301`) |

Malformed input changes **nothing** and answers with the usage hint.

## Expiry

A cue takes over guidance (`NavSource::Phone`) and is **re-validated every
second; it expires 10 000 ms after arrival** (`PHONE_CUE_STALE_MS`) unless
refreshed. On expiry guidance falls back to GPS waypoint mode or `None` when
there is no fix. Trip-computer accounting is never interrupted by cues — phone
cues do not add distance.

## UI behaviour while a cue is active

- Status chip shows `EXTERNAL CUE` (green)
- NAVIGATION screen shows the cued icon, distance, street and ETA
- Banner footnote reads `External cue (10s timeout)`

## Testing

Covered by `firmware/test/services_host_test.cpp`: field validation matrix,
NaN/infinity/overflow rejection, expiry timing, millis() wraparound, and
fallback to GPS source. Run with `./run-tests.sh`.
