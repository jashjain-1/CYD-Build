# Routing endpoint setup

DashCue does not ship with a routing server (DEC-14). The app asks the user for an
OSRM base URL, and until one is configured and reachable, navigation cannot start. That
is deliberate: shipping a hidden default means shipping someone else's bandwidth, with
someone else's availability, and no honest way for the user to know who sees their
destinations.

> **GATE-10 is `BLOCKED`**: no endpoint has been configured on the development machine,
> so no navigation test has been run. The commands below are documented but **unverified
> in this project**, treat them as a starting point, not as tested instructions.

## 1. What the app needs

| Requirement | Detail |
|---|---|
| API | OSRM HTTP API v5, `/route/v1/driving/{lon,lat};{lon,lat}` |
| Required parameters | `steps=true`, `geometries=polyline6`, `overview=full` |
| Response fields used | `routes[].geometry`, `routes[].legs[].steps[].maneuver.{type,modifier,exit}`, `routes[].legs[].steps[].name`, `routes[].distance`, `routes[].duration`, `waypoints[].location`, and route-level `driving_side` |
| Profile | `driving` (car) |
| Transport | HTTPS for anything off-LAN; cleartext only to a host the user typed |

Geocoding (destination search) is a separate, Nominatim-compatible endpoint. Search is
**on submit only**, no autocomplete, with at most one request per second, a
descriptive `User-Agent`, and cached results, because that is what public Nominatim
policy allows (DEC-16).

## 2. Self-hosting OSRM

OSRM needs a road network prepared offline; it is not a service you can start against
a live map.

```bash
# 1. Get an .osm.pbf extract for the region you drive in (Geofabrik is the usual source).
# 2. Prepare the graph (this is CPU- and RAM-heavy; a small region/state is a good start).
docker run -t -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend \
    osrm-extract -p /opt/car.lua /data/region-latest.osm.pbf
docker run -t -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend \
    osrm-partition /data/region-latest.osrm
docker run -t -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend \
    osrm-customize /data/region-latest.osrm

# 3. Serve it.
docker run -t -i -p 5000:5000 -v "${PWD}:/data" ghcr.io/project-osrm/osrm-backend \
    osrm-routed --algorithm mld /data/region-latest.osrm
```

Then set the endpoint in the app, for example `http://192.168.1.20:5000`.

Practical notes that matter more than the commands:

* **The route server must be reachable from the phone**, on the same LAN or via a
  VPN/tunnel you control. A server on your laptop is useless once the laptop is closed, so plan for something always-on if you want navigation in the car.
* **Extract size drives RAM.** A whole continent will not fit comfortably on a small
  machine; start with the region you actually drive.
* **Roundabout exit numbers and left/right traffic come from the map data.** A small or
  old extract can be missing the `driving_side` information, which changes how
  roundabouts are drawn (DEC-22).
* **There is no live traffic** in OSRM on OSM data (DEC-15). The app labels ETA as
  "estimate, no live traffic" and never claims otherwise.

## 3. Geocoding

A separate Nominatim-compatible endpoint:

```bash
docker run -t -i -p 8080:8080 -v "${PWD}:/data" mediagis/nominatim:4.4 \
    /bin/bash -c "service postgresql start && service nominatim start"
```

Public Nominatim servers are usable for very low-volume personal use under their usage
policy (one request per second, descriptive User-Agent, caching, no bulk queries). They
are not suitable as a shipped default for a distributed app, which is why the endpoint
is a setting.

## 4. Verifying the endpoint

The app's **Diagnostics → Health checks** screen reports:

* whether the routing base URL is configured and syntactically valid;
* whether a route request to a known pair of nearby points returns `steps`;
* whether the geocoding endpoint returns a result for a fixed query;
* the measured round-trip time.

A failing check reports which of these failed, "endpoint unreachable" and "endpoint
returned no route" are different problems with different fixes.

## 5. Why not GraphHopper or Google Routes

Both are evaluated in [ROUTING_EVALUATION.md](ROUTING_EVALUATION.md). Neither is
implemented in v1; the `RoutingProvider` interface exists so that adding one does not
touch the UI or the protocol.
