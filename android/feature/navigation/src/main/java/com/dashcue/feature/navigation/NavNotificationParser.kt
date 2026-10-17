package com.dashcue.feature.navigation

import android.content.Context
import android.service.notification.StatusBarNotification
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import com.dashcue.core.common.DashCueLog
import com.dashcue.v1.Maneuver
import com.dashcue.v1.TrafficLevel
import java.util.Calendar
import java.util.Locale
import java.util.concurrent.TimeUnit

/**
 * Reads the *visible* navigation notification published by Google Maps or Waze.
 *
 * Why this exists: Android exposes no public API that lets a companion app read
 * another app's live route geometry. Maps and Waze both render their live cue in
 * custom RemoteViews, so we read the text those views actually show and map it
 * to DashCue's glanceable NavState. We deliberately do not pretend this is a
 * supported route API:
 *
 *  - no route polyline,
 *  - no per-segment traffic,
 *  - no guarantee that a future app update keeps the same layout.
 *
 * Every field is optional and every parse is guarded. A malformed notification
 * returns null (or a partial cue) instead of taking the listener down.
 */
object NavNotificationParser {

    private const val TAG = "NavParser"
    private const val MAPS_PACKAGE = "com.google.android.apps.maps"
    private const val WAZE_PACKAGE = "com.waze"

    private val supportedPackages = setOf(MAPS_PACKAGE, WAZE_PACKAGE)
    private val distanceRegex = Regex(
        """(\d+(?:[.,]\d+)?)\s*(m|km|mi|ft|yd)\b""",
        RegexOption.IGNORE_CASE
    )
    private val durationRegex = Regex(
        """(\d+)\s*(?:h|hr|hrs|hour|hours)\s*(\d+)?\s*(?:m|min|mins|minute|minutes)?""" +
            """|(\d+)\s*(?:min|mins|minute|minutes)\b""",
        RegexOption.IGNORE_CASE
    )
    private val clockRegex = Regex("""\b([01]?\d|2[0-3]):([0-5]\d)\b""")
    private val clock12Regex = Regex("""\b(\d{1,2}):(\d{2})\s*(am|pm)\b""", RegexOption.IGNORE_CASE)
    private val instructionRegex = Regex(
        """\b(turn|keep|merge|exit|continue|head|take|follow|arrive|arrived|arrival|destination|roundabout|rotary|onto|toward|via)\b""",
        RegexOption.IGNORE_CASE
    )
    private val streetRegex = Regex(
        """(?:onto|on|toward|via|for)\s+([^,•·|\n]{2,48})""",
        RegexOption.IGNORE_CASE
    )
    private val delayRegex = Regex(
        """(\d+)\s*(?:min|mins|minutes)\s*(?:delay|slower|traffic)""",
        RegexOption.IGNORE_CASE
    )
    private val roundaboutExitRegex = Regex(
        """(?:take\s+(?:the\s+)?)?(\d+)(?:st|nd|rd|th)?\s+exit|\broundabout\D{0,12}(\d+)(?:st|nd|rd|th)?\s+exit|\bexit\s+(\d+)\b""",
        RegexOption.IGNORE_CASE
    )

    data class MirroredNavigation(
        val sourcePackage: String,
        val sourceLabel: String,
        val maneuver: Maneuver,
        val instruction: String,
        val street: String,
        val distanceToNextM: Int,
        val remainingM: Int,
        val remainingS: Int,
        val etaEpochS: Long,
        val traffic: TrafficLevel,
        val trafficIsExplicit: Boolean,
        val speedLimitKmh: Int,
        val notificationKey: String,
        val updatedAtMs: Long,
        val roundaboutExit: Int = 0
    )

    fun isSupportedPackage(packageName: String?): Boolean =
        packageName != null && packageName in supportedPackages

    /**
     * @return null when this is not an active navigation notification, or when
     * the notification contains no usable cue at all.
     */
    fun parse(
        context: Context,
        sbn: StatusBarNotification,
        nowMs: Long = System.currentTimeMillis()
    ): MirroredNavigation? {
        if (!isSupportedPackage(sbn.packageName)) return null
        if (!sbn.isOngoing && sbn.notification.category != android.app.Notification.CATEGORY_NAVIGATION) {
            return null
        }
        // Maps and Waze live navigation notifications. We accept ongoing or
        // CATEGORY_NAVIGATION notifications that contain valid turn cues.

        val notification = sbn.notification
        val texts = linkedSetOf<String>()
        val maneuverHints = linkedSetOf<String>()
        addExtras(texts, notification)
        addRemoteViewTexts(context, notification, texts, maneuverHints, sbn.packageName)

        logProgressSegmentsOnce(notification)

        val joined = texts.joinToString(" • ").replace(Regex("\\s+"), " ").trim()
        if (joined.isBlank()) return null
        if (!looksLikeNavigation(joined, sbn)) return null

        val cue = selectCue(texts)
        val distances = distanceRegex.findAll(joined).mapNotNull { match ->
            toMeters(match.groupValues[1], match.groupValues[2])
        }.toList()
        val durations = parseDurations(joined)
        val clock = parseClock(joined, nowMs)

        // Maps' ProgressStyle notification exposes the remaining route length
        // as progressMax (metres). It is not the distance to the next turn, so
        // keep that field at zero rather than putting 986 km next to a turn cue.
        val progressMax = notification.extras?.getInt("android.progressMax", 0) ?: 0
        val progress = notification.extras?.getInt("android.progress", 0) ?: 0
        val progressRemaining = if (progressMax in 1..5_000_000) {
            (progressMax - progress).coerceIn(0, 5_000_000)
        } else 0

        val distanceToNext = distances.firstOrNull() ?: 0
        val remainingDistance = when {
            distances.size >= 2 -> distances.last()
            progressRemaining > 0 -> progressRemaining
            distances.isNotEmpty() -> distances.first()
            else -> 0
        }
        val remainingTime = durations.maxOrNull() ?: clock?.let { eta ->
            ((eta - nowMs / 1000L).coerceAtLeast(0L)).toInt()
        } ?: 0
        val eta = clock ?: if (remainingTime > 0) nowMs / 1000L + remainingTime else 0L

        val instruction = cue ?: texts.firstOrNull().orEmpty()
        val street = streetFrom(instruction, joined)
        val traffic = classifyTraffic(joined, remainingDistance, remainingTime)
        val explicit = traffic.first
        val speedLimit = parseSpeedLimit(joined)

        val computedManeuver = classifyManeuver(((cue ?: instruction) + " " + maneuverHints.joinToString(" ")).trim()).let {
            if (it == Maneuver.MV_NONE) classifyManeuver((joined + " " + maneuverHints.joinToString(" ")).trim())
            else it
        }
        val exitNumber = if (computedManeuver == Maneuver.MV_ROUNDABOUT || joined.contains("roundabout", true) || joined.contains("rotary", true)) {
            extractRoundaboutExit(joined + " " + instruction + " " + maneuverHints.joinToString(" "))
        } else 0

        return MirroredNavigation(
            sourcePackage = sbn.packageName,
            sourceLabel = if (sbn.packageName == WAZE_PACKAGE) "Waze" else "Google Maps",
            maneuver = computedManeuver,
            instruction = instruction.take(96),
            street = street.take(48),
            distanceToNextM = distanceToNext.coerceIn(0, 5_000_000),
            remainingM = remainingDistance.coerceIn(0, 5_000_000),
            remainingS = remainingTime.coerceIn(0, 7 * 24 * 3600),
            etaEpochS = eta,
            traffic = traffic.second,
            trafficIsExplicit = explicit,
            speedLimitKmh = speedLimit,
            notificationKey = sbn.key,
            updatedAtMs = nowMs,
            roundaboutExit = exitNumber
        )
    }

    private fun addExtras(target: MutableSet<String>, notification: android.app.Notification) {
        val extras = notification.extras ?: return
        val keys = listOf(
            android.app.Notification.EXTRA_TITLE,
            android.app.Notification.EXTRA_TITLE_BIG,
            android.app.Notification.EXTRA_TEXT,
            android.app.Notification.EXTRA_SUB_TEXT,
            android.app.Notification.EXTRA_BIG_TEXT,
            android.app.Notification.EXTRA_INFO_TEXT,
            android.app.Notification.EXTRA_SUMMARY_TEXT
        )
        for (key in keys) {
            val value = extras.getCharSequence(key)?.toString()?.trim()
            if (!value.isNullOrBlank()) target.add(value)
        }
        // Do not iterate every key: a Bundle value logged as a CharSequence
        // produces a BadParcelableException warning in newer Android builds.
        // The known keys above cover Maps/Waze's progress-style notification.
    }

    /**
     * Maps renders the cue with private resource ids, so there is no public
     * findViewById contract. Applying the RemoteViews and walking its visible
     * TextViews is the least brittle option: it needs no reflection and no
     * private resource names, and it fails closed if the layout changes.
     */
    private val segmentsLogged = java.util.concurrent.atomic.AtomicBoolean(false)

    @Suppress("DEPRECATION")
    private fun logProgressSegmentsOnce(notification: android.app.Notification) {
        if (!segmentsLogged.compareAndSet(false, true)) return
        try {
            val segments = notification.extras
                ?.getParcelableArrayList<android.os.Bundle>("android.progressSegments")
                ?: return
            segments.take(2).forEach { bundle ->
                DashCueLog.i(
                    TAG,
                    "progressSegment keys=${bundle.keySet()} values=" +
                        bundle.keySet().joinToString { key ->
                            "$key=${runCatching { bundle.get(key) }.getOrNull()}"
                        }
                )
            }
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Could not inspect progress segments", e)
        }
    }

    private fun addRemoteViewTexts(
        context: Context,
        notification: android.app.Notification,
        target: MutableSet<String>,
        maneuverHints: MutableSet<String>,
        packageName: String
    ) {
        val remoteViews = notification.bigContentView ?: notification.contentView ?: return
        val resources = try {
            context.packageManager.getResourcesForApplication(packageName)
        } catch (e: Exception) {
            null
        }
        try {
            val root = remoteViews.apply(context, FrameLayout(context))
            collectText(root, target, maneuverHints, resources, 0)
        } catch (e: Exception) {
            DashCueLog.w(TAG, "RemoteViews could not be inflated", e)
        }
    }

    /**
     * Maps/Waze render the maneuver as a drawable in an ImageView. The image
     * itself is not public, but the view id is a resource id in the source
     * package, and its entry name ("nav_turn_left", "ic_roundabout", ...) is a
     * far stronger signal than the visible text, which often only names the
     * street. Resolution is best-effort: obfuscated or missing ids are ignored.
     */
    private fun collectText(
        view: View,
        target: MutableSet<String>,
        maneuverHints: MutableSet<String>,
        resources: android.content.res.Resources?,
        depth: Int
    ) {
        if (depth > 12) return
        if (view.visibility != View.VISIBLE) return
        if (view is TextView && target.size < 32) {
            val text = view.text?.toString()?.trim()
            if (!text.isNullOrBlank() && text.length <= 160) target.add(text)
        }
        if (view is android.widget.ImageView) {
            val desc = view.contentDescription?.toString()?.trim()
            if (!desc.isNullOrBlank() && desc.length <= 160) {
                target.add(desc)
                if (maneuverHints.size < 4) maneuverHints.add(desc)
            }
            val tag = view.tag?.toString()?.trim()
            if (!tag.isNullOrBlank() && tag.length <= 160) {
                target.add(tag)
                if (maneuverHints.size < 4) maneuverHints.add(tag)
            }
            if (resources != null && maneuverHints.size < 4 && view.id != View.NO_ID) {
                try {
                    val name = resources.getResourceEntryName(view.id)
                    if (name.isNotBlank() && !name.matches(Regex("""^res_0x[0-9a-fA-F]+$"""))) {
                        maneuverHints.add(name.replace('_', ' '))
                    }
                } catch (e: Exception) {
                    // A view without an id (NO_ID) or an unreadable resource name is
                    // not a maneuver hint; keep walking.
                }
            }
        }
        if (view is ViewGroup) {
            for (i in 0 until view.childCount) {
                collectText(view.getChildAt(i), target, maneuverHints, resources, depth + 1)
            }
        }
    }

    private fun extractRoundaboutExit(text: String): Int {
        val match = roundaboutExitRegex.find(text) ?: return 0
        val num = match.groupValues[1].ifEmpty {
            match.groupValues.getOrNull(2)?.ifEmpty {
                match.groupValues.getOrNull(3)
            }
        }
        return num?.toIntOrNull()?.coerceIn(1, 12) ?: 0
    }

    private fun looksLikeNavigation(text: String, sbn: StatusBarNotification): Boolean {
        val title = sbn.notification.extras?.getCharSequence(android.app.Notification.EXTRA_TITLE)?.toString().orEmpty()
        val combined = "$title $text".lowercase(Locale.ROOT)
        if (combined.contains("location sharing") ||
            combined.contains("sharing your location") ||
            combined.contains("downloading offline") ||
            combined.contains("offline area") ||
            combined.contains("background updates")
        ) {
            return false
        }

        val category = sbn.notification.category
        val channel = sbn.notification.channelId.orEmpty()
        val hasNavCues = instructionRegex.containsMatchIn(text) ||
            distanceRegex.containsMatchIn(text) ||
            durationRegex.containsMatchIn(text) ||
            clockRegex.containsMatchIn(text) ||
            clock12Regex.containsMatchIn(text) ||
            text.contains("ETA", ignoreCase = true)

        if (category == android.app.Notification.CATEGORY_NAVIGATION && hasNavCues) return true
        if (channel.contains("navigation", ignoreCase = true) && hasNavCues) return true
        if (sbn.packageName in supportedPackages && sbn.isOngoing && hasNavCues) return true

        return hasNavCues
    }

    private fun selectCue(texts: Set<String>): String? {
        val candidates = texts.filter { it.length >= 3 }
        return candidates.firstOrNull { instructionRegex.containsMatchIn(it) }
            ?: candidates.firstOrNull { it.contains("onto ", true) || it.contains("exit ", true) }
            ?: candidates.maxByOrNull { it.length }
    }

    private fun toMeters(number: String, unit: String): Int? {
        val value = number.replace(',', '.').toDoubleOrNull() ?: return null
        val multiplier = when (unit.lowercase(Locale.US)) {
            "m" -> 1.0
            "km" -> 1000.0
            "mi" -> 1609.344
            "ft" -> 0.3048
            "yd" -> 0.9144
            else -> return null
        }
        val meters = value * multiplier
        if (meters < 0 || meters > 5_000_000) return null
        return meters.toInt()
    }

    private fun parseDurations(text: String): List<Int> {
        val result = mutableListOf<Int>()
        for (match in durationRegex.findAll(text)) {
            val hours = match.groupValues[1].toIntOrNull()
            val hoursMinutes = match.groupValues[2].toIntOrNull()
            val minutesOnly = match.groupValues[3].toIntOrNull()
            val seconds = when {
                hours != null -> hours * 3600 + (hoursMinutes ?: 0) * 60
                minutesOnly != null -> minutesOnly * 60
                else -> 0
            }
            if (seconds in 1..(7 * 24 * 3600)) result.add(seconds)
        }
        return result
    }

    private fun parseClock(text: String, nowMs: Long): Long? {
        val match12 = clock12Regex.find(text)
        val (finalHour, finalMinute) = if (match12 != null) {
            val h = match12.groupValues[1].toIntOrNull() ?: return null
            val m = match12.groupValues[2].toIntOrNull() ?: return null
            val pm = match12.groupValues[3].equals("pm", ignoreCase = true)
            val adjustedHour = when {
                pm && h < 12 -> h + 12
                !pm && h == 12 -> 0
                else -> h
            }
            adjustedHour to m
        } else {
            val match24 = clockRegex.find(text) ?: return null
            val h = match24.groupValues[1].toIntOrNull() ?: return null
            val m = match24.groupValues[2].toIntOrNull() ?: return null
            h to m
        }

        val calendar = Calendar.getInstance().apply {
            timeInMillis = nowMs
            set(Calendar.HOUR_OF_DAY, finalHour)
            set(Calendar.MINUTE, finalMinute)
            set(Calendar.SECOND, 0)
            set(Calendar.MILLISECOND, 0)
        }
        // A bare "18:42" is today's clock time; if that is already well in the
        // past it is tomorrow's arrival (a night drive crossing midnight).
        if (calendar.timeInMillis < nowMs - TimeUnit.MINUTES.toMillis(90)) {
            calendar.add(Calendar.DAY_OF_YEAR, 1)
        }
        return calendar.timeInMillis / 1000L
    }

    private fun streetFrom(instruction: String, allText: String): String {
        streetRegex.find(instruction)?.groupValues?.getOrNull(1)?.trim()?.let { return it }
        val candidate = streetRegex.find(allText)?.groupValues?.getOrNull(1)?.trim()
        if (candidate != null &&
            !candidate.equals("time", ignoreCase = true) &&
            !candidate.matches(Regex("""^\d+\s*(?:m|km|mi|ft|yd|min|mins|hr|hrs).*""", RegexOption.IGNORE_CASE))
        ) {
            return candidate
        }
        return instruction.take(48)
    }

    private fun classifyManeuver(text: String): Maneuver {
        val lower = text.lowercase(Locale.US)
        val left = lower.contains("left")
        val right = lower.contains("right")
        // Turn directions are checked BEFORE arrival: a live route instruction
        // that merely mentions "destination" (e.g. "toward Destination Beach")
        // must not be misread as the route having ended.
        return when {
            lower.contains("roundabout") || lower.contains("rotary") ||
                Regex("""\b\d+(?:st|nd|rd|th)?\s+exit\b""").containsMatchIn(lower) -> Maneuver.MV_ROUNDABOUT
            lower.contains("u-turn") || lower.contains("uturn") || lower.contains("u turn") -> Maneuver.MV_UTURN
            lower.contains("slight left") -> Maneuver.MV_SLIGHT_LEFT
            lower.contains("slight right") -> Maneuver.MV_SLIGHT_RIGHT
            lower.contains("sharp left") -> Maneuver.MV_SHARP_LEFT
            lower.contains("sharp right") -> Maneuver.MV_SHARP_RIGHT
            lower.contains("fork left") || lower.contains("keep left") -> Maneuver.MV_FORK_LEFT
            lower.contains("fork right") || lower.contains("keep right") -> Maneuver.MV_FORK_RIGHT
            lower.contains("merge") -> Maneuver.MV_MERGE
            lower.contains("exit left") || lower.contains("ramp left") || lower.contains("left exit") -> Maneuver.MV_RAMP_LEFT
            lower.contains("exit right") || lower.contains("ramp right") || lower.contains("right exit") ||
                lower.contains("take exit") || lower.contains("exit ") || lower.contains("exit\n") -> Maneuver.MV_RAMP_RIGHT
            lower.contains("turn left") || lower.contains("left onto") -> Maneuver.MV_TURN_LEFT
            lower.contains("turn right") || lower.contains("right onto") -> Maneuver.MV_TURN_RIGHT
            // Destination cues
            lower.contains("destination") && lower.contains("left") -> Maneuver.MV_ARRIVE_LEFT
            lower.contains("destination") && lower.contains("right") -> Maneuver.MV_ARRIVE_RIGHT
            lower.contains("you have arrived") || lower.contains("destination reached") ||
                lower.contains("have reached your destination") || lower.contains("arrived at") ||
                (lower.contains("arrive") && !Regex("""\barrive\s*(?:at|by)?\s*\d{1,2}:\d{2}""").containsMatchIn(lower) && !lower.contains("min") && !lower.contains("km") && !lower.contains("mi")) -> Maneuver.MV_ARRIVE
            // Maps often shows only "<street> on the left/right". Treat that as
            // a turn rather than discarding the only directional cue.
            left && !right -> Maneuver.MV_TURN_LEFT
            right && !left -> Maneuver.MV_TURN_RIGHT
            lower.contains("continue") || lower.contains("head ") || lower.contains("straight") ->
                Maneuver.MV_CONTINUE
            else -> Maneuver.MV_NONE
        }
    }

    /** Returns (explicitDelay, level). */
    private fun classifyTraffic(text: String, remainingM: Int, remainingS: Int): Pair<Boolean, TrafficLevel> {
        val lower = text.lowercase(Locale.US)
        val explicitDelay = delayRegex.find(lower)?.groupValues?.getOrNull(1)?.toIntOrNull()
        val heavy = lower.contains("heavy traffic") || lower.contains("severe traffic") ||
            lower.contains("traffic jam") || lower.contains("standstill") ||
            (explicitDelay != null && explicitDelay >= 10)
        val moderate = lower.contains("moderate traffic") || lower.contains("slow traffic") ||
            (explicitDelay != null && explicitDelay in 3..9)

        if (heavy) return true to TrafficLevel.TL_HEAVY
        if (moderate) return true to TrafficLevel.TL_MODERATE

        // Average speed is a measurement, not a traffic feed. Use it only when
        // both the distance and the time are present and large enough to mean
        // something; otherwise report UNKNOWN rather than inventing a colour.
        if (remainingM >= 800 && remainingS >= 90) {
            val kmh = (remainingM / 1000.0) / (remainingS / 3600.0)
            return when {
                kmh < 14 -> false to TrafficLevel.TL_SEVERE
                kmh < 24 -> false to TrafficLevel.TL_HEAVY
                kmh < 40 -> false to TrafficLevel.TL_MODERATE
                else -> false to TrafficLevel.TL_CLEAR
            }
        }
        return false to TrafficLevel.TL_UNKNOWN
    }

    private fun parseSpeedLimit(text: String): Int {
        val match = Regex(
            """(?:speed limit|limit)\D{0,8}(\d{2,3})""",
            RegexOption.IGNORE_CASE
        ).find(text) ?: return 0
        return match.groupValues[1].toIntOrNull()?.coerceIn(0, 200) ?: 0
    }
}
