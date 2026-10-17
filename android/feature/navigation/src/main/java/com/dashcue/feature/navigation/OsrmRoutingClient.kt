package com.dashcue.feature.navigation

import com.dashcue.core.common.DashCueLog
import com.dashcue.v1.Maneuver
import com.dashcue.v1.RouteOverview
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.io.IOException
import java.text.Normalizer
import java.util.Locale
import java.util.concurrent.TimeUnit

data class RouteStep(
    val maneuver: Maneuver,
    val street: String,
    val distanceM: Double,
    val durationS: Double,
    val location: GeoPoint,
    val roundaboutExit: Int = 0
)

data class NavigationRoute(
    val routeId: Long,
    val totalDistanceM: Double,
    val totalDurationS: Double,
    val geometryPolyline6: String,
    val points: List<GeoPoint>,
    val steps: List<RouteStep>,
    val overview: RouteOverview
)

object ManeuverMapper {
    fun mapManeuver(type: String?, modifier: String?): Maneuver {
        val t = type?.lowercase(Locale.ROOT) ?: ""
        val m = modifier?.lowercase(Locale.ROOT) ?: ""

        return when (t) {
            "depart" -> Maneuver.MV_DEPART
            "arrive" -> when {
                m.contains("left") -> Maneuver.MV_ARRIVE_LEFT
                m.contains("right") -> Maneuver.MV_ARRIVE_RIGHT
                else -> Maneuver.MV_ARRIVE
            }
            "roundabout", "rotary" -> Maneuver.MV_ROUNDABOUT
            "merge" -> Maneuver.MV_MERGE
            "on ramp", "off ramp", "ramp" -> {
                if (m.contains("left")) Maneuver.MV_RAMP_LEFT else Maneuver.MV_RAMP_RIGHT
            }
            "fork" -> {
                if (m.contains("left")) Maneuver.MV_FORK_LEFT else Maneuver.MV_FORK_RIGHT
            }
            "uturn" -> Maneuver.MV_UTURN
            "turn", "end of road", "continue", "new name" -> when (m) {
                "uturn" -> Maneuver.MV_UTURN
                "sharp left" -> Maneuver.MV_SHARP_LEFT
                "left" -> Maneuver.MV_TURN_LEFT
                "slight left" -> Maneuver.MV_SLIGHT_LEFT
                "sharp right" -> Maneuver.MV_SHARP_RIGHT
                "right" -> Maneuver.MV_TURN_RIGHT
                "slight right" -> Maneuver.MV_SLIGHT_RIGHT
                "straight" -> Maneuver.MV_CONTINUE
                else -> Maneuver.MV_CONTINUE
            }
            else -> Maneuver.MV_CONTINUE
        }
    }

    /**
     * Transliterates text to Latin script and clamps to 48 characters (spec DEC-21).
     */
    fun transliterateToLatin(text: String): String {
        val normalized = Normalizer.normalize(text, Normalizer.Form.NFD)
        val ascii = normalized.replace("\\p{InCombiningDiacriticalMarks}+".toRegex(), "")
        return ascii.take(48)
    }
}

class OsrmRoutingClient(
    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build(),
    private val baseUrl: String = "https://router.project-osrm.org"
) {
    companion object {
        private const val TAG = "OsrmRoutingClient"
    }

    suspend fun calculateRoute(
        origin: GeoPoint,
        destination: GeoPoint,
        routeId: Long = System.currentTimeMillis() and 0xFFFFFFFFL
    ): Result<NavigationRoute> = withContext(Dispatchers.IO) {
        val url = "$baseUrl/route/v1/driving/${origin.lon},${origin.lat};${destination.lon},${destination.lat}?steps=true&geometries=polyline6&overview=full"
        DashCueLog.i(TAG, "Requesting OSRM route: $url")

        val request = Request.Builder()
            .url(url)
            .get()
            .build()

        try {
            val response = client.newCall(request).execute()
            if (!response.isSuccessful) {
                return@withContext Result.failure(IOException("OSRM HTTP ${response.code}: ${response.message}"))
            }

            val body = response.body?.string() ?: return@withContext Result.failure(IOException("Empty OSRM response"))
            parseOsrmResponse(body, routeId)
        } catch (e: Exception) {
            DashCueLog.e(TAG, "Failed to calculate OSRM route", e)
            Result.failure(e)
        }
    }

    fun parseOsrmResponse(jsonString: String, routeId: Long): Result<NavigationRoute> {
        return try {
            val root = JSONObject(jsonString)
            val code = root.optString("code", "")
            if (code != "Ok") {
                return Result.failure(IOException("OSRM error code: $code"))
            }

            val routesArray = root.getJSONArray("routes")
            if (routesArray.length() == 0) {
                return Result.failure(IOException("No routes found in OSRM response"))
            }

            val routeObj = routesArray.getJSONObject(0)
            val totalDistance = routeObj.getDouble("distance")
            val totalDuration = routeObj.getDouble("duration")
            val geometry = routeObj.getString("geometry")

            val points = PolylineDecoder.decodePolyline6(geometry)
            val stepsList = mutableListOf<RouteStep>()
            val stepIndices = mutableSetOf<Int>()

            val legs = routeObj.getJSONArray("legs")
            if (legs.length() > 0) {
                val leg = legs.getJSONObject(0)
                val steps = leg.getJSONArray("steps")
                for (i in 0 until steps.length()) {
                    val s = steps.getJSONObject(i)
                    val dist = s.getDouble("distance")
                    val dur = s.getDouble("duration")
                    val name = s.optString("name", "")
                    val transliterated = ManeuverMapper.transliterateToLatin(name)

                    val manObj = s.getJSONObject("maneuver")
                    val type = manObj.optString("type", "")
                    val modifier = manObj.optString("modifier", "")
                    val exit = manObj.optInt("exit", 0)

                    val locArray = manObj.getJSONArray("location")
                    val stepLoc = GeoPoint(locArray.getDouble(1), locArray.getDouble(0)) // OSRM is [lon, lat]

                    val maneuver = ManeuverMapper.mapManeuver(type, modifier)
                    stepsList.add(
                        RouteStep(
                            maneuver = maneuver,
                            street = transliterated,
                            distanceM = dist,
                            durationS = dur,
                            location = stepLoc,
                            roundaboutExit = exit
                        )
                    )

                    // Find closest polyline point index for this step vertex
                    val closestIndex = findClosestPointIndex(stepLoc, points)
                    if (closestIndex != -1) {
                        stepIndices.add(closestIndex)
                    }
                }
            }

            val overview = DouglasPeuckerSimplifier.simplifyAndEncode(
                routeId = routeId,
                points = points,
                mandatoryIndices = stepIndices
            )

            Result.success(
                NavigationRoute(
                    routeId = routeId,
                    totalDistanceM = totalDistance,
                    totalDurationS = totalDuration,
                    geometryPolyline6 = geometry,
                    points = points,
                    steps = stepsList,
                    overview = overview
                )
            )
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    private fun findClosestPointIndex(target: GeoPoint, points: List<GeoPoint>): Int {
        if (points.isEmpty()) return -1
        var bestIdx = 0
        var bestDist = Double.MAX_VALUE
        for (i in points.indices) {
            val p = points[i]
            val d = (p.lat - target.lat) * (p.lat - target.lat) + (p.lon - target.lon) * (p.lon - target.lon)
            if (d < bestDist) {
                bestDist = d
                bestIdx = i
            }
        }
        return bestIdx
    }
}
