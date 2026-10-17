package com.dashcue.feature.navigation

import com.dashcue.v1.RouteOverview

/**
 * Expands the compact RouteOverview wire form back into absolute points.
 *
 * Point 0 is (origin_lat_e5, origin_lon_e5); each following point is the
 * previous point plus the corresponding delta. Coordinates are degrees * 1e5.
 */
fun routeOverviewPoints(overview: RouteOverview?): List<GeoPoint> {
    if (overview == null || overview.pointCount <= 0) return emptyList()

    val count = minOf(overview.pointCount, overview.dlatE5Count + 1, overview.dlonE5Count + 1)
    if (count <= 0) return emptyList()

    var latE5 = overview.originLatE5.toLong()
    var lonE5 = overview.originLonE5.toLong()
    val points = ArrayList<GeoPoint>(count)
    points.add(GeoPoint(latE5 / 1e5, lonE5 / 1e5))

    for (i in 0 until count - 1) {
        latE5 += overview.getDlatE5(i)
        lonE5 += overview.getDlonE5(i)
        points.add(GeoPoint(latE5 / 1e5, lonE5 / 1e5))
    }
    return points
}
