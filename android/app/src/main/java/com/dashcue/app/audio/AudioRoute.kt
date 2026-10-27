package com.dashcue.app.audio

/**
 * Output routes for DashCue's own alert chirps.
 *
 * Android does not let a normal app redirect another player's media (Spotify,
 * YouTube Music, ...). Those apps follow the system's active output and ignore
 * per-app requests, so this control deliberately covers only the synthetic
 * X-band alert AudioTrack that DashCue generates itself. The settings screen
 * states that boundary instead of promising to move the driver's music.
 */
enum class AudioRoute(val id: Int, val label: String, val description: String) {
    PHONE_SPEAKER(
        id = 0,
        label = "Phone speaker",
        description = "Alerts play through the phone's built-in speaker."
    ),
    BLUETOOTH_VEHICLE(
        id = 1,
        label = "Car Bluetooth",
        description = "Alerts use the connected Bluetooth output; falls back to the phone speaker."
    );

    companion object {
        /**
         * Older builds could persist id 2 ("Onboard Alert"), which had no
         * distinct behaviour. Treat anything unknown as the safe Bluetooth
         * route so an upgrade never silences the alert.
         */
        fun fromId(id: Int): AudioRoute = entries.firstOrNull { it.id == id } ?: BLUETOOTH_VEHICLE
    }
}
