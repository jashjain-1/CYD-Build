package com.dashcue.feature.navigation

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.sin

class DouglasPeuckerSimplifierTest {

    @Test
    fun testEmptyPointsReturnsZeroOverview() {
        val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
            routeId = 100L,
            points = emptyList()
        )
        assertEquals(100, overview.routeId)
        assertEquals(0, overview.pointCount)
        assertEquals(0, overview.dlatE5Count)
        assertEquals(0, overview.dlonE5Count)
    }

    @Test
    fun testSinglePointReturnsSinglePointOverview() {
        val pt = GeoPoint(37.7749, -122.4194)
        val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
            routeId = 101L,
            points = listOf(pt)
        )
        assertEquals(1, overview.pointCount)
        assertEquals(0, overview.dlatE5Count)
        assertEquals(0, overview.dlonE5Count)
        assertEquals(3777490, overview.originLatE5)
        assertEquals(-12241940, overview.originLonE5)
    }

    @Test
    fun testTwoPointsProducesOneDelta() {
        val p1 = GeoPoint(37.7749, -122.4194)
        val p2 = GeoPoint(37.7750, -122.4190)
        val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
            routeId = 102L,
            points = listOf(p1, p2)
        )
        assertEquals(2, overview.pointCount)
        assertEquals(1, overview.dlatE5Count)
        assertEquals(1, overview.dlonE5Count)
        assertEquals(10, overview.getDlatE5(0)) // 37.7750 - 37.7749 = 0.0001 * 1e5 = 10
        assertEquals(40, overview.getDlonE5(0)) // -122.4190 - -122.4194 = 0.0004 * 1e5 = 40
    }

    @Test
    fun testCollinearPointsSimplifiedToEndpoints() {
        // Line of 10 perfectly collinear points
        val points = (0..10).map { i ->
            GeoPoint(37.0 + i * 0.001, -122.0 + i * 0.001)
        }
        val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
            routeId = 103L,
            points = points
        )
        // Douglas-Peucker should collapse the collinear interior points
        assertEquals(2, overview.pointCount)
        assertEquals(1, overview.dlatE5Count)
    }

    @Test
    fun testMandatoryVerticesPreserved() {
        // 10 collinear points, but index 5 is mandatory
        val points = (0..10).map { i ->
            GeoPoint(37.0 + i * 0.001, -122.0 + i * 0.001)
        }
        val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
            routeId = 104L,
            points = points,
            mandatoryIndices = setOf(5)
        )
        assertEquals(3, overview.pointCount)
        assertEquals(2, overview.dlatE5Count)
    }

    @Test
    fun testLargeRouteStrictlyCapsAtMaxPoints() {
        // Generate 500 wavy points
        val points = (0 until 500).map { i ->
            val lat = 37.0 + (i * 0.0005)
            val lon = -122.0 + (sin(i * 0.1) * 0.005)
            GeoPoint(lat, lon)
        }

        val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
            routeId = 105L,
            points = points,
            maxPoints = DouglasPeuckerSimplifier.MAX_ROUTE_POINTS
        )

        assertTrue(
            "Point count ${overview.pointCount} must be <= 120",
            overview.pointCount <= 120
        )
        assertTrue(
            "Delta count ${overview.dlatE5Count} must be <= 119",
            overview.dlatE5Count <= 119
        )
        assertEquals(overview.pointCount - 1, overview.dlatE5Count)
        assertEquals(overview.pointCount - 1, overview.dlonE5Count)
    }

    @Test
    fun testDownsampleUniformlyEdgeCases() {
        val points = listOf(
            GeoPoint(1.0, 1.0),
            GeoPoint(2.0, 2.0),
            GeoPoint(3.0, 3.0),
            GeoPoint(4.0, 4.0)
        )

        val target1 = DouglasPeuckerSimplifier.downsampleUniformly(points, 1)
        assertEquals(1, target1.size)
        assertEquals(points.first(), target1[0])

        val target0 = DouglasPeuckerSimplifier.downsampleUniformly(points, 0)
        assertTrue(target0.isEmpty())

        val target2 = DouglasPeuckerSimplifier.downsampleUniformly(points, 2)
        assertEquals(2, target2.size)
        assertEquals(points.first(), target2[0])
        assertEquals(points.last(), target2[1])

        val target10 = DouglasPeuckerSimplifier.downsampleUniformly(points, 10)
        assertEquals(4, target10.size)
    }

    @Test
    fun testDeltaReconstructionAccuracy() {
        val points = listOf(
            GeoPoint(37.77492, -122.41941),
            GeoPoint(37.77583, -122.41825),
            GeoPoint(37.77610, -122.41700),
            GeoPoint(37.77800, -122.41500)
        )

        val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
            routeId = 106L,
            points = points
        )

        assertEquals(4, overview.pointCount)
        var curLatE5 = overview.originLatE5
        var curLonE5 = overview.originLonE5

        for (i in 0 until overview.dlatE5Count) {
            curLatE5 += overview.getDlatE5(i)
            curLonE5 += overview.getDlonE5(i)
        }

        val expectedEndLatE5 = Math.round(points.last().lat * 1e5).toInt()
        val expectedEndLonE5 = Math.round(points.last().lon * 1e5).toInt()

        assertEquals(expectedEndLatE5, curLatE5)
        assertEquals(expectedEndLonE5, curLonE5)
    }
}
