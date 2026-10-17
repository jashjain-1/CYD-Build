package com.dashcue.app

import android.Manifest
import android.app.Application
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
import com.dashcue.core.ble.DashCueBleManager
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
import com.dashcue.v1.MediaCmd
import com.dashcue.v1.MessageType
import com.dashcue.v1.NavCmd
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
        .stateIn(viewModelScope, SharingStarted.Eagerly, LinkTelemetry())

    val mediaState = DashCueNotificationListenerService.currentMediaState
    val artwork = DashCueNotificationListenerService.artwork

    private val _mirroredNav = MutableStateFlow<NavState?>(null)
    private val _navSource = MutableStateFlow<String?>(null)

    /**
     * The HUD and the phone show exactly the same navigation snapshot. When a
     * Maps/Waze notification is live it wins; otherwise the in-app engine (if
     * one is running) is used. There is no third, silently different state.
     */
    val navState: StateFlow<NavState?> = combine(navEngine.navState, _mirroredNav) { engine, mirror ->
        mirror ?: engine
    }.stateIn(viewModelScope, SharingStarted.Eagerly, null)

    val navSource: StateFlow<String?> = _navSource.asStateFlow()
    val mirroredNav: StateFlow<NavState?> = _mirroredNav.asStateFlow()

    val routeSnapshot = navEngine.routeOverview
    val drivingLock = navEngine.drivingLockActive

    val settings: StateFlow<UserSettings> = prefs.userSettingsFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, UserSettings())

    val favorites = database.favoriteDestinationDao().getAll()
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())

    val recents = database.recentDestinationDao().getRecents(8)
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())

    private val _deviceStatus = MutableStateFlow<DeviceStatus?>(null)
    val deviceStatus = _deviceStatus.asStateFlow()

    private val _hudSettings = MutableStateFlow<SettingsData?>(null)
    val hudSettings = _hudSettings.asStateFlow()

    private val _destinationLabel = MutableStateFlow<String?>(null)
    val destinationLabel = _destinationLabel.asStateFlow()

    private val _searchResults = MutableStateFlow<List<PlaceResult>>(emptyList())
    val searchResults = _searchResults.asStateFlow()

    private val _searching = MutableStateFlow(false)
    val searching = _searching.asStateFlow()

    private val _searchError = MutableStateFlow<String?>(null)
    val searchError = _searchError.asStateFlow()

    private val _log = MutableStateFlow<List<String>>(emptyList())
    val protocolLog = _log.asStateFlow()

    private val _messages = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val messages = _messages.asSharedFlow()

    private var commandId = 1L
    private var settingsVersion = 1L
    private var searchJob: Job? = null
    private var lastMirrorSbn: android.service.notification.StatusBarNotification? = null
    private var lastMirrorKey: String? = null
    private var lastMirrorAtMs: Long = 0L
    private var lastMirrorNav: NavState? = null

    init {
        AppState.bindService(app)
        observeLink()
        observeNavigationNotifications()
        observeArtwork()
        startLivenessTicker()
        mirrorStateToHud()
        mirrorSettingsToHud()
    }

    private fun nextCommandId(): Long = commandId++

    private fun observeLink() {
        viewModelScope.launch {
            AppState.manager.collectLatest { mgr ->
                if (mgr == null) return@collectLatest
                mgr.onSnapshotResyncRequired = { sendSnapshotToHud() }

                launch {
                    mgr.connectionState.collect { state ->
                        log("LINK ${state.name}")
                        if (state == ReconnectionStateMachine.State.READY) {
                            DashCueNotificationListenerService.instance?.syncNavigationState()
                            sendSnapshotToHud()
                        }
                    }
                }
                launch {
                    mgr.incomingEvents.collect { frame -> handleIncoming(mgr, frame) }
                }
            }
        }
    }

    private fun observeNavigationNotifications() {
        viewModelScope.launch {
            DashCueNotificationListenerService.navEvents.collect { event ->
                if (event.removed) {
                    if (event.sbn.key == lastMirrorKey) {
                        lastMirrorSbn = null
                        lastMirrorKey = null
                        lastMirrorNav = null
                        _mirroredNav.value = null
                        _navSource.value = null
                        val clearNav = NavState.newBuilder()
                            .setStatus(NavStatus.NAV_NONE)
                            .setPhoneTsMs(System.currentTimeMillis())
                            .build()
                        sendToHud(
                            MessageType.MT_NAV_STATE.number,
                            System.currentTimeMillis(),
                            clearNav.toByteArray(),
                            "NAV_STATE (CLEARED)"
                        )
                    }
                    return@collect
                }

                val parsed = try {
                    NavNotificationParser.parse(app, event.sbn)
                } catch (e: Exception) {
                    log("NAV mirror parse failed: ${e.message}")
                    null
                } ?: return@collect

                lastMirrorSbn = event.sbn
                lastMirrorKey = event.sbn.key
                lastMirrorAtMs = System.currentTimeMillis()
                _navSource.value = parsed.sourceLabel

                val speed = currentSpeedKmhX10()
                val mirrored = NavState.newBuilder()
                    .setRouteId(parsed.notificationKey.hashCode() and 0x7fffffff)
                    .setStatus(NavStatus.NAV_ACTIVE)
                    .setNext(parsed.maneuver)
                    .setDistToNextM(parsed.distanceToNextM)
                    .setStreet(parsed.street)
                    .setRemainingM(parsed.remainingM)
                    .setRemainingS(parsed.remainingS)
                    .setSpeedKmhX10(speed)
                    .setHasLiveTraffic(parsed.trafficIsExplicit)
                    .setTraffic(parsed.traffic)
                    .setEtaEpochS(parsed.etaEpochS.toInt())
                    .setSpeedLimitKmh(parsed.speedLimitKmh)
                    .setSourceApp(parsed.sourceLabel)
                    .setPhoneTsMs(parsed.updatedAtMs)
                    .build()
                lastMirrorNav = mirrored
                _mirroredNav.value = mirrored
                AppState.manager.value?.setReportedSpeed(speed)
                log("NAV mirror ${parsed.sourceLabel}: ${parsed.instruction} / ${parsed.distanceToNextM} m")
            }
        }
    }

    /**
     * Playback callbacks are event-driven, not a clock. Without a heartbeat the
     * HUD marks media stale after 6 s even though the track is playing, and a
     * mirrored Maps cue expires after 5 s between notification updates. This
     * ticker re-emits the current snapshot with a fresh position/ETA so both
     * surfaces stay live and the progress bar advances while paused albums sit
     * at a stable position.
     */
    private fun startLivenessTicker() {
        viewModelScope.launch {
            while (true) {
                kotlinx.coroutines.delay(2000)
                DashCueNotificationListenerService.instance?.refreshMediaState()
                DashCueNotificationListenerService.instance?.syncNavigationState()

                val last = lastMirrorNav ?: continue
                val nowS = System.currentTimeMillis() / 1000
                val remaining = (last.etaEpochS.toLong() - nowS).coerceIn(0L, 7L * 24 * 3600).toInt()
                val speed = currentSpeedKmhX10()
                _mirroredNav.value = last.toBuilder()
                    .setRemainingS(remaining)
                    .setSpeedKmhX10(speed)
                    .setPhoneTsMs(System.currentTimeMillis())
                    .build()
                lastMirrorNav = _mirroredNav.value
                AppState.manager.value?.setReportedSpeed(speed)
            }
        }
    }

    private fun observeArtwork() {
        viewModelScope.launch {
            DashCueNotificationListenerService.artworkTransfer
                .filterNotNull()
                .collectLatest { transfer -> sendArtworkWithFallback(transfer) }
        }
    }

    private suspend fun sendArtworkWithFallback(transfer: Rgb565ArtworkConverter.PreparedArtwork) {
        val mgr = AppState.manager.value ?: return
        if (mgr.stateMachine.currentState != ReconnectionStateMachine.State.READY) return

        // Tier 1: Paced 64x64 RGB565 transfer (16ms pace between 1KB chunks) with 1 retry on error
        val tier1Success = transmitArtworkPrepared(mgr, transfer, paceMs = 16L, maxRetries = 1)
        if (tier1Success) {
            log("TX ARTWORK ${transfer.artworkId} OK (Tier 1: 64x64, ${transfer.chunks.size} chunks)")
            return
        }

        // Tier 2 Fallback: Compact 32x32 RGB565 thumbnail (2 KB = 2 chunks)
        val bitmap = DashCueNotificationListenerService.artwork.value
        if (bitmap != null) {
            try {
                log("TX ARTWORK Tier 1 dropped/timed out -> Fallback to Tier 2: 32x32 thumbnail")
                val thumbTransfer = Rgb565ArtworkConverter.convertBitmapToRgb565(
                    bitmap,
                    transfer.artworkId,
                    targetWidth = 32,
                    targetHeight = 32
                )
                val tier2Success = transmitArtworkPrepared(mgr, thumbTransfer, paceMs = 20L, maxRetries = 1)
                if (tier2Success) {
                    log("TX ARTWORK ${transfer.artworkId} OK (Tier 2: 32x32 thumbnail fallback)")
                    return
                }
            } catch (e: Exception) {
                log("Tier 2 thumbnail transmission error: ${e.message}")
            }
        }

        // Tier 3 Fallback: Emblem & Vibrant Backdrop gradient
        log("TX ARTWORK Fallback Tier 3: High-contrast emblem & dynamic backdrop active")
    }

    private suspend fun transmitArtworkPrepared(
        mgr: DashCueBleManager,
        transfer: Rgb565ArtworkConverter.PreparedArtwork,
        paceMs: Long,
        maxRetries: Int
    ): Boolean {
        try {
            var beginOk = false
            for (attempt in 0..maxRetries) {
                beginOk = mgr.sendMessage(
                    MessageType.MT_ARTWORK_BEGIN.number,
                    transfer.artworkId,
                    0,
                    transfer.beginMsg.toByteArray(),
                    DashCueFragmentation.LANE_BULK
                )
                if (beginOk) break
                kotlinx.coroutines.delay(paceMs)
            }
            if (!beginOk) return false

            for ((index, chunk) in transfer.chunks.withIndex()) {
                var chunkOk = false
                for (attempt in 0..maxRetries) {
                    chunkOk = mgr.sendMessage(
                        MessageType.MT_ARTWORK_CHUNK.number,
                        transfer.artworkId + index,
                        0,
                        chunk.toByteArray(),
                        DashCueFragmentation.LANE_BULK
                    )
                    if (chunkOk) break
                    kotlinx.coroutines.delay(paceMs * 2)
                }
                if (!chunkOk) return false
                kotlinx.coroutines.delay(paceMs)
            }

            var endOk = false
            for (attempt in 0..maxRetries) {
                endOk = mgr.sendMessage(
                    MessageType.MT_ARTWORK_END.number,
                    transfer.artworkId,
                    0,
                    transfer.endMsg.toByteArray(),
                    DashCueFragmentation.LANE_BULK
                )
                if (endOk) break
                kotlinx.coroutines.delay(paceMs)
            }
            return endOk
        } catch (e: Exception) {
            log("Artwork transmission error: ${e.message}")
            return false
        }
    }

    private fun mirrorStateToHud() {
        viewModelScope.launch {
            mediaState.filterNotNull().debounce(250).collect { state ->
                sendToHud(
                    MessageType.MT_MEDIA_STATE.number,
                    System.currentTimeMillis(),
                    state.toByteArray(),
                    "MEDIA_STATE"
                )
            }
        }
        viewModelScope.launch {
            navState.filterNotNull().debounce(200).collect { state ->
                sendToHud(
                    MessageType.MT_NAV_STATE.number,
                    System.currentTimeMillis(),
                    state.toByteArray(),
                    "NAV_STATE"
                )
            }
        }
    }

    private fun mirrorSettingsToHud() {
        viewModelScope.launch {
            settings.drop(1).debounce(300).collect { sendSettingsToHud(it) }
        }
    }

    private suspend fun sendSnapshotToHud() {
        mediaState.value?.let {
            sendToHud(MessageType.MT_MEDIA_STATE.number, System.currentTimeMillis(), it.toByteArray(), "MEDIA_STATE(snapshot)")
        }
        val currentNav = navState.value ?: NavState.newBuilder()
            .setStatus(NavStatus.NAV_NONE)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()
        sendToHud(MessageType.MT_NAV_STATE.number, System.currentTimeMillis(), currentNav.toByteArray(), "NAV_STATE(snapshot)")
        sendSettingsToHud(settings.value)
        DashCueNotificationListenerService.artworkTransfer.value?.let { sendArtworkWithFallback(it) }
    }

    private suspend fun sendSettingsToHud(value: UserSettings) {
        val data = SettingsData.newBuilder()
            .setBrightnessPct(value.brightnessPct.coerceIn(10, 100))
            .setTheme(ThemeMode.forNumber(value.themeMode) ?: ThemeMode.TH_AUTO)
            .setOrientation(ScreenOrientation.forNumber(value.orientation) ?: ScreenOrientation.SO_LANDSCAPE)
            .setSeekStepS(if (value.seekStepS == 30) 30 else 10)
            .setUnits(UnitSystem.forNumber(value.units) ?: UnitSystem.UN_METRIC)
            .setVersion((settingsVersion++).toInt())
            .build()
        val id = nextCommandId()
        val message = SettingsSet.newBuilder().setCommandId(id.toInt()).setSettings(data).build()
        sendToHud(MessageType.MT_SETTINGS_SET.number, id, message.toByteArray(), "SETTINGS_SET")
    }

    private suspend fun sendToHud(
        msgType: Int,
        msgId: Long,
        payload: ByteArray,
        label: String
    ) {
        val mgr = AppState.manager.value ?: return
        if (mgr.stateMachine.currentState != ReconnectionStateMachine.State.READY) return
        val ok = try {
            mgr.sendMessage(msgType, msgId, 0, payload)
        } catch (e: Exception) {
            log("TX $label failed: ${e.message}")
            false
        }
        log(if (ok) "TX $label (${payload.size} B)" else "TX $label FAILED")
    }

    private suspend fun handleIncoming(
        mgr: DashCueBleManager,
        frame: DashCueFraming.DecodeResult.Success
    ) {
        val type = MessageType.forNumber(frame.header.msgType)
        log("RX ${type?.name ?: frame.header.msgType} (${frame.payload.size} B)")

        try {
            when (type) {
                MessageType.MT_MEDIA_CMD -> {
                    val cmd = MediaCmd.parseFrom(frame.payload)
                    val listener = DashCueNotificationListenerService.instance
                    if (listener == null) {
                        sendCommandResult(
                            mgr,
                            CmdResult.newBuilder()
                                .setCommandId(cmd.commandId)
                                .setStatus(CmdResult.Status.ST_FAILED)
                                .setReason(com.dashcue.v1.ErrorCode.EC_NO_SESSION_VALUE)
                                .build()
                        )
                    } else {
                        listener.dispatchMediaCommand(cmd) { result ->
                            viewModelScope.launch { sendCommandResult(mgr, result) }
                        }
                    }
                }
                MessageType.MT_NAV_CMD -> {
                    val cmd = NavCmd.parseFrom(frame.payload)
                    val result = when (cmd.action) {
                        com.dashcue.v1.NavCommand.NC_END_NAVIGATION -> {
                            endNavigation()
                            val clearNav = NavState.newBuilder()
                                .setStatus(NavStatus.NAV_NONE)
                                .setPhoneTsMs(System.currentTimeMillis())
                                .build()
                            sendToHud(
                                MessageType.MT_NAV_STATE.number,
                                System.currentTimeMillis(),
                                clearNav.toByteArray(),
                                "NAV_STATE (CLEARED)"
                            )
                            CmdResult.newBuilder()
                                .setCommandId(cmd.commandId)
                                .setStatus(CmdResult.Status.ST_CONFIRMED)
                                .setObservedAtMs(System.currentTimeMillis())
                                .build()
                        }
                        com.dashcue.v1.NavCommand.NC_REROUTE -> {
                            if (_mirroredNav.value == null && navState.value?.status != NavStatus.NAV_ACTIVE) {
                                openGoogleMaps()
                            }
                            navEngine.handleNavCommand(cmd)
                        }
                        else -> navEngine.handleNavCommand(cmd)
                    }
                    sendCommandResult(mgr, result)
                }
                MessageType.MT_DEVICE_STATUS -> {
                    _deviceStatus.value = DeviceStatus.parseFrom(frame.payload)
                }
                MessageType.MT_DIAG_RESP -> {
                    val resp = DiagResp.parseFrom(frame.payload)
                    if (resp.hasStatus()) _deviceStatus.value = resp.status
                    resp.logLinesList.forEach { log("HUD $it") }
                }
                MessageType.MT_SETTINGS_SET -> {
                    // The HUD changed brightness/theme/orientation locally; adopt
                    // it so both sides converge. The preference collector then
                    // mirrors the same values back with a fresh version.
                    val set = SettingsSet.parseFrom(frame.payload)
                    if (set.hasSettings()) applySettingsFromHud(set.settings)
                }
                MessageType.MT_SETTINGS_STATE -> {
                    val state = SettingsState.parseFrom(frame.payload)
                    if (state.hasSettings()) _hudSettings.value = state.settings
                }
                MessageType.MT_ERROR -> {
                    val error = ErrorMsg.parseFrom(frame.payload)
                    log("HUD ERROR code=${error.code} text=${error.text}")
                }
                MessageType.MT_HEARTBEAT -> Unit
                else -> Unit
            }
        } catch (e: Exception) {
            log("RX decode failed: ${e.message}")
        }
    }

    private fun applySettingsFromHud(data: SettingsData) {
        _hudSettings.value = data
        viewModelScope.launch {
            prefs.updateBrightness(data.brightnessPct.toInt())
            prefs.updateTheme(data.theme.number)
            prefs.updateOrientation(data.orientation.number)
            prefs.updateUnits(data.units.number)
            prefs.updateSeekStep(data.seekStepS.toInt())
        }
    }

    private suspend fun sendCommandResult(mgr: DashCueBleManager, result: CmdResult) {
        val ok = mgr.sendMessage(
            MessageType.MT_CMD_RESULT.number,
            result.commandId.toLong() and 0xFFFFFFFFL,
            0,
            result.toByteArray()
        )
        log("TX CMD_RESULT id=${result.commandId} status=${result.status} ok=$ok")
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
        // their own Exit action first so the phone stops routing too; if the
        // action is unavailable, clear the local mirror and say so rather than
        // pretending the other app was stopped.
        val sbn = lastMirrorSbn
        if (sbn != null) {
            val exitAction = sbn.notification.actions?.firstOrNull { action ->
                val title = action.title?.toString().orEmpty()
                Regex("(?i)\\b(exit|stop|end|cancel)\\b").containsMatchIn(title)
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
            lastMirrorSbn = null
            lastMirrorKey = null
            lastMirrorNav = null
            _mirroredNav.value = null
            _navSource.value = null
        }
        navEngine.stopNavigation()
        _destinationLabel.value = null
    }

    fun openGoogleMaps() {
        launchExternal("com.google.android.apps.maps", Uri.parse("geo:0,0?q="))
    }

    fun openWaze() {
        launchExternal("com.waze", Uri.parse("https://waze.com/ul"))
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

    fun mirrorAgeMs(): Long =
        if (lastMirrorAtMs == 0L) -1L else (System.currentTimeMillis() - lastMirrorAtMs).coerceAtLeast(0L)

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

    fun forgetPairedDevice() {
        viewModelScope.launch { prefs.forgetPairedDevice() }
        AppState.stopConnection(app)
    }

    // ---------------------------------------------------------- diagnostics
    fun refreshDiagnostics() {
        val id = nextCommandId()
        val request = DiagReq.newBuilder().setCommandId(id.toInt()).setWant(1).build()
        viewModelScope.launch {
            sendToHud(MessageType.MT_DIAG_REQ.number, id, request.toByteArray(), "DIAG_REQ")
        }
    }

    fun clearLog() {
        _log.value = emptyList()
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
        _deviceStatus.value?.let { ds ->
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
    }

    private fun isoNow(): String =
        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS", Locale.US).format(Date())

    private fun message(text: String) {
        _messages.tryEmit(text)
    }
}
