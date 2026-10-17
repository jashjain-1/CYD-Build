package com.dashcue.feature.media

import android.graphics.Bitmap
import android.graphics.Color
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class AlbumColorExtractorTest {

    private fun solid(r: Int, g: Int, b: Int): Bitmap =
        Bitmap.createBitmap(24, 24, Bitmap.Config.ARGB_8888).apply {
            eraseColor(Color.rgb(r, g, b))
        }

    @Test
    fun `solid saturated colour is returned as the accent`() {
        val accent = AlbumColorExtractor.extractDominantRgb(solid(200, 40, 60))
        assertNotNull(accent)
        val r = (accent!! shr 16) and 0xFF
        val g = (accent shr 8) and 0xFF
        val b = accent and 0xFF
        // Dominant hue must be preserved (red is the strongest channel).
        assert(r > g && r > b) { "red hue not preserved: ${accent.toString(16)}" }
    }

    @Test
    fun `dark saturated colour is lifted above the luminance floor`() {
        val accent = AlbumColorExtractor.extractDominantRgb(solid(60, 10, 15))
        assertNotNull(accent)
        val r = (accent!! shr 16) and 0xFF
        val g = (accent shr 8) and 0xFF
        val b = accent and 0xFF
        // The HSV value floor guarantees the brightest channel reaches ~0.62.
        // (Luma is not the right invariant: a pure red is luma 76 by
        // construction, and forcing luma up would desaturate the album hue.)
        val maxChannel = maxOf(r, g, b)
        assert(maxChannel >= 150) { "brightest channel $maxChannel below value floor" }
        assert(r > g && r > b) { "hue not preserved" }
    }

    @Test
    fun `grey image yields null so caller falls back to brand accent`() {
        assertNull(AlbumColorExtractor.extractDominantRgb(solid(128, 128, 128)))
    }

    @Test
    fun `black image yields null`() {
        assertNull(AlbumColorExtractor.extractDominantRgb(solid(0, 0, 0)))
    }

    @Test
    fun `null bitmap yields null`() {
        assertNull(AlbumColorExtractor.extractDominantRgb(null))
    }

    @Test
    fun `lift for dark surface brightens without changing hue order`() {
        // A deeply saturated red is dark in luminance even though R is high.
        val lifted = AlbumColorExtractor.liftForDarkSurface(0x960000) // 150,0,0
        val r = (lifted shr 16) and 0xFF
        val g = (lifted shr 8) and 0xFF
        val b = lifted and 0xFF
        assertEquals("grey leakage must be equal on both lifted channels", g, b)
        assert(r > 150) { "red did not brighten: $r" }
        assert(r > g) { "hue order lost: r=$r g=$g b=$b" }
        assert(g > 0) { "red channel alone cannot reach the contrast bar" }
    }

    @Test
    fun `lift for dark surface is idempotent in direction for white`() {
        // Blending toward white must never darken a channel.
        val lifted = AlbumColorExtractor.liftForDarkSurface(0xFFFFFF)
        assertEquals(0xFFFFFF, lifted)
    }

    @Test
    fun `dominant subject beats dark background`() {
        // 90% near-black background, 10% vivid teal subject. The weighted vote
        // must pick the teal, not the muddy background mean.
        val bmp = Bitmap.createBitmap(32, 32, Bitmap.Config.ARGB_8888)
        for (y in 0 until 32) {
            for (x in 0 until 32) {
                val subject = x in 0..3 && y in 0..3
                bmp.setPixel(x, y, if (subject) Color.rgb(20, 180, 170) else Color.rgb(12, 12, 14))
            }
        }
        val accent = AlbumColorExtractor.extractDominantRgb(bmp)
        assertNotNull(accent)
        val g = (accent!! shr 8) and 0xFF
        val b = accent and 0xFF
        // Green/blue dominant (teal), not the black backdrop.
        assert(g > 80 && b > 80) { "expected teal subject, got ${accent.toString(16)}" }
    }

    @Test
    fun `distinct rgb buckets separate colors with high green and low red vs high red and low green`() {
        val redBmp = solid(200, 40, 30)
        val greenBmp = solid(40, 200, 30)

        val redAccent = AlbumColorExtractor.extractDominantRgb(redBmp)
        val greenAccent = AlbumColorExtractor.extractDominantRgb(greenBmp)

        assertNotNull(redAccent)
        assertNotNull(greenAccent)
        val rFromRed = (redAccent!! shr 16) and 0xFF
        val gFromGreen = (greenAccent!! shr 8) and 0xFF
        assert(rFromRed > 150)
        assert(gFromGreen > 150)
    }
}
