package com.dashcue.core.database

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.map
import java.io.IOException

private val Context.dashcueDataStore: DataStore<Preferences> by preferencesDataStore(name = "dashcue_settings")

data class UserPreferences(
    val brightnessPct: Int = 80,
    val theme: Int = 0, // 0=AUTO, 1=DARK, 2=LIGHT
    val orientation: Int = 0, // 0=LANDSCAPE, 1=LANDSCAPE_180
    val units: Int = 0, // 0=METRIC, 1=IMPERIAL
    val seekStepSeconds: Int = 10, // 10 or 30
    val osrmEndpoint: String = "",
    val nominatimEndpoint: String = "",
    val tripHistoryEnabled: Boolean = false // Privacy-by-Default (Rule 6 / DEC-20)
)

class DashCuePreferencesRepository(private val context: Context) {
    private object Keys {
        val BRIGHTNESS_PCT = intPreferencesKey("brightness_pct")
        val THEME = intPreferencesKey("theme")
        val ORIENTATION = intPreferencesKey("orientation")
        val UNITS = intPreferencesKey("units")
        val SEEK_STEP_S = intPreferencesKey("seek_step_s")
        val OSRM_ENDPOINT = stringPreferencesKey("osrm_endpoint")
        val NOMINATIM_ENDPOINT = stringPreferencesKey("nominatim_endpoint")
        val TRIP_HISTORY_ENABLED = booleanPreferencesKey("trip_history_enabled")
    }

    val userPreferencesFlow: Flow<UserPreferences> = context.dashcueDataStore.data
        .catch { exception ->
            if (exception is IOException) {
                emit(emptyPreferences())
            } else {
                throw exception
            }
        }
        .map { preferences ->
            UserPreferences(
                brightnessPct = preferences[Keys.BRIGHTNESS_PCT] ?: 80,
                theme = preferences[Keys.THEME] ?: 0,
                orientation = preferences[Keys.ORIENTATION] ?: 0,
                units = preferences[Keys.UNITS] ?: 0,
                seekStepSeconds = preferences[Keys.SEEK_STEP_S] ?: 10,
                osrmEndpoint = preferences[Keys.OSRM_ENDPOINT] ?: "",
                nominatimEndpoint = preferences[Keys.NOMINATIM_ENDPOINT] ?: "",
                tripHistoryEnabled = preferences[Keys.TRIP_HISTORY_ENABLED] ?: false
            )
        }

    suspend fun updateBrightness(brightnessPct: Int) {
        val clamped = brightnessPct.coerceIn(10, 100)
        context.dashcueDataStore.edit { it[Keys.BRIGHTNESS_PCT] = clamped }
    }

    suspend fun updateTheme(theme: Int) {
        context.dashcueDataStore.edit { it[Keys.THEME] = theme.coerceIn(0, 2) }
    }

    suspend fun updateOrientation(orientation: Int) {
        context.dashcueDataStore.edit { it[Keys.ORIENTATION] = orientation.coerceIn(0, 1) }
    }

    suspend fun updateUnits(units: Int) {
        context.dashcueDataStore.edit { it[Keys.UNITS] = units.coerceIn(0, 1) }
    }

    suspend fun updateSeekStep(stepSeconds: Int) {
        val valid = if (stepSeconds == 30) 30 else 10
        context.dashcueDataStore.edit { it[Keys.SEEK_STEP_S] = valid }
    }

    suspend fun updateOsrmEndpoint(endpoint: String) {
        context.dashcueDataStore.edit { it[Keys.OSRM_ENDPOINT] = endpoint.trim() }
    }

    suspend fun updateNominatimEndpoint(endpoint: String) {
        context.dashcueDataStore.edit { it[Keys.NOMINATIM_ENDPOINT] = endpoint.trim() }
    }

    suspend fun updateTripHistoryEnabled(enabled: Boolean) {
        context.dashcueDataStore.edit { it[Keys.TRIP_HISTORY_ENABLED] = enabled }
    }
}
