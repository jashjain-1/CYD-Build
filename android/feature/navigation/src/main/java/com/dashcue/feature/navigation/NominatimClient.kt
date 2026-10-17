package com.dashcue.feature.navigation

import com.dashcue.core.common.DashCueLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import java.io.IOException
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

/** One geocoding hit from the operator's Nominatim instance. */
data class PlaceResult(
    val label: String,
    val displayName: String,
    val point: GeoPoint
)

/**
 * Nominatim client (§7.3). The endpoint is user-supplied (DEC-14); there is no
 * baked-in public server. A descriptive User-Agent is mandatory under the OSMF
 * usage policy, so it is always sent.
 */
class NominatimClient(
    private val baseUrl: String,
    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()
) {
    companion object {
        private const val TAG = "NominatimClient"
        private const val USER_AGENT = "DashCue/1.0 (companion app; contact: operator)"
    }

    suspend fun search(query: String, limit: Int = 5): Result<List<PlaceResult>> =
        withContext(Dispatchers.IO) {
            val trimmed = query.trim()
            if (trimmed.isEmpty()) return@withContext Result.success(emptyList())
            if (baseUrl.isBlank()) {
                return@withContext Result.failure(
                    IOException("No Nominatim endpoint configured. Set it in Settings.")
                )
            }

            val encoded = URLEncoder.encode(trimmed, "UTF-8")
            val url = "${baseUrl.trimEnd('/')}/search?q=$encoded&format=jsonv2&limit=${limit.coerceIn(1, 10)}&addressdetails=0"
            DashCueLog.i(TAG, "Geocoding: $url")

            val request = Request.Builder()
                .url(url)
                .header("User-Agent", USER_AGENT)
                .get()
                .build()

            try {
                client.newCall(request).execute().use { response ->
                    if (!response.isSuccessful) {
                        return@withContext Result.failure(
                            IOException("Nominatim HTTP ${response.code}: ${response.message}")
                        )
                    }
                    val body = response.body?.string()
                        ?: return@withContext Result.failure(IOException("Empty Nominatim response"))
                    Result.success(parse(body))
                }
            } catch (e: Exception) {
                DashCueLog.e(TAG, "Geocoding failed", e)
                Result.failure(e)
            }
        }

    fun parse(json: String): List<PlaceResult> {
        val array = JSONArray(json)
        val results = mutableListOf<PlaceResult>()
        for (i in 0 until array.length()) {
            val item = array.optJSONObject(i) ?: continue
            val lat = item.optString("lat").toDoubleOrNull() ?: continue
            val lon = item.optString("lon").toDoubleOrNull() ?: continue
            val display = item.optString("display_name", "")
            if (display.isBlank()) continue
            val label = item.optString("name").takeIf { it.isNotBlank() }
                ?: display.substringBefore(',')
            results.add(
                PlaceResult(
                    label = label.take(60),
                    displayName = display.take(160),
                    point = GeoPoint(lat, lon)
                )
            )
        }
        return results
    }
}
