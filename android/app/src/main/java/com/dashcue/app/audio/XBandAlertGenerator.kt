package com.dashcue.app.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import com.dashcue.core.common.DashCueLog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Real-time synthetic PCM X-Band / Laser radar alert generator.
 *
 * Synthesizes an authentic 1,150 Hz to 2,300 Hz pulsed linear frequency sweep
 * (4 pulses of 50 ms separated by 3 gaps of 30 ms = 290 ms total duration)
 * with 20% 2nd harmonic injection and anti-click Tukey boundary windowing.
 */
object XBandAlertGenerator {
    private const val TAG = "XBandAlertGenerator"

    const val SAMPLE_RATE = 44100
    const val F0 = 1150.0 // Start frequency in Hz
    const val F1 = 2300.0 // End frequency in Hz
    const val PULSE_DURATION_S = 0.050 // 50 ms
    const val GAP_DURATION_S = 0.030 // 30 ms
    const val NUM_PULSES = 4
    const val PEAK_AMPLITUDE = 28000.0 // ~1.3 dB digital headroom below 32767
    const val COOLDOWN_MS = 20_000L

    val NUM_PULSE_SAMPLES = (SAMPLE_RATE * PULSE_DURATION_S).roundToInt() // 2,205
    val NUM_GAP_SAMPLES = (SAMPLE_RATE * GAP_DURATION_S).roundToInt() // 1,323
    val TOTAL_SAMPLES = NUM_PULSES * NUM_PULSE_SAMPLES + (NUM_PULSES - 1) * NUM_GAP_SAMPLES // 12,789
    val TOTAL_BYTES = TOTAL_SAMPLES * 2 // 25,578 bytes

    /** Pre-computed static PCM buffer for low latency playback. */
    val pcmData: ByteArray by lazy {
        generatePcmWaveform()
    }

    private var lastAlertTimeMs: Long = 0L
    private var activeTrack: AudioTrack? = null
    private var releaseJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.Default)

    /**
     * Synthesizes the authentic 4-pulse radar alert PCM byte buffer.
     */
    fun generatePcmWaveform(): ByteArray {
        val buffer = ByteArray(TOTAL_BYTES)
        var byteOffset = 0

        val beta = (F1 - F0) / PULSE_DURATION_S // 23,000 Hz/s chirp rate
        val rampSamples = 110 // ~2.5 ms Tukey taper

        for (pulse in 0 until NUM_PULSES) {
            // Write pulse samples
            for (n in 0 until NUM_PULSE_SAMPLES) {
                val t = n.toDouble() / SAMPLE_RATE
                // Instantaneous phase phi(t) = 2*pi * (f0*t + 0.5*beta*t^2)
                val phi = 2.0 * Math.PI * (F0 * t + 0.5 * beta * t * t)

                // 20% second-harmonic injection
                val wRaw = 0.80 * sin(phi) + 0.20 * sin(2.0 * phi)

                // Anti-click Tukey window
                val window = when {
                    n < rampSamples -> 0.5 * (1.0 - cos(Math.PI * n / rampSamples))
                    n > NUM_PULSE_SAMPLES - 1 - rampSamples -> {
                        val decayN = NUM_PULSE_SAMPLES - 1 - n
                        0.5 * (1.0 - cos(Math.PI * decayN / rampSamples))
                    }
                    else -> 1.0
                }

                val sampleVal = (wRaw * window * PEAK_AMPLITUDE).roundToInt().coerceIn(-32768, 32767).toShort()

                // Little-endian 16-bit PCM
                buffer[byteOffset++] = (sampleVal.toInt() and 0xFF).toByte()
                buffer[byteOffset++] = ((sampleVal.toInt() shr 8) and 0xFF).toByte()
            }

            // Write silence gap between pulses (if not after the last pulse)
            if (pulse < NUM_PULSES - 1) {
                for (g in 0 until NUM_GAP_SAMPLES) {
                    buffer[byteOffset++] = 0
                    buffer[byteOffset++] = 0
                }
            }
        }

        return buffer
    }

    private fun safeLog(level: Int, msg: String, tr: Throwable? = null) {
        try {
            when (level) {
                2 -> DashCueLog.d(TAG, msg)
                4 -> DashCueLog.i(TAG, msg)
                5 -> DashCueLog.w(TAG, msg, tr)
            }
        } catch (_: Throwable) {}
    }

    /**
     * Triggers the synthetic X-band laser radar alert over the specified audio route.
     *
     * @param context Application context
     * @param route Target AudioRoute ID (0=Phone, 1=Bluetooth, 2=Onboard)
     * @param isTest If true, bypasses the 20-second cooldown timer
     * @return true if alert was played, false if suppressed by cooldown or error
     */
    @Synchronized
    fun playAlert(context: Context, route: Int, isTest: Boolean = false): Boolean {
        val now = System.currentTimeMillis()
        if (!isTest) {
            if (now - lastAlertTimeMs < COOLDOWN_MS) {
                safeLog(2, "X-band alert suppressed by cooldown (${now - lastAlertTimeMs} ms < $COOLDOWN_MS ms)")
                return false
            }
        }
        lastAlertTimeMs = now

        try {
            // Stop and release any previous track
            releaseActiveTrack()

            val audioAttributes = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build()

            val audioFormat = AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                .build()

            val track = AudioTrack.Builder()
                .setAudioAttributes(audioAttributes)
                .setAudioFormat(audioFormat)
                .setBufferSizeInBytes(pcmData.size)
                .setTransferMode(AudioTrack.MODE_STATIC)
                .build()

            track.write(pcmData, 0, pcmData.size)
            AudioRouter.routeAudioTrack(context, track, route)
            track.play()

            activeTrack = track

            // Auto-release track after playback duration (~290 ms + buffer)
            releaseJob = scope.launch {
                delay(500)
                synchronized(this@XBandAlertGenerator) {
                    if (activeTrack == track) {
                        releaseActiveTrack()
                    }
                }
            }
            safeLog(4, "X-band radar alert played successfully (route=$route, isTest=$isTest)")
            return true
        } catch (e: Exception) {
            safeLog(5, "Error playing X-band radar alert", e)
            releaseActiveTrack()
            return false
        }
    }

    @Synchronized
    private fun releaseActiveTrack() {
        releaseJob?.cancel()
        releaseJob = null
        try {
            activeTrack?.stop()
        } catch (_: Exception) {}
        try {
            activeTrack?.release()
        } catch (_: Exception) {}
        activeTrack = null
    }
}
