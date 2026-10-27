package com.dashcue.app

import android.Manifest
import android.app.Application
import android.app.Notification
import android.content.Intent
import android.content.pm.PackageManager
import android.location.LocationManager
import android.net.Uri
import android.media.AudioManager
import android.media.session.PlaybackState
import android.provider.Settings
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.dashcue.app.audio.AudioRouter
import com.dashcue.app.audio.XBandAlertGenerator
import com.dashcue.core.ble.DashCueBleManager
import com.dashcue.core.common.DashCueLog
import com.dashcue.core.ble.LinkTelemetry
import com.dashcue.core.ble.ReconnectionStateMachine
import com.dashcue.core.database.DashCueDatabase
import com.dashcue.core.database.FavoriteDestination
import com.dashcue.core.database.RecentDestination
import com.dashcue.core.database.UserPreferencesRepository
import com.dashcue.core.database.UserSettings
import com.dashcue.core.protocol.DashCueFraming
import com.dashcue.core.protocol.DashCueFragmentation
import com.dashcue.feature.media.DashCueNotificationListenerService
import com.dashcue.feature.media.Rgb565ArtworkConverter
import com.dashcue.feature.navigation.GeoPoint
import com.dashcue.feature.navigation.NominatimClient
import com.dashcue.feature.navigation.NavNotificationParser
import com.dashcue.feature.navigation.NavigationEngine
import com.dashcue.feature.navigation.PlaceResult
import com.dashcue.feature.navigation.routeOverviewPoints
import com.dashcue.v1.CmdResult
import com.dashcue.v1.DiagReq
import com.dashcue.v1.DiagResp
import com.dashcue.v1.DeviceStatus
import com.dashcue.v1.ErrorMsg
import com.dashcue.v1.Maneuver
import com.dashcue.v1.MediaCmd
import com.dashcue.v1.MessageType
import com.dashcue.v1.NavCmd
import com.dashcue.v1.NavCommand
import com.dashcue.v1.NavState
import com.dashcue.v1.NavStatus
import com.dashcue.v1.ScreenOrientation
import com.dashcue.v1.SettingsData
import com.dashcue.v1.SettingsSet
import com.dashcue.v1.SettingsState
import com.dashcue.v1.ThemeMode
import com.dashcue.v1.UnitSystem
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Single process-scoped state holder for the companion UI.
 *
 * Outbound: media/nav/settings snapshots are mirrored to the HUD as they change.
 * Inbound: HUD MediaCmd/NavCmd are executed against the real Android session /
 * navigation engine and answered with CmdResult. Nothing here fabricates state:
 * if a subsystem is not connected the UI says so.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class DashCueViewModel(application: Application) : AndroidViewModel(application) {

    private val app = application
    private val prefs = UserPreferencesRepository(application)
    private val database = DashCueDatabase.getInstance(application)
    private val navEngine = NavigationEngine(application, scope = viewModelScope)

    val connectionState: StateFlow<ReconnectionStateMachine.State> = AppState.connectionState
    val manager: StateFlow<DashCueBleManager?> = AppState.manager

    val telemetry: StateFlow<LinkTelemetry> = AppState.manager
        .flatMapLatest { mgr -> mgr?.telemetry ?: flowOf(LinkTelemetry()) }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), LinkTelemetry())

    val mediaState = DashCueNotificationListenerService.currentMediaState
    val artwork = DashCueNotificationListenerService.artwork
    val dominantAccentRgb: StateFlow<Int?> = DashCueNotificationListenerService.dominantAccentRgb

    val navState: StateFlow<NavState?> = combine(navEngine.navState, AppState.mirroredNav) { engine, mirror ->
        mirror ?: engine
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), null)

    val navSource: StateFlow<String?> = AppState.navSource
    val mirroredNav: StateFlow<NavState?> = AppState.mirroredNav

    val routeSnapshot = navEngine.routeOverview
    val drivingLock = navEngine.drivingLockActive

    val settings: StateFlow<UserSettings> = prefs.userSettingsFlow
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), UserSettings())

    val favorites = database.favoriteDestinationDao().getAll()
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), emptyList())

    val recents = database.recentDestinationDao().getRecents(8)
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), emptyList())

    val deviceStatus: StateFlow<DeviceStatus?> = AppState.deviceStatus

    val hudSettings: StateFlow<SettingsData?> = AppState.hudSettings

    private val _destinationLabel = MutableStateFlow<String?>(null)
    val destinationLabel = _destinationLabel.asStateFlow()

    private val _searchResults = MutableStateFlow<List<PlaceResult>>(emptyList())
    val searchResults = _searchResults.asStateFlow()

    private val _searching = MutableStateFlow(false)
    val searching = _searching.asStateFlow()

    private val _searchError = MutableStateFlow<String?>(null)
    val searchError = _searchError.asStateFlow()

    val protocolLog: StateFlow<List<String>> = AppState.protocolLog

    private val _log = MutableStateFlow<List<String>>(emptyList())

    private val _messages = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val messages = _messages.asSharedFlow()

    private var searchJob: Job? = null

    val isBluetoothAudioConnected: StateFlow<Boolean> = AudioRouter.isBluetoothConnected

    /** Name of the output the phone is currently using for music (display only). */
    val mediaOutputName: String get() = AudioRouter.activeMediaOutputName(app)

    init {
        AppState.bindService(app)
        AudioRouter.register(app)

        // The HUD's Maps button asks the phone to open navigation. Provide the
        // active destination (if any) so the launch lands on a route instead
        // of an empty map, and handle engine-side command effects.
        DashCueSyncBridge.destinationProvider = {
            navEngine.currentDestination()?.let { dest ->
                Triple(dest.lat, dest.lon, destinationLabel.value)
            }
        }
        viewModelScope.launch {
            DashCueSyncBridge.navCommands.collect { cmd ->
                if (cmd.action == NavCommand.NC_END_NAVIGATION) {
                    navEngine.stopNavigation()
                    AppState.syncEngine?.clearMirroredNav()
                    _destinationLabel.value = null
                } else {
                    navEngine.handleNavCommand(cmd)
                }
            }
        }

        viewModelScope.launch {
            DashCueNotificationListenerService.navEvents.collect { event ->
                if (!event.removed && event.sbn.packageName == "com.google.android.apps.maps") {
                    triggerXBandAlertIfEnabled()
                }
            }
        }
    }

    // ---------------------------------------------------------------- media
    private fun controller() = DashCueNotificationListenerService.activeController

    fun togglePlayback() {
        val controller = controller()
        if (controller == null) {
            val listener = DashCueNotificationListenerService.instance
            if (listener != null) {
                listener.launchDefaultPlayerAndPlay()
                return
            }
            message("No active media session. Start playback in a player first.")
            return
        }
        val playing = controller.playbackState?.state == PlaybackState.STATE_PLAYING
        if (playing) controller.transportControls.pause() else controller.transportControls.play()
    }

    fun skipNext() {
        controller()?.transportControls?.skipToNext() ?: message("No active media session.")
    }

    fun skipPrevious() {
        controller()?.transportControls?.skipToPrevious() ?: message("No active media session.")
    }

    fun seekTo(positionMs: Long) {
        val controller = controller() ?: run {
            message("No active media session.")
            return
        }
        val actions = controller.playbackState?.actions ?: 0L
        if (actions and PlaybackState.ACTION_SEEK_TO == 0L) {
            message("This player does not expose seeking.")
            return
        }
        val duration = controller.metadata?.getLong(android.media.MediaMetadata.METADATA_KEY_DURATION) ?: -1L
        val target = if (duration > 0) positionMs.coerceIn(0L, duration) else positionMs.coerceAtLeast(0L)
        controller.transportControls.seekTo(target)
    }

    fun seekBy(deltaMs: Long) {
        val controller = controller() ?: run {
            message("No active media session.")
            return
        }
        val current = controller.playbackState?.position ?: 0L
        controller.transportControls.seekTo((current + deltaMs).coerceAtLeast(0L))
    }

    fun setVolume(percent: Int) {
        val audio = app.getSystemService(AudioManager::class.java) ?: return
        val max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        audio.setStreamVolume(
            AudioManager.STREAM_MUSIC,
            (max * percent.coerceIn(0, 100) / 100.0).toInt().coerceIn(0, max),
            0
        )
    }

    fun hasNotificationAccess(): Boolean {
        val flat = Settings.Secure.getString(app.contentResolver, "enabled_notification_listeners")
            ?: return false
        return flat.contains(app.packageName)
    }

    // ----------------------------------------------------------- navigation
    fun search(query: String) {
        val endpoint = settings.value.nominatimBaseUrl
        if (endpoint.isBlank()) {
            message("Set a Nominatim endpoint in Settings before searching.")
            return
        }
        if (query.isBlank()) return

        searchJob?.cancel()
        searchJob = viewModelScope.launch {
            _searching.value = true
            _searchError.value = null
            NominatimClient(endpoint).search(query).fold(
                onSuccess = { results ->
                    _searchResults.value = results
                    if (results.isEmpty()) _searchError.value = "No places found."
                },
                onFailure = { error ->
                    _searchResults.value = emptyList()
                    _searchError.value = error.message ?: "Search failed."
                }
            )
            _searching.value = false
        }
    }

    fun clearSearch() {
        searchJob?.cancel()
        _searchResults.value = emptyList()
        _searchError.value = null
        _searching.value = false
    }

    fun startNavigation(result: PlaceResult) {
        startNavigation(result.point, result.label)
    }

    fun startNavigation(destination: GeoPoint, label: String) {
        if (settings.value.osrmBaseUrl.isBlank()) {
            message("Set an OSRM endpoint in Settings before navigating.")
            return
        }
        val origin = currentLocation()
        if (origin == null) {
            message("Waiting for a GPS fix. Enable location and try again.")
            return
        }
        navEngine.startNavigation(origin, destination)
        _destinationLabel.value = label
        _searchResults.value = emptyList()
        viewModelScope.launch {
            database.recentDestinationDao().insert(
                RecentDestination(
                    label = label.take(60),
                    address = label.take(160),
                    latitude = destination.lat,
                    longitude = destination.lon
                )
            )
            database.recentDestinationDao().trimRecents(20)
        }
    }

    fun endNavigation() {
        // Mirror mode is only a listener: the route belongs to Maps/Waze. Try
        // their own Exit action first so the phone stops routing too; then let
        // the sync bridge (which owns the mirroring state) clear it and push
        // NAV_NONE to the HUD.
        val sbn = AppState.syncEngine?.lastMirroredSbn
        if (sbn != null) {
            var exitAction: Notification.Action? = null
            val actions = sbn.notification.actions ?: emptyArray()
            for (action in actions) {
                val title = action?.title?.toString().orEmpty()
                if (Regex("(?i)\\b(exit|stop|end|cancel)\\b").containsMatchIn(title)) {
                    exitAction = action
                    break
                }
            }
            if (exitAction != null) {
                try {
                    exitAction.actionIntent.send()
                } catch (e: Exception) {
                    log("NAV exit action failed: ${e.message}")
                    message("Could not stop Maps/Waze from here. Use the phone.")
                }
            } else {
                message("Stop navigation in Maps/Waze; the HUD is mirroring it.")
            }
        }
        AppState.syncEngine?.clearMirroredNav()
        navEngine.stopNavigation()
        _destinationLabel.value = null
    }

    fun openGoogleMaps() {
        triggerXBandAlertIfEnabled()
        val dest = navEngine.currentDestination()
        MapLauncher.openNavigation(app, dest?.lat, dest?.lon, destinationLabel.value)
    }

    fun triggerXBandAlertIfEnabled(isTest: Boolean = false) {
        if (settings.value.xbandAlertEnabled || isTest) {
            XBandAlertGenerator.playAlert(app, settings.value.audioRoute, isTest = isTest)
        }
    }

    fun playTestXBandAlert() {
        triggerXBandAlertIfEnabled(isTest = true)
    }

    fun openWaze() {
        // Waze first; MapLauncher's chain still covers it being uninstalled.
        launchExternal("com.waze", Uri.parse("https://waze.com/ul?navigate=yes"))
    }

    private fun launchExternal(packageName: String, uri: Uri) {
        val base = Intent(Intent.ACTION_VIEW, uri).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            app.startActivity(Intent(base).setPackage(packageName))
        } catch (e: Exception) {
            // A geo: URI is handled by Maps; an https URI can fall back to the
            // browser when Waze is not installed.
            try {
                app.startActivity(base)
            } catch (fallback: Exception) {
                message("No app can open that destination.")
            }
        }
    }

    fun currentSpeedKmhX10(): Int {
        val location = lastKnownLocation() ?: return -1
        if (!location.hasSpeed()) return -1
        return (location.speed * 3.6f * 10f).toInt().coerceIn(0, 4000)
    }

    fun mirrorAgeMs(): Long = AppState.syncEngine?.mirrorAgeMs() ?: -1L

    fun currentLocation(): GeoPoint? = lastKnownLocation()?.let { GeoPoint(it.latitude, it.longitude) }

    private fun lastKnownLocation(): android.location.Location? {
        if (ContextCompat.checkSelfPermission(app, Manifest.permission.ACCESS_FINE_LOCATION) !=
            PackageManager.PERMISSION_GRANTED
        ) return null
        val lm = app.getSystemService(LocationManager::class.java) ?: return null
        return try {
            lm.getLastKnownLocation(LocationManager.GPS_PROVIDER)
                ?: lm.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
        } catch (e: SecurityException) {
            null
        }
    }

    fun routePoints(): List<GeoPoint> = routeOverviewPoints(routeSnapshot.value)

    // ------------------------------------------------------------ favorites
    fun saveFavorite(label: String, point: GeoPoint) {
        viewModelScope.launch {
            database.favoriteDestinationDao().insert(
                FavoriteDestination(
                    label = label.take(60),
                    address = label.take(160),
                    latitude = point.lat,
                    longitude = point.lon
                )
            )
            message("Saved $label")
        }
    }

    fun deleteFavorite(favorite: FavoriteDestination) {
        viewModelScope.launch {
            database.favoriteDestinationDao().delete(favorite)
        }
    }

    // ------------------------------------------------------------- settings
    fun setBrightness(percent: Int) {
        viewModelScope.launch { prefs.updateBrightness(percent) }
    }

    fun setTheme(mode: Int) {
        viewModelScope.launch { prefs.updateTheme(mode) }
    }

    fun setOrientation(orientation: Int) {
        viewModelScope.launch { prefs.updateOrientation(orientation) }
    }

    fun setUnits(units: Int) {
        viewModelScope.launch { prefs.updateUnits(units) }
    }

    fun setSeekStep(seconds: Int) {
        viewModelScope.launch { prefs.updateSeekStep(seconds) }
    }

    fun setOsrmEndpoint(url: String) {
        viewModelScope.launch { prefs.updateOsrmBaseUrl(url) }
    }

    fun setNominatimEndpoint(url: String) {
        viewModelScope.launch { prefs.updateNominatimBaseUrl(url) }
    }

    /**
     * Manual recovery: clears backoff/terminal state and rescans for the HUD.
     * Covers the stuck cases where Bluetooth was toggled, permission was
     * re-granted or the HUD rebooted while the app was giving up.
     */
    fun refreshConnection() {
        val mgr = manager.value
        if (mgr != null) {
            mgr.reconnectNow()
        } else {
            AppState.startScanAndConnection(app)
        }
        message("Reconnecting to the HUD…")
    }

    fun forgetPairedDevice() {
        viewModelScope.launch { prefs.forgetPairedDevice() }
        AppState.stopConnection(app)
    }

    // ------------------------------------------------------------- audio & dsp
    fun setAudioRoute(route: Int) {
        viewModelScope.launch { prefs.updateAudioRoute(route) }
    }

    fun setXBandAlertEnabled(enabled: Boolean) {
        viewModelScope.launch { prefs.updateXBandAlertEnabled(enabled) }
    }

    // ---------------------------------------------------------- diagnostics
    fun refreshDiagnostics() {
        AppState.syncEngine?.requestDiagnostics()
    }

    fun clearLog() {
        _log.value = emptyList()
        AppState.syncEngine?.clearLog()
    }

    fun exportDiagnostics(): String {
        val root = JSONObject()
        root.put("exportedAt", isoNow())
        root.put("appVersion", "1.0.0")
        root.put("protocol", "1.0")
        root.put("connection", connectionState.value.name)
        root.put("mtu", telemetry.value.mtu)
        root.put("rssiDbm", telemetry.value.rssiDbm)
        root.put("packetsIn", telemetry.value.packetsIn)
        root.put("packetsOut", telemetry.value.packetsOut)
        deviceStatus.value?.let { ds ->
            root.put("deviceStatus", JSONObject().apply {
                put("uptimeS", ds.uptimeS)
                put("minFreeHeap", ds.minFreeHeap)
                put("largestFreeBlock", ds.largestFreeBlock)
                put("queueDrops", ds.queueDrops)
                put("crcFailures", ds.crcFailures)
                put("duplicateCommands", ds.duplicateCommands)
                put("rssiDbm", ds.rssiDbm)
                put("mtu", ds.mtu)
                put("uiAvgMs", ds.uiAvgMs)
                put("uiP95Ms", ds.uiP95Ms)
                put("uiMaxMs", ds.uiMaxMs)
                put("safeMode", ds.safeMode)
            })
        }
        root.put("settings", JSONObject().apply {
            put("brightnessPct", settings.value.brightnessPct)
            put("theme", settings.value.themeMode)
            put("orientation", settings.value.orientation)
            put("units", settings.value.units)
            put("osrmEndpoint", settings.value.osrmBaseUrl)
            put("nominatimEndpoint", settings.value.nominatimBaseUrl)
        })
        root.put("log", JSONArray(_log.value))
        return root.toString(2)
    }

    private fun log(line: String) {
        val entry = "${isoNow()} $line"
        _log.update { (it + entry).takeLast(200) }
        DashCueLog.i("DashCueViewModel", line)
    }

    private fun isoNow(): String =
        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS", Locale.US).format(Date())

    private fun message(text: String) {
        _messages.tryEmit(text)
    }

    override fun onCleared() {
        super.onCleared()
        AudioRouter.unregister(app)
    }
}
