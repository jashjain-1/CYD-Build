package com.dashcue.feature.media

import android.graphics.Bitmap
import android.graphics.Color
import kotlin.math.max
import kotlin.math.min

/**
 * Dominant-accent extraction for album artwork.
 *
 * This mirrors the firmware-side algorithm in
 * `firmware/src/ui/artwork_store.cpp` so the phone card and the HUD tile tint
 * from the same colour. It is a weighted vote, not a plain mean: a plain mean
 * of a cover collapses to a muddy mid-tone because dark background pixels
 * dominate the count. Each pixel votes for a coarse 4x4x4 RGB bucket weighted
 * by how colourful and how bright it is, so a single saturated subject (the
 * album's hero colour) wins. Near-greys and near-blacks barely vote.
 *
 * The winning bucket's mean is then clamped to a minimum luminance so the
 * accent never disappears against a dark cockpit background.
 */
object AlbumColorExtractor {

    private const val MIN_VALUE_FLOOR = 0.62f
    private const val MIN_SAT_FLOOR = 0.45f

    /** Returns a packed 0xRRGGBB accent, or null when the image is empty/grey. */
    fun extractDominantRgb(bitmap: Bitmap?): Int? {
        if (bitmap == null) return null
        val w = bitmap.width
        val h = bitmap.height
        if (w <= 0 || h <= 0) return null

        val pixels = IntArray(w * h)
        bitmap.getPixels(pixels, 0, w, 0, 0, w, h)

        val rSum = LongArray(64)
        val gSum = LongArray(64)
        val bSum = LongArray(64)
        val cnt = LongArray(64)

        for (color in pixels) {
            val r = Color.red(color)
            val g = Color.green(color)
            val b = Color.blue(color)

            val mx = max(r, max(g, b))
            val mn = min(r, min(g, b))
            val sat = if (mx == 0) 0 else ((mx - mn) * 255) / mx

            // Weight = saturation * brightness; grey and black pixels barely vote.
            val weight = (sat.toLong() * mx) / 255
            if (weight == 0L) continue

            val bucket = ((r shr 6) shl 2) or ((g shr 6) shl 1) or (b shr 6)
            rSum[bucket] += r.toLong() * weight
            gSum[bucket] += g.toLong() * weight
            bSum[bucket] += b.toLong() * weight
            cnt[bucket] += weight
        }

        var best = -1
        var bestWeight = 0L
        for (i in 0 until 64) {
            if (cnt[i] > bestWeight) {
                bestWeight = cnt[i]
                best = i
            }
        }
        if (best < 0 || cnt[best] == 0L) return null

        val r = (rSum[best] / cnt[best]).toInt()
        val g = (gSum[best] / cnt[best]).toInt()
        val b = (bSum[best] / cnt[best]).toInt()

        // Floor the brightness via HSV so the hue is preserved exactly and the
        // value floor is guaranteed. A naive linear scale clamps the dominant
        // channel at 255 first and leaves the others lagging, so a deep red
        // would end up darker than intended.
        val hsv = FloatArray(3)
        Color.colorToHSV(Color.rgb(r, g, b), hsv)
        if (hsv[2] < MIN_VALUE_FLOOR) hsv[2] = MIN_VALUE_FLOOR
        // Keep some richness: a slight saturation floor stops the lift from
        // washing the accent toward white on very dark art.
        if (hsv[1] < MIN_SAT_FLOOR) hsv[1] = MIN_SAT_FLOOR
        val lifted = Color.HSVToColor(hsv)

        return (Color.red(lifted) shl 16) or (Color.green(lifted) shl 8) or Color.blue(lifted)
    }

    /**
     * Blend an accent 35% toward white for use as small UI on a dark surface.
     *
     * A saturated hue can be very dark in *luminance* even when its brightest
     * channel is high — pure red tops out near L=0.21 — so the raw accent is not
     * safe for 12 px text or a 3:1 graphic against near-black. The firmware does
     * the identical blend in `ui_screen_media.cpp::applyAccent()` so the phone
     * card and the HUD tile keep matching.
     */
    fun liftForDarkSurface(rgb: Int): Int {
        val r = ((rgb shr 16 and 0xFF) * 65 + 255 * 35) / 100
        val g = ((rgb shr 8 and 0xFF) * 65 + 255 * 35) / 100
        val b = ((rgb and 0xFF) * 65 + 255 * 35) / 100
        return (r shl 16) or (g shl 8) or b
    }
}
