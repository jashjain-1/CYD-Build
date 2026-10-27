package com.dashcue.app.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AudioRouteTest {

    @Test
    fun testAudioRouteMapping() {
        assertEquals(AudioRoute.PHONE_SPEAKER, AudioRoute.fromId(0))
        assertEquals(AudioRoute.BLUETOOTH_VEHICLE, AudioRoute.fromId(1))

        // Retired "Onboard Alert" id and any out-of-range value must never
        // silence the alert; they fall back to the Bluetooth route.
        assertEquals(AudioRoute.BLUETOOTH_VEHICLE, AudioRoute.fromId(2))
        assertEquals(AudioRoute.BLUETOOTH_VEHICLE, AudioRoute.fromId(-1))
        assertEquals(AudioRoute.BLUETOOTH_VEHICLE, AudioRoute.fromId(99))
    }

    @Test
    fun testAudioRouteLabelsAndDescriptions() {
        assertEquals("Phone speaker", AudioRoute.PHONE_SPEAKER.label)
        assertEquals("Car Bluetooth", AudioRoute.BLUETOOTH_VEHICLE.label)

        assertTrue(AudioRoute.PHONE_SPEAKER.description.isNotBlank())
        assertTrue(AudioRoute.BLUETOOTH_VEHICLE.description.isNotBlank())
        // The description must not claim control over other apps' media.
        assertTrue(AudioRoute.BLUETOOTH_VEHICLE.description.startsWith("Alerts"))
    }
}
