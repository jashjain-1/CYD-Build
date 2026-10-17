package com.dashcue.feature.navigation

import com.dashcue.v1.RouteOverview
import java.util.TreeSet
import kotlin.math.*

object DouglasPeuckerSimplifier {
    const val MAX_ROUTE_POINTS = 120
    private const val INITIAL_EPSILON = 0.00005 // roughly 5 meters in degrees

    /**
     * Simplifies full route geometry down to at most 120 points,
     * preserving all maneuver step vertices and endpoints, and encodes into RouteOverview.
     */
    fun simplifyAndEncode(
        routeId: Long,
        points: List<GeoPoint>,
        mandatoryIndices: Set<Int> = emptySet(),
        maxPoints: Int = MAX_ROUTE_POINTS
    ): RouteOverview {
        if (points.isEmpty()) {
            return RouteOverview.newBuilder()
                .setRouteId(routeId.toInt())
                .setPointCount(0)
                .build()
        }

        if (points.size <= 2) {
            return encodeToRouteOverview(routeId, points)
        }

        // Build list of fixed mandatory indices: 0, last index, and any step indices within range
        val fixedIndices = TreeSet<Int>()
        fixedIndices.add(0)
        fixedIndices.add(points.size - 1)
        for (idx in mandatoryIndices) {
            if (idx in points.indices) {
                fixedIndices.add(idx)
            }
        }

        var epsilon = INITIAL_EPSILON
        var simplifiedIndices = runSimplification(points, fixedIndices, epsilon)

        var iterations = 0
        while (simplifiedIndices.size > maxPoints && iterations < 50) {
            epsilon *= 1.5
            simplifiedIndices = runSimplification(points, fixedIndices, epsilon)
            iterations++
        }

        // Defensive fallback: if fixedIndices alone exceeded maxPoints or epsilon reached limit
        var finalPoints = simplifiedIndices.map { points[it] }
        if (finalPoints.size > maxPoints) {
            finalPoints = downsampleUniformly(finalPoints, maxPoints)
        }

        return encodeToRouteOverview(routeId, finalPoints)
    }

    private fun runSimplification(
        points: List<GeoPoint>,
        fixedIndices: Set<Int>,
        epsilon: Double
    ): List<Int> {
        val sortedFixed = fixedIndices.sorted()
        val resultIndices = mutableSetOf<Int>()

        for (i in 0 until sortedFixed.size - 1) {
            val startIdx = sortedFixed[i]
            val endIdx = sortedFixed[i + 1]
            val segmentResult = simplifySection(points, startIdx, endIdx, epsilon)
            resultIndices.addAll(segmentResult)
        }

        return resultIndices.sorted()
    }

    private fun simplifySection(
        points: List<GeoPoint>,
        startIdx: Int,
        endIdx: Int,
        epsilon: Double
    ): List<Int> {
        if (endIdx <= startIdx + 1) {
            return listOf(startIdx, endIdx)
        }

        var maxDist = 0.0
        var maxIndex = startIdx
        val p1 = points[startIdx]
        val p2 = points[endIdx]

        for (i in (startIdx + 1) until endIdx) {
            val dist = perpendicularDistance(points[i], p1, p2)
            if (dist > maxDist) {
                maxDist = dist
                maxIndex = i
            }
        }

        return if (maxDist > epsilon) {
            val left = simplifySection(points, startIdx, maxIndex, epsilon)
            val right = simplifySection(points, maxIndex, endIdx, epsilon)
            // Combine while removing duplicate maxIndex
            left.dropLast(1) + right
        } else {
            listOf(startIdx, endIdx)
        }
    }

    private fun perpendicularDistance(p: GeoPoint, p1: GeoPoint, p2: GeoPoint): Double {
        val dx = p2.lon - p1.lon
        val dy = p2.lat - p1.lat
        val mag = sqrt(dx * dx + dy * dy)
        if (mag == 0.0) {
            return sqrt((p.lon - p1.lon).pow(2) + (p.lat - p1.lat).pow(2))
        }
        val cross = abs((p2.lat - p1.lat) * (p1.lon - p.lon) - (p1.lat - p.lat) * (p2.lon - p1.lon))
        return cross / mag
    }

    internal fun downsampleUniformly(points: List<GeoPoint>, targetCount: Int): List<GeoPoint> {
        if (targetCount <= 0 || points.isEmpty()) return emptyList()
        if (targetCount == 1) return listOf(points.first())
        if (points.size <= targetCount) return points
        val result = ArrayList<GeoPoint>(targetCount)
        result.add(points.first())
        val step = (points.size - 1).toDouble() / (targetCount - 1)
        for (i in 1 until targetCount - 1) {
            val idx = (i * step).roundToInt()
            result.add(points[idx])
        }
        result.add(points.last())
        return result
    }

    private fun encodeToRouteOverview(routeId: Long, points: List<GeoPoint>): RouteOverview {
        val builder = RouteOverview.newBuilder()
            .setRouteId(routeId.toInt())
            .setPointCount(points.size)

        if (points.isEmpty()) return builder.build()

        val originLatE5 = Math.round(points[0].lat * 1e5).toInt()
        val originLonE5 = Math.round(points[0].lon * 1e5).toInt()
        builder.setOriginLatE5(originLatE5)
        builder.setOriginLonE5(originLonE5)

        var curLatE5 = originLatE5
        var curLonE5 = originLonE5

        for (i in 0 until points.size - 1) {
            val nextLatE5 = Math.round(points[i + 1].lat * 1e5).toInt()
            val nextLonE5 = Math.round(points[i + 1].lon * 1e5).toInt()

            val dlat = nextLatE5 - curLatE5
            val dlon = nextLonE5 - curLonE5

            builder.addDlatE5(dlat)
            builder.addDlonE5(dlon)

            curLatE5 = nextLatE5
            curLonE5 = nextLonE5
        }

        return builder.build()
    }
}
