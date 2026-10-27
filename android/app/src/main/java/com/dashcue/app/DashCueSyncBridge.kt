package com.dashcue.app

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Geocoder
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.Looper
import androidx.core.content.ContextCompat
import com.dashcue.core.ble.DashCueBleManager
import com.dashcue.core.ble.ReconnectionStateMachine
import com.dashcue.core.common.DashCueLog
import com.dashcue.core.database.UserPreferencesRepository
import com.dashcue.core.database.UserSettings
import com.dashcue.core.protocol.DashCueFragmentation
import com.dashcue.core.protocol.DashCueFraming
import com.dashcue.feature.media.DashCueNotificationListenerService
import com.dashcue.feature.media.Rgb565ArtworkConverter
import com.dashcue.feature.navigation.NavNotificationParser
import com.dashcue.feature.navigation.NavTextSanitizer
import com.dashcue.v1.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.Locale

/**
 * Process-scoped synchronization engine between Android companion and DashCue HUD.
 *
 * Runs continuously in AppState scope, surviving screen-off, device locking,
 * and MainActivity destruction.
 */
@OptIn(FlowPreview::class)
class DashCueSyncBridge(private val context: Context) {
    companion object {
        private const val TAG = "DashCueSyncBridge"

        /**
         * Nav commands the HUD sent, delivered to the ViewModel (which owns the
         * NavigationEngine) without the engine having to reach back into the
         * bridge. Emitted as a SharedFlow so late subscribers still see the
         * next command rather than requiring the bridge instance.
         */
        private val _navCommands = MutableSharedFlow<NavCmd>(extraBufferCapacity = 4)
        val navCommands: SharedFlow<NavCmd> = _navCommands.asSharedFlow()

        /**
         * Optional destination lookup installed by the ViewModel so a HUD
         * "open maps" request can start navigation straight at the active
         * destination instead of just opening the map at the current position.
         */
        var destinationProvider: (() -> Triple<Double, Double, String?>?)? = null
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val prefs = UserPreferencesRepository(context)

    private var syncJob: Job? = null
    private var activeManager: DashCueBleManager? = null

    private var commandId = 1L
    private var settingsVersion = 1L

    private val _mirroredNav = MutableStateFlow<NavState?>(null)
    val mirroredNav: StateFlow<NavState?> = _mirroredNav.asStateFlow()

    private val _navSource = MutableStateFlow<String?>(null)
    val navSource: StateFlow<String?> = _navSource.asStateFlow()

    private val _hudSettings = MutableStateFlow<SettingsData?>(null)
    val hudSettings: StateFlow<SettingsData?> = _hudSettings.asStateFlow()

    private val _deviceStatus = MutableStateFlow<DeviceStatus?>(null)
    val deviceStatus: StateFlow<DeviceStatus?> = _deviceStatus.asStateFlow()

    private val _protocolLog = MutableStateFlow<List<String>>(emptyList())
    val protocolLog: StateFlow<List<String>> = _protocolLog.asStateFlow()

    private var lastMirrorKey: String? = null
    /** Last StatusBarNotification that produced a mirrored nav state (for the Exit action). */
    var lastMirroredSbn: android.service.notification.StatusBarNotification? = null
    private var lastMirrorAtMs: Long = 0L
    private var lastMirrorNav: NavState? = null

    private var locationManager: LocationManager? = null
    private var locationListener: LocationListener? = null
    private var lastLocation: Location? = null
    private var cachedGeocodedStreet: String = ""
    private var lastGeocodedLocation: Location? = null
    private var lastGeocodedAtMs: Long = 0L
    private var lastKnownSpeedLimitKmh: Int = 0
    /** Artwork id the HUD has confirmed by a completed transfer this link. */
    private var lastSentArtworkId: Long = 0L
    /**
     * One artwork transfer at a time. The HUD's store accepts strictly
     * sequential chunk offsets; two coroutines streaming the same cover used to
     * interleave their BEGINs and corrupt the offset sequence (observed on
     * hardware as "ARTWORK chunk REJECTED id=4 off=1024").
     */
    private val artworkMutex = Mutex()

    private fun log(message: String) {
        val entry = "[${java.text.SimpleDateFormat("HH:mm:ss.SSS", java.util.Locale.US).format(java.util.Date())}] $message"
        DashCueLog.i(TAG, message)
        _protocolLog.update { (it + entry).takeLast(64) }
    }

    fun start(mgr: DashCueBleManager) {
        if (activeManager === mgr && syncJob?.isActive == true) return
        stop()
        activeManager = mgr
        log("DashCueSyncBridge starting background pipeline")

        mgr.onSnapshotResyncRequired = {
            scope.launch { sendSnapshotToHud(mgr) }
        }

        startLocationUpdates()

        syncJob = scope.launch {
            launch { observeConnection(mgr) }
            launch { observeIncomingEvents(mgr) }
            launch { observeMedia(mgr) }
            launch { observeNavigation(mgr) }
            launch { observeArtwork(mgr) }
            launch { observeSettings(mgr) }
            launch { startLivenessTicker(mgr) }
        }
    }

    fun stop() {
        stopLocationUpdates()
        syncJob?.cancel()
        syncJob = null
        activeManager = null
        log("DashCueSyncBridge stopped")
    }

    private fun startLocationUpdates() {
        if (locationListener != null) return
        val lm = context.getSystemService(LocationManager::class.java) ?: return
        locationManager = lm

        if (ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_COARSE_LOCATION) != PackageManager.PERMISSION_GRANTED
        ) {
            log("Location permission not granted for standby telemetry")
            return
        }

        val listener = object : LocationListener {
            override fun onLocationChanged(loc: Location) {
                lastLocation = loc
                if (loc.hasSpeed()) {
                    val speed = (loc.speed * 3.6f * 10f).toInt().coerceIn(0, 4000)
                    activeManager?.setReportedSpeed(speed)
                }
                updateGeocodedStreet(loc)
            }

            @Deprecated("Deprecated in Java")
            override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
            override fun onProviderEnabled(provider: String) {}
            override fun onProviderDisabled(provider: String) {}
        }
        locationListener = listener

        try {
            val mainLooper = Looper.getMainLooper() ?: Looper.myLooper()
            if (lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                if (mainLooper != null) {
                    lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 1.0f, listener, mainLooper)
                } else {
                    lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 1.0f, listener)
                }
            }
            if (lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                if (mainLooper != null) {
                    lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000L, 1.0f, listener, mainLooper)
                } else {
                    lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000L, 1.0f, listener)
                }
            }
            log("Active LocationListener registered for standby telemetry")
        } catch (e: SecurityException) {
            log("Location SecurityException: ${e.message}")
        } catch (e: Exception) {
            log("Location registration failed: ${e.message}")
        }
    }

    private fun stopLocationUpdates() {
        locationListener?.let { listener ->
            try {
                locationManager?.removeUpdates(listener)
            } catch (e: Exception) {
                // ignore
            }
        }
        locationListener = null
    }

    private fun updateGeocodedStreet(loc: Location) {
        val now = System.currentTimeMillis()
        val lastLoc = lastGeocodedLocation
        if (lastLoc != null && now - lastGeocodedAtMs < 10_000L && loc.distanceTo(lastLoc) < 75.0f) {
            return
        }
        lastGeocodedLocation = loc
        lastGeocodedAtMs = now

        scope.launch(Dispatchers.IO) {
            try {
                if (!Geocoder.isPresent()) return@launch
                val geocoder = Geocoder(context, Locale.getDefault())
                @Suppress("DEPRECATION")
                val addresses = geocoder.getFromLocation(loc.latitude, loc.longitude, 1)
                val address = addresses?.firstOrNull() ?: return@launch
                val street = address.thoroughfare
                    ?: address.subThoroughfare?.let { "$it ${address.thoroughfare.orEmpty()}" }
                    ?: address.featureName
                    ?: address.locality
                if (!street.isNullOrBlank()) {
                    cachedGeocodedStreet = NavTextSanitizer.sanitize(street, 48)
                }
            } catch (e: Exception) {
                // Network or geocoder service unavailable
            }
        }
    }

    private suspend fun observeConnection(mgr: DashCueBleManager) {
        mgr.connectionState.collect { state ->
            log("BLE State: ${state.name}")
            if (state == ReconnectionStateMachine.State.READY) {
                // The HUD drops its cover on link loss, so a reconnect must
                // resend the current artwork even though its id is unchanged.
                lastSentArtworkId = 0L
                DashCueNotificationListenerService.instance?.syncNavigationState()
                DashCueNotificationListenerService.instance?.refreshMediaState()
                sendSnapshotToHud(mgr)
            }
        }
    }

    private suspend fun observeIncomingEvents(mgr: DashCueBleManager) {
        mgr.incomingEvents.collect { frame ->
            handleIncoming(mgr, frame)
        }
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
                                .setReason(ErrorCode.EC_NO_SESSION_VALUE)
                                .build()
                        )
                    } else {
                        listener.dispatchMediaCommand(cmd) { result ->
                            scope.launch { sendCommandResult(mgr, result) }
                        }
                    }
                }
                MessageType.MT_SETTINGS_SET -> {
                    val set = SettingsSet.parseFrom(frame.payload)
                    if (set.hasSettings()) applySettingsFromHud(set.settings)
                }
                MessageType.MT_NAV_CMD -> {
                    val cmd = NavCmd.parseFrom(frame.payload)
                    log("RX NAV_CMD action=${cmd.action} id=${cmd.commandId}")

                    // The HUD's Maps button sends NC_REROUTE. Until now no
                    // handler existed, so the button did nothing at all.
                    if (cmd.action == NavCommand.NC_REROUTE) {
                        val destination = destinationProvider?.invoke()
                        val result = MapLauncher.openNavigation(
                            context,
                            destination?.first,
                            destination?.second,
                            destination?.third
                        )
                        log("Map launch via ${result.target} for HUD reroute")
                    }

                    // Give the engine its own view of the command (mirrored
                    // route teardown, in-app reroute state).
                    _navCommands.tryEmit(cmd)

                    sendCommandResult(
                        mgr,
                        CmdResult.newBuilder()
                            .setCommandId(cmd.commandId)
                            .setStatus(CmdResult.Status.ST_CONFIRMED)
                            .build()
                    )
                }
                MessageType.MT_SETTINGS_STATE -> {
                    val state = SettingsState.parseFrom(frame.payload)
                    if (state.hasSettings()) _hudSettings.value = state.settings
                }
                MessageType.MT_DEVICE_STATUS -> {
                    val status = DeviceStatus.parseFrom(frame.payload)
                    _deviceStatus.value = status
                    log("HUD DeviceStatus heap=${status.minFreeHeap} uptime=${status.uptimeS}s")
                }
                MessageType.MT_DIAG_RESP -> {
                    val resp = DiagResp.parseFrom(frame.payload)
                    if (resp.hasStatus()) _deviceStatus.value = resp.status
                    resp.logLinesList.forEach { log("HUD $it") }
                }
                MessageType.MT_SNAPSHOT_REQUEST -> {
                    // The HUD asks because its cover is missing for the current
                    // track; allow the resend even if the id is unchanged.
                    lastSentArtworkId = 0L
                    log("HUD requested a full snapshot (artwork resend)")
                    sendSnapshotToHud(mgr)
                }
                MessageType.MT_ERROR -> {
                    val error = ErrorMsg.parseFrom(frame.payload)
                    log("HUD ERROR code=${error.code} text=${error.text}")
                }
                else -> Unit
            }
        } catch (e: Exception) {
            log("RX decode failed: ${e.message}")
        }
    }

    private fun applySettingsFromHud(data: SettingsData) {
        _hudSettings.value = data
        scope.launch {
            prefs.updateBrightness(data.brightnessPct)
            prefs.updateTheme(data.theme.number)
            prefs.updateOrientation(data.orientation.number)
            prefs.updateUnits(data.units.number)
            prefs.updateSeekStep(data.seekStepS)
        }
    }

    private suspend fun sendCommandResult(mgr: DashCueBleManager, result: CmdResult) {
        val ok = mgr.sendMessage(
            MessageType.MT_CMD_RESULT.number,
            result.commandId.toLong(),
            0,
            result.toByteArray(),
            DashCueFragmentation.LANE_CONTROL
        )
        log("TX CMD_RESULT id=${result.commandId} status=${result.status} ok=$ok")
    }

    private suspend fun observeMedia(mgr: DashCueBleManager) {
        DashCueNotificationListenerService.currentMediaState
            .filterNotNull()
            .debounce(200)
            .collect { state ->
                sendToHud(
                    mgr,
                    MessageType.MT_MEDIA_STATE.number,
                    System.currentTimeMillis(),
                    state.toByteArray(),
                    "MEDIA_STATE"
                )
                // Artwork and MediaState are separate flows. If the cover was
                // produced while the link was down (or lost its ready race),
                // the media snapshot is the reliable moment to retry it.
                val transfer = DashCueNotificationListenerService.artworkTransfer.value
                if (transfer != null &&
                    transfer.artworkId == state.artworkId.toLong() &&
                    transfer.artworkId != lastSentArtworkId
                ) {
                    log("ARTWORK resend for media snapshot ${state.artworkId}")
                    sendArtworkWithFallback(mgr, transfer)
                }
            }
    }

    private suspend fun observeNavigation(mgr: DashCueBleManager) {
        DashCueNotificationListenerService.navEvents.collect { event ->
            if (event.removed) {
                if (event.sbn.key == lastMirrorKey) {
                    lastMirrorKey = null
                    lastMirroredSbn = null
                    lastMirrorNav = null
                    _navSource.value = null
                    val speed = currentSpeedKmhX10()
                    val loc = lastLocation ?: lastKnownLocation()
                    val heading = currentHeadingDegrees(loc)
                    val accuracy = if (loc != null && loc.hasAccuracy()) loc.accuracy else 0.0f
                    val street = cachedGeocodedStreet.ifBlank { "Driving Standby" }
                    val clearNav = NavState.newBuilder()
                        .setStatus(NavStatus.NAV_NONE)
                        .setSpeedKmhX10(speed)
                        .setHeadingDeg(heading)
                        .setGpsAccuracyM(accuracy)
                        .setStreet(street)
                        .setSpeedLimitKmh(lastKnownSpeedLimitKmh)
                        .setPhoneTsMs(System.currentTimeMillis())
                        .build()
                    _mirroredNav.value = clearNav
                    sendToHud(
                        mgr,
                        MessageType.MT_NAV_STATE.number,
                        System.currentTimeMillis(),
                        clearNav.toByteArray(),
                        "NAV_STATE (STANDBY)"
                    )
                }
                return@collect
            }

            val parsed = try {
                NavNotificationParser.parse(context, event.sbn)
            } catch (e: Exception) {
                log("NAV mirror parse failed: ${e.message}")
                null
            } ?: return@collect

            lastMirrorKey = event.sbn.key
            lastMirroredSbn = event.sbn
            lastMirrorAtMs = System.currentTimeMillis()
            _navSource.value = parsed.sourceLabel
            if (parsed.speedLimitKmh > 0) {
                lastKnownSpeedLimitKmh = parsed.speedLimitKmh
            }

            val speed = currentSpeedKmhX10()
            val loc = lastLocation ?: lastKnownLocation()
            val heading = currentHeadingDegrees(loc)
            val accuracy = if (loc != null && loc.hasAccuracy()) loc.accuracy else 0.0f
            val isArrival = parsed.maneuver == Maneuver.MV_ARRIVE ||
                parsed.maneuver == Maneuver.MV_ARRIVE_LEFT ||
                parsed.maneuver == Maneuver.MV_ARRIVE_RIGHT ||
                (parsed.instruction.contains("destination", ignoreCase = true) &&
                 (parsed.distanceToNextM <= 50 || parsed.remainingM <= 50)) ||
                (parsed.distanceToNextM == 0 && parsed.remainingM == 0 &&
                 parsed.instruction.contains("arrived", ignoreCase = true))

            val navStatus = if (isArrival) NavStatus.NAV_ARRIVED else NavStatus.NAV_ACTIVE
            val mirrored = NavState.newBuilder()
                .setRouteId(parsed.notificationKey.hashCode() and 0x7fffffff)
                .setStatus(navStatus)
                .setNext(parsed.maneuver)
                .setDistToNextM(parsed.distanceToNextM)
                .setStreet(parsed.street)
                .setRoundaboutExit(parsed.roundaboutExit)
                .setRemainingM(parsed.remainingM)
                .setRemainingS(parsed.remainingS)
                .setSpeedKmhX10(speed)
                .setHeadingDeg(heading)
                .setGpsAccuracyM(accuracy)
                .setHasLiveTraffic(parsed.trafficIsExplicit)
                .setTraffic(parsed.traffic)
                .setEtaEpochS(parsed.etaEpochS.toInt())
                .setSpeedLimitKmh(parsed.speedLimitKmh)
                .setSourceApp(parsed.sourceLabel)
                .setPhoneTsMs(parsed.updatedAtMs)
                .build()
            lastMirrorNav = mirrored
            _mirroredNav.value = mirrored
            mgr.setReportedSpeed(speed)
            log("NAV mirror ${parsed.sourceLabel}: ${parsed.instruction} / ${parsed.distanceToNextM} m")
            sendToHud(
                mgr,
                MessageType.MT_NAV_STATE.number,
                System.currentTimeMillis(),
                mirrored.toByteArray(),
                "NAV_STATE(live)"
            )
        }
    }

    private suspend fun observeArtwork(mgr: DashCueBleManager) {
        DashCueNotificationListenerService.artworkTransfer
            .filterNotNull()
            .collectLatest { transfer ->
                sendArtworkWithFallback(mgr, transfer)
            }
    }

    private suspend fun sendArtworkWithFallback(
        mgr: DashCueBleManager,
        transfer: Rgb565ArtworkConverter.PreparedArtwork
    ) = artworkMutex.withLock {
        if (mgr.stateMachine.currentState != ReconnectionStateMachine.State.READY) {
            log("ARTWORK ${transfer.artworkId} deferred: link not READY")
            return@withLock
        }
        if (lastSentArtworkId == transfer.artworkId) {
            // A snapshot/media tick can ask again while a completed transfer is
            // still the current cover; resending 8 KB every second is noise.
            return@withLock
        }

        // Tier 1: 64x64 RGB565 transfer
        val tier1Success = transmitArtworkPrepared(mgr, transfer, paceMs = 16L, maxRetries = 1)
        if (tier1Success) {
            lastSentArtworkId = transfer.artworkId
            log("TX ARTWORK ${transfer.artworkId} OK (Tier 1: 64x64, ${transfer.chunks.size} chunks)")
            return@withLock
        }

        // Tier 2 Fallback: 32x32 RGB565 thumbnail
        val bitmap = DashCueNotificationListenerService.artwork.value
        if (bitmap != null) {
            try {
                log("TX ARTWORK Tier 1 timeout -> Fallback to Tier 2: 32x32 thumbnail")
                val thumbTransfer = Rgb565ArtworkConverter.convertBitmapToRgb565(
                    bitmap,
                    transfer.artworkId,
                    targetWidth = 32,
                    targetHeight = 32
                )
                val tier2Success = transmitArtworkPrepared(mgr, thumbTransfer, paceMs = 20L, maxRetries = 1)
                if (tier2Success) {
                    lastSentArtworkId = transfer.artworkId
                    log("TX ARTWORK ${transfer.artworkId} OK (Tier 2: 32x32 thumbnail fallback)")
                    return@withLock
                }
            } catch (e: Exception) {
                log("Tier 2 thumbnail transmission error: ${e.message}")
            }
        }

        // Tier 3: Backdrop gradient fallback on HUD
        log("TX ARTWORK Fallback Tier 3: Dynamic backdrop active")
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
                delay(paceMs)
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
                    delay(paceMs * 2)
                }
                if (!chunkOk) return false
                delay(paceMs)
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
                delay(paceMs)
            }
            return endOk
        } catch (e: Exception) {
            log("Artwork transmission error: ${e.message}")
            return false
        }
    }

    private suspend fun observeSettings(mgr: DashCueBleManager) {
        prefs.userSettingsFlow.drop(1).debounce(300).collect {
            sendSettingsToHud(mgr, it)
        }
    }

    private suspend fun startLivenessTicker(mgr: DashCueBleManager) {
        var tickCount = 0L
        while (currentCoroutineContext().isActive) {
            delay(1000)
            tickCount++

            if (tickCount % 2 == 0L) {
                DashCueNotificationListenerService.instance?.refreshMediaState()
                DashCueNotificationListenerService.instance?.syncNavigationState()
            }

            val last = lastMirrorNav
            if (last != null && last.status != NavStatus.NAV_NONE) {
                val nowS = System.currentTimeMillis() / 1000
                val remaining = (last.etaEpochS.toLong() - nowS).coerceIn(0L, 7L * 24 * 3600).toInt()
                val speed = currentSpeedKmhX10()
                val loc = lastLocation ?: lastKnownLocation()
                val heading = currentHeadingDegrees(loc).takeIf { it >= 0f } ?: last.headingDeg
                val accuracy = if (loc != null && loc.hasAccuracy()) loc.accuracy else last.gpsAccuracyM
                val updated = last.toBuilder()
                    .setRemainingS(remaining)
                    .setSpeedKmhX10(speed)
                    .setHeadingDeg(heading)
                    .setGpsAccuracyM(accuracy)
                    .setPhoneTsMs(System.currentTimeMillis())
                    .build()
                lastMirrorNav = updated
                _mirroredNav.value = updated
                mgr.setReportedSpeed(speed)
                sendToHud(mgr, MessageType.MT_NAV_STATE.number, System.currentTimeMillis(), updated.toByteArray(), "NAV_STATE(heartbeat)")
            } else {
                // Standby driving telemetry at 1 Hz (R5)
                val speed = currentSpeedKmhX10()
                val loc = lastLocation ?: lastKnownLocation()
                val heading = currentHeadingDegrees(loc)
                val accuracy = if (loc != null && loc.hasAccuracy()) loc.accuracy else 0.0f
                val street = cachedGeocodedStreet.ifBlank { "Driving Standby" }
                val standbyNav = NavState.newBuilder()
                    .setStatus(NavStatus.NAV_NONE)
                    .setSpeedKmhX10(speed)
                    .setHeadingDeg(heading)
                    .setGpsAccuracyM(accuracy)
                    .setStreet(street)
                    .setSpeedLimitKmh(lastKnownSpeedLimitKmh)
                    .setPhoneTsMs(System.currentTimeMillis())
                    .build()
                _mirroredNav.value = standbyNav
                mgr.setReportedSpeed(speed)
                sendToHud(
                    mgr,
                    MessageType.MT_NAV_STATE.number,
                    System.currentTimeMillis(),
                    standbyNav.toByteArray(),
                    "NAV_STATE(standby 1Hz)"
                )
            }
        }
    }

    suspend fun sendSnapshotToHud(mgr: DashCueBleManager? = activeManager) {
        val manager = mgr ?: return
        DashCueNotificationListenerService.currentMediaState.value?.let {
            sendToHud(manager, MessageType.MT_MEDIA_STATE.number, System.currentTimeMillis(), it.toByteArray(), "MEDIA_STATE(snapshot)")
        }
        val currentNav = _mirroredNav.value ?: run {
            val speed = currentSpeedKmhX10()
            val loc = lastLocation ?: lastKnownLocation()
            val heading = currentHeadingDegrees(loc)
            val accuracy = if (loc != null && loc.hasAccuracy()) loc.accuracy else 0.0f
            val street = cachedGeocodedStreet.ifBlank { "Driving Standby" }
            NavState.newBuilder()
                .setStatus(NavStatus.NAV_NONE)
                .setSpeedKmhX10(speed)
                .setHeadingDeg(heading)
                .setGpsAccuracyM(accuracy)
                .setStreet(street)
                .setSpeedLimitKmh(lastKnownSpeedLimitKmh)
                .setPhoneTsMs(System.currentTimeMillis())
                .build()
        }
        sendToHud(manager, MessageType.MT_NAV_STATE.number, System.currentTimeMillis(), currentNav.toByteArray(), "NAV_STATE(snapshot)")
        prefs.userSettingsFlow.firstOrNull()?.let {
            sendSettingsToHud(manager, it)
        }
        DashCueNotificationListenerService.artworkTransfer.value?.let {
            sendArtworkWithFallback(manager, it)
        }
    }

    suspend fun sendSettingsToHud(mgr: DashCueBleManager? = activeManager, value: UserSettings) {
        val manager = mgr ?: return
        val data = SettingsData.newBuilder()
            .setBrightnessPct(value.brightnessPct.coerceIn(10, 100))
            .setTheme(ThemeMode.forNumber(value.themeMode) ?: ThemeMode.TH_AUTO)
            .setOrientation(ScreenOrientation.forNumber(value.orientation) ?: ScreenOrientation.SO_LANDSCAPE)
            .setSeekStepS(if (value.seekStepS == 30) 30 else 10)
            .setUnits(UnitSystem.forNumber(value.units) ?: UnitSystem.UN_METRIC)
            .setVersion((settingsVersion++).toInt())
            .build()
        val id = commandId++
        val message = SettingsSet.newBuilder().setCommandId(id.toInt()).setSettings(data).build()
        sendToHud(manager, MessageType.MT_SETTINGS_SET.number, id, message.toByteArray(), "SETTINGS_SET")
    }

    private suspend fun sendToHud(
        mgr: DashCueBleManager,
        msgType: Int,
        msgId: Long,
        payload: ByteArray,
        label: String
    ): Boolean {
        if (mgr.stateMachine.currentState != ReconnectionStateMachine.State.READY) return false
        val ok = try {
            mgr.sendMessage(msgType, msgId, 0, payload)
        } catch (e: Exception) {
            log("TX $label failed: ${e.message}")
            false
        }
        log(if (ok) "TX $label (${payload.size} B)" else "TX $label FAILED")
        return ok
    }

    /**
     * GPS course when the receiver has one, magnetic heading when it does not,
     * and -1 when neither is trustworthy. The HUD renders -1 as \"--°\" rather
     * than a false north arrow.
     */
    private fun currentHeadingDegrees(loc: android.location.Location?): Float {
        if (loc != null && loc.hasBearing()) return loc.bearing
        return HeadingProvider.currentAzimuth() ?: -1f
    }

    fun currentSpeedKmhX10(): Int {
        val location = lastLocation ?: lastKnownLocation() ?: return -1
        if (!location.hasSpeed()) return -1
        return (location.speed * 3.6f * 10f).toInt().coerceIn(0, 4000)
    }

    /** Clears the mirrored nav and posts a NAV_NONE to the HUD. */
    fun clearMirroredNav() {
        lastMirrorKey = null
        lastMirroredSbn = null
        lastMirrorNav = null
        _mirroredNav.value = null
        _navSource.value = null
        val speed = currentSpeedKmhX10()
        val loc = lastLocation ?: lastKnownLocation()
        val heading = currentHeadingDegrees(loc)
        val accuracy = if (loc != null && loc.hasAccuracy()) loc.accuracy else 0.0f
        val street = cachedGeocodedStreet.ifBlank { "Driving Standby" }
        val clearNav = NavState.newBuilder()
            .setStatus(NavStatus.NAV_NONE)
            .setSpeedKmhX10(speed)
            .setHeadingDeg(heading)
            .setGpsAccuracyM(accuracy)
            .setStreet(street)
            .setSpeedLimitKmh(lastKnownSpeedLimitKmh)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()
        activeManager?.let { mgr ->
            scope.launch {
                sendToHud(
                    mgr,
                    MessageType.MT_NAV_STATE.number,
                    System.currentTimeMillis(),
                    clearNav.toByteArray(),
                    "NAV_STATE (STANDBY)"
                )
            }
        }
    }

    fun mirrorAgeMs(): Long =
        if (lastMirrorAtMs == 0L) -1L else (System.currentTimeMillis() - lastMirrorAtMs).coerceAtLeast(0L)

    fun clearLog() {
        _protocolLog.value = emptyList()
    }

    /** Sends a DIAG_REQ for the full status snapshot; the HUD answers with DIAG_RESP. */
    fun requestDiagnostics() {
        val mgr = activeManager ?: return
        scope.launch {
            val id = commandId++
            val request = DiagReq.newBuilder().setCommandId(id.toInt()).setWant(1).build()
            sendToHud(mgr, MessageType.MT_DIAG_REQ.number, id, request.toByteArray(), "DIAG_REQ")
        }
    }

    private fun lastKnownLocation(): Location? {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) !=
            PackageManager.PERMISSION_GRANTED
        ) return null
        val lm = context.getSystemService(LocationManager::class.java) ?: return null
        return try {
            lm.getLastKnownLocation(LocationManager.GPS_PROVIDER)
                ?: lm.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
        } catch (e: SecurityException) {
            null
        }
    }
}
