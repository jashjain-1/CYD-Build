package com.dashcue.core.database

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.map
import java.io.IOException

val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "dashcue_settings")

data class UserSettings(
    val brightnessPct: Int = 100,
    val themeMode: Int = 0, // 0 = Auto, 1 = Dark, 2 = Light
    val orientation: Int = 0, // 0 = Landscape, 1 = Landscape 180
    val leftHandTraffic: Boolean = false,
    val seekStepS: Int = 10, // 10 or 30 only
    val units: Int = 0, // 0 = Metric, 1 = Imperial
    val osrmBaseUrl: String = "",
    val nominatimBaseUrl: String = "",
    val tripHistoryEnabled: Boolean = false, // Default OFF for privacy
    val drivingLockEnabled: Boolean = true,
    val pairedHudAddress: String = "",
    val pairedHudName: String = "",

    // R1: Alert routing and radar alert
    val audioRoute: Int = 1, // 0 = Phone speaker, 1 = Car Bluetooth (DashCue alerts only)
    val xbandAlertEnabled: Boolean = true
)

class UserPreferencesRepository(private val context: Context) {

    private object PreferencesKeys {
        val BRIGHTNESS_PCT = intPreferencesKey("brightness_pct")
        val THEME_MODE = intPreferencesKey("theme_mode")
        val ORIENTATION = intPreferencesKey("orientation")
        val LEFT_HAND_TRAFFIC = booleanPreferencesKey("left_hand_traffic")
        val SEEK_STEP_S = intPreferencesKey("seek_step_s")
        val UNITS = intPreferencesKey("units")
        val OSRM_BASE_URL = stringPreferencesKey("osrm_base_url")
        val NOMINATIM_BASE_URL = stringPreferencesKey("nominatim_base_url")
        val TRIP_HISTORY_ENABLED = booleanPreferencesKey("trip_history_enabled")
        val DRIVING_LOCK_ENABLED = booleanPreferencesKey("driving_lock_enabled")
        val PAIRED_HUD_ADDRESS = stringPreferencesKey("paired_hud_address")
        val PAIRED_HUD_NAME = stringPreferencesKey("paired_hud_name")

        // R1: Audio & Alert
        val AUDIO_ROUTE = intPreferencesKey("audio_route")
        val XBAND_ALERT_ENABLED = booleanPreferencesKey("xband_alert_enabled")

    }

    val userSettingsFlow: Flow<UserSettings> = context.dataStore.data
        .catch { exception ->
            if (exception is IOException) {
                emit(emptyPreferences())
            } else {
                throw exception
            }
        }
        .map { preferences ->
            UserSettings(
                brightnessPct = preferences[PreferencesKeys.BRIGHTNESS_PCT] ?: 100,
                themeMode = preferences[PreferencesKeys.THEME_MODE] ?: 0,
                orientation = preferences[PreferencesKeys.ORIENTATION] ?: 0,
                leftHandTraffic = preferences[PreferencesKeys.LEFT_HAND_TRAFFIC] ?: false,
                seekStepS = preferences[PreferencesKeys.SEEK_STEP_S] ?: 10,
                units = preferences[PreferencesKeys.UNITS] ?: 0,
                osrmBaseUrl = preferences[PreferencesKeys.OSRM_BASE_URL] ?: "",
                nominatimBaseUrl = preferences[PreferencesKeys.NOMINATIM_BASE_URL] ?: "",
                tripHistoryEnabled = preferences[PreferencesKeys.TRIP_HISTORY_ENABLED] ?: false,
                drivingLockEnabled = preferences[PreferencesKeys.DRIVING_LOCK_ENABLED] ?: true,
                pairedHudAddress = preferences[PreferencesKeys.PAIRED_HUD_ADDRESS] ?: "",
                pairedHudName = preferences[PreferencesKeys.PAIRED_HUD_NAME] ?: "",
                audioRoute = preferences[PreferencesKeys.AUDIO_ROUTE] ?: 1,
                xbandAlertEnabled = preferences[PreferencesKeys.XBAND_ALERT_ENABLED] ?: true
            )
        }

    suspend fun updateBrightness(brightness: Int) {
        val clamped = brightness.coerceIn(10, 100)
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.BRIGHTNESS_PCT] = clamped
        }
    }

    suspend fun updateTheme(themeMode: Int) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.THEME_MODE] = themeMode
        }
    }

    suspend fun updateOrientation(orientation: Int) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.ORIENTATION] = orientation
        }
    }

    suspend fun updateLeftHandTraffic(leftHandTraffic: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.LEFT_HAND_TRAFFIC] = leftHandTraffic
        }
    }

    suspend fun updateSeekStep(seekStepS: Int) {
        val validStep = if (seekStepS == 30) 30 else 10
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.SEEK_STEP_S] = validStep
        }
    }

    suspend fun updateUnits(units: Int) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.UNITS] = units
        }
    }

    suspend fun updateOsrmBaseUrl(url: String) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.OSRM_BASE_URL] = url.trim()
        }
    }

    suspend fun updateNominatimBaseUrl(url: String) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.NOMINATIM_BASE_URL] = url.trim()
        }
    }

    suspend fun updateTripHistoryEnabled(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.TRIP_HISTORY_ENABLED] = enabled
        }
    }

    suspend fun updateDrivingLockEnabled(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.DRIVING_LOCK_ENABLED] = enabled
        }
    }

    suspend fun updatePairedDevice(address: String, name: String) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.PAIRED_HUD_ADDRESS] = address
            preferences[PreferencesKeys.PAIRED_HUD_NAME] = name
        }
    }

    suspend fun forgetPairedDevice() {
        context.dataStore.edit { preferences ->
            preferences.remove(PreferencesKeys.PAIRED_HUD_ADDRESS)
            preferences.remove(PreferencesKeys.PAIRED_HUD_NAME)
        }
    }

    suspend fun updateAudioRoute(route: Int) {
        // 2 was the retired "Onboard Alert" duplicate; keep only routes that
        // map to a real output device.
        val validRoute = if (route == 0) 0 else 1
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.AUDIO_ROUTE] = validRoute
        }
    }

    suspend fun updateXBandAlertEnabled(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[PreferencesKeys.XBAND_ALERT_ENABLED] = enabled
        }
    }
}
