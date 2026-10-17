# Routing provider evaluation

§7.2 requires a written comparison of the alternatives to OSRM. Nothing here is
implemented in v1; the `RoutingProvider` interface exists so a second provider can be
added without touching the UI or the protocol.

Measurements are deliberately absent: no endpoint has been exercised on this project
(GATE-10 is `BLOCKED`), so this is a comparison of documented properties, not of
observed performance. Cost figures are the providers' published terms and drift over
time — verify before committing to one.

---

## 1. Comparison

| | OSRM (v1 choice, DEC-14) | GraphHopper | Google Routes / Navigation SDK |
|---|---|---|---|
| Model | self-hosted, open source | self-hosted (open core) or hosted API | hosted only |
| Data source | OpenStreetMap | OpenStreetMap | Google |
| Live traffic | **no** | yes when the traffic data is enabled | yes |
| Turn instructions | yes; `maneuver.type/modifier/exit`, `driving_side` | yes, comparable structure | best-in-class, lane-level |
| Lane guidance | limited | yes | yes |
| Offline routing | yes (the self-hosted server is yours; there is still no on-device offline routing in v1) | yes, in the paid self-hosted product | no |
| Credentials | none | none self-hosted; API key hosted | API key + billing + terms acceptance |
| Deployment effort | medium: prepare an OSM extract (CPU/RAM heavy for large regions) | medium to high | none (but a hard dependency) |
| Cost | your own hosting | your own hosting, or per-request | per-request, and Android Navigation SDK pricing is separate |
| Licence/terms constraints | free software; OSM ODbL attribution | self-hosted core is Apache-2.0; hosted service per terms | contractual restrictions on display, caching and derived data |
| Fits "no hidden server" rule | yes — the endpoint is the user's | yes | **no** — it is inherently a third-party service |
| Works in v1 without traffic | yes, ETA labelled an estimate | yes | yes |

## 2. Why OSRM is the v1 choice

* It needs no API key, so the project can ship without holding anyone's credentials
  (Rule 12).
* It returns the manoeuvre data the HUD needs, including `driving_side` and roundabout
  exits (DEC-22).
* It can be self-hosted, which is the only way to satisfy "no pre-existing server" *and*
  "no hidden default" simultaneously: the endpoint is an explicit user setting (DEC-14).
* It has no live traffic, so the product's honesty requirement is simple: never claim
  traffic or speed limits, and label ETA as an estimate (DEC-15).

## 3. What would change the decision

| Trigger | Likely move |
|---|---|
| Users need live traffic or lane guidance | GraphHopper (self-hosted, traffic enabled) keeps the no-credentials property; Google would require rethinking the "no hidden server" rule entirely |
| On-device offline routing becomes a requirement | neither OSRM nor GraphHopper as configured here; this is a different class of product and would need a dedicated offline engine |
| The operator already runs GraphHopper | supporting a second `RoutingProvider` is a small, local change by design |

## 4. Interface constraint

```kotlin
interface RoutingProvider {
    suspend fun route(origin: LatLng, dest: LatLng, options: RouteOptions): RouteResult
}
```

`RouteResult` carries geometry, steps, distance and duration, and a `hasLiveTraffic`
flag that is `false` for anything that cannot prove otherwise. The guidance engine,
the HUD route overview and the protocol are written against this interface, not against
OSRM's JSON — that is the property that makes the table above a decision that can be
revisited rather than a rewrite.
