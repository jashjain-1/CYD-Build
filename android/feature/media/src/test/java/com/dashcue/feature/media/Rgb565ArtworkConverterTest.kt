package com.dashcue.feature.media

import android.graphics.Bitmap
import android.graphics.Color
import com.dashcue.core.protocol.DashCueFraming
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class Rgb565ArtworkConverterTest {

    private fun bitmap(w: Int, h: Int, color: (Int, Int) -> Int): Bitmap {
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        for (y in 0 until h) {
            for (x in 0 until w) bmp.setPixel(x, y, color(x, y))
        }
        return bmp
    }

    @Test
    fun `prepared artwork is 64x64 rgb565 with eight 1KB chunks`() {
        val prepared = Rgb565ArtworkConverter.convertBitmapToRgb565(
            bitmap(300, 300) { _, _ -> Color.rgb(40, 160, 220) },
            artworkId = 42L
        )

        assertEquals(8192, prepared.rgb565Bytes.size)
        assertEquals(8192, prepared.beginMsg.totalBytes)
        assertEquals(64, prepared.beginMsg.width)
        assertEquals(64, prepared.beginMsg.height)
        assertEquals(8, prepared.chunks.size)

        // Chunks must tile the buffer exactly, in order, with no gap or overlap.
        var expectedOffset = 0
        prepared.chunks.forEachIndexed { index, chunk ->
            assertEquals(index, chunk.chunkIndex)
            assertEquals(expectedOffset, chunk.offset)
            expectedOffset += chunk.data.size()
        }
        assertEquals(8192, expectedOffset)

        // The declared CRC must be the CRC of the payload actually streamed.
        assertEquals(
            prepared.totalCrc32,
            DashCueFraming.calculateCrc32(prepared.rgb565Bytes)
        )
        assertEquals(prepared.totalCrc32, prepared.endMsg.receivedCrc32.toLong() and 0xFFFFFFFFL)
    }

    @Test
    fun `solid colour survives the downscale unchanged`() {
        val pixels = Rgb565ArtworkConverter.downscaleToPixels(
            bitmap(256, 256) { _, _ -> Color.rgb(200, 60, 30) },
            Rgb565ArtworkConverter.TARGET_WIDTH,
            Rgb565ArtworkConverter.TARGET_HEIGHT
        )
        assertEquals(Rgb565ArtworkConverter.TOTAL_PIXELS, pixels.size)
        pixels.forEach { c ->
            assertEquals(200, (c shr 16) and 0xFF)
            assertEquals(60, (c shr 8) and 0xFF)
            assertEquals(30, c and 0xFF)
        }
    }

    @Test
    fun `one-pixel checkerboard averages to mid grey instead of aliasing`() {
        // Bilinear sampling of a 1 px checkerboard keeps landing on pure black
        // or pure white, which is exactly the choppy look being fixed. A box
        // filter integrates each 2x2 block to ~127.
        val pixels = Rgb565ArtworkConverter.downscaleToPixels(
            bitmap(128, 128) { x, y -> if ((x + y) % 2 == 0) Color.BLACK else Color.WHITE },
            Rgb565ArtworkConverter.TARGET_WIDTH,
            Rgb565ArtworkConverter.TARGET_HEIGHT
        )
        pixels.forEach { c ->
            val r = (c shr 16) and 0xFF
            val g = (c shr 8) and 0xFF
            val b = c and 0xFF
            assertTrue("channel aliased to $r/$g/$b", r in 115..140 && g in 115..140 && b in 115..140)
        }
    }

    @Test
    fun `non-integer downscale covers every source row`() {
        // 100 -> 64 is a 1.5625 ratio; the banding must not skip rows or leave
        // an output pixel unsampled. A vertical ramp then comes out monotonic.
        val pixels = Rgb565ArtworkConverter.downscaleToPixels(
            bitmap(100, 100) { _, y -> Color.rgb(y * 255 / 99, y * 255 / 99, y * 255 / 99) },
            Rgb565ArtworkConverter.TARGET_WIDTH,
            Rgb565ArtworkConverter.TARGET_HEIGHT
        )
        var previous = -1
        for (y in 0 until Rgb565ArtworkConverter.TARGET_HEIGHT) {
            val v = (pixels[y * Rgb565ArtworkConverter.TARGET_WIDTH] shr 16) and 0xFF
            assertTrue("ramp went backwards at row $y ($previous -> $v)", v >= previous)
            previous = v
        }
        assertTrue("ramp did not reach the bottom", previous > 240)
    }

    @Test
    fun `small source is upscaled rather than rejected`() {
        val pixels = Rgb565ArtworkConverter.downscaleToPixels(
            bitmap(16, 16) { _, _ -> Color.rgb(10, 20, 30) },
            Rgb565ArtworkConverter.TARGET_WIDTH,
            Rgb565ArtworkConverter.TARGET_HEIGHT
        )
        assertEquals(Rgb565ArtworkConverter.TOTAL_PIXELS, pixels.size)
        assertEquals(10, (pixels[0] shr 16) and 0xFF)
    }
}
