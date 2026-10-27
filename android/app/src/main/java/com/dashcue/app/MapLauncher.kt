package com.dashcue.app

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import com.dashcue.core.common.DashCueLog

/**
 * Cascading map launch.
 *
 * The HUD's Maps button (and the phone's own buttons) must open a navigation
 * app even when the target app is missing, disabled or unable to start. Each
 * step is checked before the next is tried, and the whole chain falls back to a
 * browser, so the button never silently does nothing.
 *
 * Order (researched against the official intent docs):
 *  1. Google Maps turn-by-turn, package-targeted (`google.navigation:`)
 *  2. Waze deep link (app when installed, website otherwise)
 *  3. generic `geo:` handler (chooser for any installed map app)
 *  4. Google Maps universal https URL (works in a browser)
 */
object MapLauncher {
    private const val TAG = "MapLauncher"
    private const val MAPS_PACKAGE = "com.google.android.apps.maps"
    private const val WAZE_PACKAGE = "com.waze"

    /** Result of an attempt: which target handled it, or null when every step failed. */
    sealed class Result(val target: String) {
        object GoogleMaps : Result("google-maps")
        object Waze : Result("waze")
        object GeoChooser : Result("geo-chooser")
        object Browser : Result("browser")
        object Failed : Result("none")
    }

    fun openNavigation(
        context: Context,
        lat: Double? = null,
        lon: Double? = null,
        label: String? = null
    ): Result {
        val point = if (lat != null && lon != null) "$lat,$lon" else null

        // 1. Google Maps turn-by-turn. Package-targeted so the Google-only URI
        //    is never handed to an unrelated handler.
        val navUri = when {
            point != null -> Uri.parse("google.navigation:q=${Uri.encode(point)}&mode=d")
            !label.isNullOrBlank() -> Uri.parse(
                "google.navigation:q=${Uri.encode(label)}&mode=d"
            )
            else -> Uri.parse("geo:0,0")
        }
        if (tryStart(context, Intent(Intent.ACTION_VIEW, navUri).setPackage(MAPS_PACKAGE))) {
            return Result.GoogleMaps
        }

        // 2. Waze: official HTTPS link resolves to the app when installed and
        //    to the website when it is not, so it needs no package check.
        val wazeUri = if (point != null) {
            "https://waze.com/ul?ll=${Uri.encode(point)}&navigate=yes&zoom=17"
        } else {
            "https://waze.com/ul?navigate=yes"
        }
        if (tryStart(context, Intent(Intent.ACTION_VIEW, Uri.parse(wazeUri)))) {
            return Result.Waze
        }

        // 3. Any installed map app (OSM, Organic Maps, ...), via chooser.
        val geoUri = when {
            point != null -> Uri.parse("geo:$point")
            !label.isNullOrBlank() -> Uri.parse("geo:0,0?q=${Uri.encode(label)}")
            else -> Uri.parse("geo:0,0")
        }
        if (tryStart(context, Intent(Intent.ACTION_VIEW, geoUri))) {
            return Result.GeoChooser
        }

        // 4. Universal URL: opens Maps if present, otherwise the browser.
        val webUri = if (point != null) {
            "https://www.google.com/maps/dir/?api=1&destination=${Uri.encode(point)}" +
                "&travelmode=driving&dir_action=navigate"
        } else {
            "https://www.google.com/maps"
        }
        if (tryStart(context, Intent(Intent.ACTION_VIEW, Uri.parse(webUri)))) {
            return Result.Browser
        }

        DashCueLog.w(TAG, "No map handler available for any fallback step")
        return Result.Failed
    }

    private fun tryStart(context: Context, intent: Intent): Boolean {
        return try {
            context.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            DashCueLog.i(TAG, "Map step launched: ${intent.data} pkg=${intent.`package`}")
            true
        } catch (e: ActivityNotFoundException) {
            DashCueLog.i(TAG, "Map step unavailable: ${intent.data}")
            false
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Map step failed: ${e.message}", e)
            false
        }
    }
}