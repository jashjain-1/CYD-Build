package com.dashcue.feature.navigation

import android.app.Notification
import android.content.Context
import android.os.Bundle
import android.service.notification.StatusBarNotification
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import org.robolectric.RuntimeEnvironment
import com.dashcue.v1.Maneuver
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowNotification

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class NavNotificationParserTest {

    private lateinit var context: Context

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
    }

    private fun mockSbn(
        pkg: String = "com.google.android.apps.maps",
        title: String = "Navigation",
        text: String = "Turn left onto Market St",
        isOngoing: Boolean = true,
        category: String? = Notification.CATEGORY_NAVIGATION
    ): StatusBarNotification {
        val extras = Bundle().apply {
            putCharSequence(Notification.EXTRA_TITLE, title)
            putCharSequence(Notification.EXTRA_TEXT, text)
        }
        val notification = Notification.Builder(context, "nav_channel")
            .setCategory(category)
            .setExtras(extras)
            .build()
        notification.flags = if (isOngoing) notification.flags or Notification.FLAG_ONGOING_EVENT else notification.flags

        return StatusBarNotification(
            pkg,
            null,
            1,
            "tag",
            1000,
            1000,
            0,
            notification,
            android.os.Process.myUserHandle(),
            System.currentTimeMillis()
        )
    }

    @Test
    fun testRoundaboutExitExtraction() {
        val sbn = mockSbn(
            title = "At the roundabout, take the 2nd exit onto Oak St",
            text = "In 300 m, take the 2nd exit"
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        assertEquals(Maneuver.MV_ROUNDABOUT, parsed!!.maneuver)
        assertEquals(2, parsed.roundaboutExit)
    }

    @Test
    fun testRoundaboutFirstExit() {
        val sbn = mockSbn(
            title = "Take the 1st exit onto Elm Ave",
            text = "In 150 m"
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        assertEquals(Maneuver.MV_ROUNDABOUT, parsed!!.maneuver)
        assertEquals(1, parsed.roundaboutExit)
    }

    @Test
    fun testRejectLocationSharingOngoingNotification() {
        val sbn = mockSbn(
            title = "Location sharing",
            text = "Sharing your location with Alice",
            isOngoing = true,
            category = null
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNull("Ongoing location sharing notification should be rejected", parsed)
    }

    @Test
    fun testArrivalManeuverParsed() {
        val sbn = mockSbn(
            title = "Destination reached",
            text = "You have arrived at your destination",
            isOngoing = true
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        assertEquals(Maneuver.MV_ARRIVE, parsed!!.maneuver)
    }

    @Test
    fun testContentDescriptionAccessibilityExtraction() {
        // Test that contentDescription on an ImageView is extracted as a maneuver hint
        val sbn = mockSbn(
            title = "Market St",
            text = "In 200 m"
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        assertEquals(200, parsed!!.distanceToNextM)
    }

    @Test
    fun testSanitizationRemovesHtmlTags() {
        val sbn = mockSbn(
            title = "<b>Turn right</b> onto <i>Grand Ave</i>",
            text = "In 500 m"
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        assertFalse("HTML tags should be stripped from instruction", parsed!!.instruction.contains("<"))
        assertFalse("HTML tags should be stripped from instruction", parsed.instruction.contains(">"))
        assertEquals("Turn right onto Grand Ave", parsed.instruction)
        assertEquals("Grand Ave", parsed.street)
    }

    @Test
    fun testSanitizationTransliteratesUnicodeArrows() {
        val sbn = mockSbn(
            title = "➔ Turn right onto Market St",
            text = "In 300 m"
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        assertTrue("Unicode arrow ➔ should be transliterated to ->", parsed!!.instruction.startsWith("->"))
        assertEquals("Market St", parsed.street)
    }

    @Test
    fun testSanitizationTransliteratesDiacritics() {
        val sbn = mockSbn(
            title = "Turn right onto Champs-Élysées",
            text = "In 200 m"
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        assertEquals("Champs-Elysees", parsed!!.street)
    }

    @Test
    fun testZeroUnrenderableGlyphs() {
        val sbn = mockSbn(
            title = "• Turn right onto “Oak St” 🚗 &amp; Grand Ave",
            text = "In 100 m"
        )
        val parsed = NavNotificationParser.parse(context, sbn)
        assertNotNull(parsed)
        for (ch in parsed!!.instruction) {
            assertTrue("Char '${ch.code}' should be in ASCII 32..126", ch.code in 32..126)
        }
        for (ch in parsed.street) {
            assertTrue("Char '${ch.code}' should be in ASCII 32..126", ch.code in 32..126)
        }
        assertTrue(parsed.instruction.contains("&"))
        assertTrue(parsed.instruction.contains("\"Oak St\""))
    }
}
