package com.dashcue.app

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.SystemClock
import kotlin.math.abs

/**
 * Phone compass used for the HUD standby rose.
 *
 * GPS bearing is the right source while moving, but it only exists when the
 * receiver reports a course over ground; standing still it is absent, and the
 * old fallback sent 0.0 which the HUD drew as a confident north arrow. A
 * rotation-vector sensor fills that gap. The bridge still prefers the GPS course
 * whenever it is valid, because the sensor describes the phone's mounting, not
 * necessarily the direction of travel.
 */
object HeadingProvider : SensorEventListener {
    private const val TAG = "HeadingProvider"
    private const val STALE_AFTER_MS = 3_000L

    private var sensorManager: SensorManager? = null
    private var hasSensor = false
    private var azimuth: Float? = null
    private var lastEventElapsedMs = 0L

    fun start(context: Context) {
        if (sensorManager != null) return
        val manager = context.getSystemService(Context.SENSOR_SERVICE) as? SensorManager ?: return
        val sensor = manager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
            ?: manager.getDefaultSensor(Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR)
            ?: manager.getDefaultSensor(Sensor.TYPE_ORIENTATION)
        if (sensor == null) {
            com.dashcue.core.common.DashCueLog.w(TAG, "No heading sensor on this device")
            return
        }
        try {
            manager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_UI)
            sensorManager = manager
            hasSensor = true
            com.dashcue.core.common.DashCueLog.i(TAG, "Heading sensor active: ${sensor.name}")
        } catch (e: Exception) {
            com.dashcue.core.common.DashCueLog.w(TAG, "Heading sensor registration failed", e)
        }
    }

    /** Degrees clockwise from magnetic north in [0,360), or null when unknown. */
    fun currentAzimuth(): Float? {
        if (!hasSensor) return null
        val value = azimuth ?: return null
        if (SystemClock.elapsedRealtime() - lastEventElapsedMs > STALE_AFTER_MS) return null
        return value
    }

    override fun onSensorChanged(event: SensorEvent?) {
        val values = event?.values ?: return
        val degrees = try {
            when (event.sensor.type) {
                Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR -> {
                    val rotation = FloatArray(9)
                    SensorManager.getRotationMatrixFromVector(rotation, values)
                    val orientation = FloatArray(3)
                    SensorManager.getOrientation(rotation, orientation)
                    Math.toDegrees(orientation[0].toDouble()).toFloat()
                }
                Sensor.TYPE_ORIENTATION -> values[0]
                else -> return
            }
        } catch (e: Exception) {
            return
        }
        if (degrees.isNaN() || degrees.isInfinite()) return

        val normalized = ((degrees % 360f) + 360f) % 360f
        val previous = azimuth
        azimuth = if (previous == null) {
            normalized
        } else {
            // Circular low-pass: smooth across the 359->0 boundary without
            // turning a one-degree step into a 359-degree spin.
            var delta = normalized - previous
            if (delta > 180f) delta -= 360f
            if (delta < -180f) delta += 360f
            if (abs(delta) > 45f) normalized else ((previous + 0.25f * delta) + 360f) % 360f
        }
        lastEventElapsedMs = SystemClock.elapsedRealtime()
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
