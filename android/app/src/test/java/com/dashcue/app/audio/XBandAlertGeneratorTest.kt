package com.dashcue.app.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class XBandAlertGeneratorTest {

    @Test
    fun testPcmWaveformTotalSamplesAndBytes() {
        assertEquals(44100, XBandAlertGenerator.SAMPLE_RATE)
        assertEquals(4, XBandAlertGenerator.NUM_PULSES)
        assertEquals(2205, XBandAlertGenerator.NUM_PULSE_SAMPLES)
        assertEquals(1323, XBandAlertGenerator.NUM_GAP_SAMPLES)
        assertEquals(12789, XBandAlertGenerator.TOTAL_SAMPLES)
        assertEquals(25578, XBandAlertGenerator.TOTAL_BYTES)

        val pcm = XBandAlertGenerator.pcmData
        assertEquals(25578, pcm.size)
    }

    @Test
    fun testPcmWaveformDurationAndHeadroom() {
        val durationS = XBandAlertGenerator.TOTAL_SAMPLES.toDouble() / XBandAlertGenerator.SAMPLE_RATE
        assertEquals(0.290, durationS, 0.001)

        val pcm = XBandAlertGenerator.pcmData
        var maxAbsSample = 0
        var clippedCount = 0

        for (i in 0 until pcm.size step 2) {
            val low = pcm[i].toInt() and 0xFF
            val high = pcm[i + 1].toInt()
            val sample = (high shl 8) or low

            if (sample == 32767 || sample == -32768) {
                clippedCount++
            }
            val absVal = abs(sample)
            if (absVal > maxAbsSample) {
                maxAbsSample = absVal
            }
        }

        // Must have zero clipping
        assertEquals("Clipping detected in synthetic waveform", 0, clippedCount)

        // Must reach high output level without exceeding headroom
        assertTrue("Peak sample should exceed 24000 for driver intelligibility, got $maxAbsSample", maxAbsSample >= 24000)
        assertTrue("Peak sample should not exceed 28500 headroom, got $maxAbsSample", maxAbsSample <= 28500)
    }

    @Test
    fun testPcmWaveformPulsesAndGaps() {
        val pcm = XBandAlertGenerator.pcmData

        fun getSample(sampleIndex: Int): Short {
            val byteOffset = sampleIndex * 2
            val low = pcm[byteOffset].toInt() and 0xFF
            val high = pcm[byteOffset + 1].toInt()
            return ((high shl 8) or low).toShort()
        }

        // Pulse 1: 0 .. 2204
        var pulse1Energy = 0L
        for (i in 0 until 2205) {
            pulse1Energy += abs(getSample(i).toLong())
        }
        assertTrue("Pulse 1 must contain audible energy", pulse1Energy > 100000)

        // Gap 1: 2205 .. 3527 must be complete silence (zero)
        for (i in 2205 until 3528) {
            assertEquals("Gap 1 sample $i must be zero", 0, getSample(i).toInt())
        }

        // Pulse 2: 3528 .. 5732
        var pulse2Energy = 0L
        for (i in 3528 until 5733) {
            pulse2Energy += abs(getSample(i).toLong())
        }
        assertTrue("Pulse 2 must contain audible energy", pulse2Energy > 100000)

        // Gap 2: 5733 .. 7055 must be complete silence
        for (i in 5733 until 7056) {
            assertEquals("Gap 2 sample $i must be zero", 0, getSample(i).toInt())
        }

        // Pulse 3: 7056 .. 9260
        var pulse3Energy = 0L
        for (i in 7056 until 9261) {
            pulse3Energy += abs(getSample(i).toLong())
        }
        assertTrue("Pulse 3 must contain audible energy", pulse3Energy > 100000)

        // Gap 3: 9261 .. 10583 must be complete silence
        for (i in 9261 until 10584) {
            assertEquals("Gap 3 sample $i must be zero", 0, getSample(i).toInt())
        }

        // Pulse 4: 10584 .. 12788
        var pulse4Energy = 0L
        for (i in 10584 until 12789) {
            pulse4Energy += abs(getSample(i).toLong())
        }
        assertTrue("Pulse 4 must contain audible energy", pulse4Energy > 100000)
    }

    @Test
    fun testCooldownDebounceConstant() {
        assertEquals(20000L, XBandAlertGenerator.COOLDOWN_MS)
    }
}
