# Android permissions

Every permission the app declares, why it exists, when it is requested, and what the
app does **without** it. The release manifest is checked against this file by
TC-SEC-02; a permission that appears in the manifest and not here is a release
blocker.

Declared permissions are deliberately minimal (§17): no background location, no
contacts, no SMS, no accessibility service.

---

## 1. Bluetooth

| Permission | API | Purpose | Without it |
|---|---|---|---|
| `BLUETOOTH_SCAN` (`neverForLocation`) | 31+ | find the HUD by its service UUID | cannot scan; the app explains that the HUD must be paired first |
| `BLUETOOTH_CONNECT` | 31+ | connect to and communicate with the bonded HUD | cannot connect |
| `BLUETOOTH` | ≤ 30 | legacy equivalent of the two above | cannot connect |
| `BLUETOOTH_ADMIN` | ≤ 30 | legacy scan/connect | cannot connect |
| `ACCESS_FINE_LOCATION` | ≤ 30 only | Android requires location permission to scan for BLE devices below API 31 | cannot scan; onboarding says exactly this rather than asking vaguely for "location" |

`neverForLocation` is declared because the app genuinely does not derive location from
Bluetooth scans: it filters on the DashCue service UUID. This is what lets the app avoid
location permission entirely on Android 12+ (DEC-08).

CompanionDeviceManager associates the HUD and, on API 31+, lets the app be woken for a
known device instead of scanning. Below API 31 the app falls back to a foreground
service with scanning.

## 2. Location

| Permission | Purpose | Without it |
|---|---|---|
| `ACCESS_FINE_LOCATION` (while in use) | navigation: GPS position for guidance and reroute | navigation cannot start; media control still works |

Deliberately **not** declared: `ACCESS_BACKGROUND_LOCATION`. Navigation runs in a
foreground service started while the app is visible, so background location is not
needed (DEC-19). The coarse-location permission is not requested either: route guidance
needs better than ~2 km accuracy to be meaningful.

## 3. Foreground service

| Permission | Purpose | Without it |
|---|---|---|
| `FOREGROUND_SERVICE` | run navigation/connection reliably while the screen is off | Android kills the session when the screen locks |
| `FOREGROUND_SERVICE_LOCATION` | declare the location service type (API 34+) | start failed at runtime; required by the platform |
| `FOREGROUND_SERVICE_CONNECTED_DEVICE` | declare the connected-device service type (API 34+) | start failed at runtime; required by the platform |
| `POST_NOTIFICATIONS` (33+) | show the persistent navigation/connection notification | the service still runs; the driver loses the notification and its "End navigation" action |

The notification is the app's honest signal that something is running in the
background; without it the driver would have no way to stop navigation from outside the
app.

## 4. Media control

| Permission / component | Purpose | Without it |
|---|---|---|
| `MEDIA_CONTENT_CONTROL` | granted by the notification-listener grant; required to inspect media sessions | media screens show "no session" |
| `BIND_NOTIFICATION_LISTENER_SERVICE` (service permission) | lets the system bind our listener | the listener never runs |
| Notification listener access (user-granted) | `MediaSessionManager.getActiveSessions()` only works for an enabled listener | no media metadata or transport control at all |

The listener reads **media sessions only**. Other notifications are neither read nor
stored nor logged, and the app has no code path that could do so (DEC-20, §10.1).

On Android 13+, sideloaded apps are blocked from this grant under certain conditions
and the toggle is greyed out. The app detects this state and shows the exact steps to
enable it, and reports "not enabled" until the system says otherwise, it never shows a
green tick for a permission it does not have (Rule 4).

## 5. Network

| Permission | Purpose | Without it |
|---|---|---|
| `INTERNET` | routing (OSRM) and geocoding (Nominatim) HTTP requests | no navigation; the rest works |
| `ACCESS_NETWORK_STATE` | distinguish "no network" from "endpoint unreachable" so the error is honest | errors are vaguer |

There is no `requestLegacyExternalStorage`, no `MANAGE_EXTERNAL_STORAGE` and no
`QUERY_ALL_PACKAGES`. Package visibility is handled with an explicit `<queries>` entry
for the media-session intent rather than a blanket declaration.

## 6. Network security

* TLS for every non-LAN endpoint, with certificate validation on.
* Cleartext HTTP is permitted **only** to a host the user typed themselves, via a
  scoped network-security-config domain, and the app warns before accepting one.
* No certificates are bundled and no validation is disabled, including in debug
  builds, a debug-only trust exception is how it ends up in a release.

TC-SEC-03 verifies this by attempting a cleartext request to a host that is not
user-approved and expecting a failure.

## 7. What the app never asks for

Contacts, SMS, call log, microphone, camera, storage, accessibility services, device
admin, exact alarms, `QUERY_ALL_PACKAGES`, background location, or "draw over other
apps". Any of these appearing in a future build is a deviation requiring an entry in
[DECISIONS.md](DECISIONS.md).
