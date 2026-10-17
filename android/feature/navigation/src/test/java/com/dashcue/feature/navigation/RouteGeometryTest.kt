package com.dashcue.feature.navigation

import com.dashcue.v1.RouteOverview
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class RouteGeometryTest {

    @Test
    fun deltasExpandToAbsolutePoints() {
        val overview = RouteOverview.newBuilder()
            .setOriginLatE5(2_851_000)
            .setOriginLonE5(7_720_000)
            .addDlatE5(100)
            .addDlonE5(-200)
            .addDlatE5(-50)
            .addDlonE5(400)
            .setPointCount(3)
            .build()

        val points = routeOverviewPoints(overview)

        assertEquals(3, points.size)
        assertEquals(28.51, points[0].lat, 1e-9)
        assertEquals(77.20, points[0].lon, 1e-9)
        assertEquals(28.511, points[1].lat, 1e-9)
        assertEquals(77.198, points[1].lon, 1e-9)
        assertEquals(28.5105, points[2].lat, 1e-9)
        assertEquals(77.202, points[2].lon, 1e-9)
    }

    @Test
    fun emptyOrMissingOverviewIsSafe() {
        assertTrue(routeOverviewPoints(null).isEmpty())
        assertTrue(routeOverviewPoints(RouteOverview.getDefaultInstance()).isEmpty())
    }

    @Test
    fun nominatimJsonParsesLatLon() {
        val json = """
            [
              {"lat":"51.5007","lon":"-0.1246","display_name":"Big Ben, London, England","name":"Big Ben"},
              {"lat":"48.8584","lon":"2.2945","display_name":"Eiffel Tower, Paris, France"}
            ]
        """.trimIndent()

        val results = NominatimClient("http://example.invalid").parse(json)

        assertEquals(2, results.size)
        assertEquals("Big Ben", results[0].label)
        assertEquals(51.5007, results[0].point.lat, 1e-9)
        assertEquals(-0.1246, results[0].point.lon, 1e-9)
        assertEquals("Eiffel Tower", results[1].label)
    }
}
