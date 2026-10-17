package com.dashcue.feature.media

import android.graphics.Bitmap
import com.dashcue.core.protocol.DashCueFraming
import com.dashcue.v1.ArtworkBegin
import com.dashcue.v1.ArtworkChunk
import com.dashcue.v1.ArtworkEnd
import com.google.protobuf.ByteString
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Downscales and converts album artwork to 64x64 RGB565 little-endian byte array
 * and streams over the bulk lane (§10.2).
 */
object Rgb565ArtworkConverter {
    // 64x64 RGB565 (spec §1.1) = 8,192 B streamed as 8 x 1 KB chunks over the
    // bulk lane. Going larger trades BLE airtime for no visible gain: the HUD
    // hero tile is 84 px, so the 1.31x upscale plus bilinear antialiasing is
    // what keeps the art crisp at size rather than blocky.
    const val TARGET_WIDTH = 64
    const val TARGET_HEIGHT = 64
    const val TOTAL_PIXELS = TARGET_WIDTH * TARGET_HEIGHT // 4096
    const val TOTAL_BYTES = TOTAL_PIXELS * 2 // 8192
    const val CHUNK_SIZE = 1024 // 8 chunks total

    data class PreparedArtwork(
        val artworkId: Long,
        val rgb565Bytes: ByteArray,
        val totalCrc32: Long,
        val beginMsg: ArtworkBegin,
        val chunks: List<ArtworkChunk>,
        val endMsg: ArtworkEnd
    )

    fun convertBitmapToRgb565(
        bitmap: Bitmap,
        artworkId: Long,
        targetWidth: Int = TARGET_WIDTH,
        targetHeight: Int = TARGET_HEIGHT
    ): PreparedArtwork {
        val totalPixels = targetWidth * targetHeight
        val totalBytes = totalPixels * 2
        val pixels = downscaleToPixels(bitmap, targetWidth, targetHeight)

        val byteBuffer = ByteBuffer.allocate(totalBytes).order(ByteOrder.LITTLE_ENDIAN)
        for (color in pixels) {
            val r = (color shr 16 and 0xFF) ushr 3 // 5 bits (0..31)
            val g = (color shr 8 and 0xFF) ushr 2  // 6 bits (0..63)
            val b = (color and 0xFF) ushr 3        // 5 bits (0..31)
            val rgb565 = ((r shl 11) or (g shl 5) or b) and 0xFFFF
            byteBuffer.putShort(rgb565.toShort())
        }

        val rawBytes = byteBuffer.array()
        val crc32 = DashCueFraming.calculateCrc32(rawBytes)

        val beginMsg = ArtworkBegin.newBuilder()
            .setArtworkId(artworkId.toInt())
            .setWidth(targetWidth)
            .setHeight(targetHeight)
            .setFormat(0) // 0 = RGB565 little-endian
            .setTotalBytes(totalBytes)
            .setTotalCrc32(crc32.toInt())
            .build()

        val numChunks = (totalBytes + CHUNK_SIZE - 1) / CHUNK_SIZE
        val chunks = ArrayList<ArtworkChunk>(numChunks)

        for (i in 0 until numChunks) {
            val offset = i * CHUNK_SIZE
            val len = minOf(CHUNK_SIZE, totalBytes - offset)
            val chunkBytes = ByteArray(len)
            System.arraycopy(rawBytes, offset, chunkBytes, 0, len)

            chunks.add(
                ArtworkChunk.newBuilder()
                    .setArtworkId(artworkId.toInt())
                    .setChunkIndex(i)
                    .setOffset(offset)
                    .setData(ByteString.copyFrom(chunkBytes))
                    .build()
            )
        }

        val endMsg = ArtworkEnd.newBuilder()
            .setArtworkId(artworkId.toInt())
            .setReceivedCrc32(crc32.toInt())
            .setOk(true)
            .build()

        return PreparedArtwork(artworkId, rawBytes, crc32, beginMsg, chunks, endMsg)
    }

    /**
     * Area-average ("box") downscale to [dstW] x [dstH] packed ARGB pixels.
     *
     * `Bitmap.createScaledBitmap(..., filter = true)` is bilinear: at the ~8-10x
     * reduction a 512 px cover needs for a 64 px tile it samples only a few of
     * the source pixels, so fine detail aliases into the noisy, "choppy" look
     * the HUD showed. Averaging every source pixel that maps to an output pixel
     * is the correct low-pass filter for this ratio: high-frequency detail is
     * integrated away instead of being sampled at random, and the 565 quantise
     * step afterwards has a smooth image to work with.
     *
     * Upscaling (source smaller than target) still uses the platform scaler.
     */
    internal fun downscaleToPixels(src: Bitmap, dstW: Int, dstH: Int): IntArray {
        if (src.width == dstW && src.height == dstH) {
            return IntArray(dstW * dstH).also { src.getPixels(it, 0, dstW, 0, 0, dstW, dstH) }
        }
        if (src.width < dstW || src.height < dstH) {
            val scaled = Bitmap.createScaledBitmap(src, dstW, dstH, true)
            return IntArray(dstW * dstH).also {
                scaled.getPixels(it, 0, dstW, 0, 0, dstW, dstH)
                if (scaled !== src) scaled.recycle()
            }
        }

        val working: Bitmap
        val mustRecycle: Boolean
        if (src.width > 512 || src.height > 512) {
            val maxDim = maxOf(src.width, src.height)
            val scale = 512f / maxDim
            val preW = maxOf(dstW, (src.width * scale).toInt())
            val preH = maxOf(dstH, (src.height * scale).toInt())
            val scaled = Bitmap.createScaledBitmap(src, preW, preH, true)
            working = scaled
            mustRecycle = (scaled !== src)
        } else {
            working = src
            mustRecycle = false
        }

        val sw = working.width
        val sh = working.height
        val srcPixels = IntArray(sw * sh)
        working.getPixels(srcPixels, 0, sw, 0, 0, sw, sh)
        if (mustRecycle) {
            working.recycle()
        }

        val out = IntArray(dstW * dstH)
        for (dy in 0 until dstH) {
            // Half-open source band, always at least one row wide so a target
            // row is never left unsampled when the ratio is not integral.
            val y0 = dy * sh / dstH
            val y1 = maxOf(y0 + 1, (dy + 1) * sh / dstH)
            for (dx in 0 until dstW) {
                val x0 = dx * sw / dstW
                val x1 = maxOf(x0 + 1, (dx + 1) * sw / dstW)
                var r = 0L
                var g = 0L
                var b = 0L
                var n = 0
                for (y in y0 until y1) {
                    val row = y * sw
                    for (x in x0 until x1) {
                        val c = srcPixels[row + x]
                        r += (c shr 16) and 0xFF
                        g += (c shr 8) and 0xFF
                        b += c and 0xFF
                        n++
                    }
                }
                out[dy * dstW + dx] =
                    (0xFF shl 24) or
                        (((r / n).toInt()) shl 16) or
                        (((g / n).toInt()) shl 8) or
                        ((b / n).toInt())
            }
        }
        return out
    }
}
