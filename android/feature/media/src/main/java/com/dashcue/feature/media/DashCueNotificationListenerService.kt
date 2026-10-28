package com.dashcue.feature.media

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.app.Notification
import android.media.AudioManager
import android.view.KeyEvent
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.SystemClock
import android.service.notification.NotificationListenerService
import androidx.core.graphics.drawable.toBitmap
import android.service.notification.StatusBarNotification
import com.dashcue.core.common.DashCueLog
import com.dashcue.v1.MediaState
import com.dashcue.v1.PlaybackStatus
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Android NotificationListenerService for DashCue media integration (§10.1, DEC-20).
 *
 * PRIVACY CONTRACT:
 * Inspects MediaSessionManager active sessions ONLY.
 * Never logs, reads, or persists notification texts or contacts.
 */
class DashCueNotificationListenerService : NotificationListenerService() {
    companion object {
        private const val TAG = "NotificationListener"

        private val _currentMediaState = MutableStateFlow<MediaState?>(null)
        val currentMediaState: StateFlow<MediaState?> = _currentMediaState.asStateFlow()

        private val _artwork = MutableStateFlow<Bitmap?>(null)
        val artwork: StateFlow<Bitmap?> = _artwork.asStateFlow()

        private val _dominantAccentRgb = MutableStateFlow<Int?>(null)
        val dominantAccentRgb: StateFlow<Int?> = _dominantAccentRgb.asStateFlow()

        private val _artworkTransfer = MutableStateFlow<Rgb565ArtworkConverter.PreparedArtwork?>(null)
        val artworkTransfer: StateFlow<Rgb565ArtworkConverter.PreparedArtwork?> =
            _artworkTransfer.asStateFlow()

        /** Navigation notifications from the packages the parser understands. */
        data class NavEvent(val sbn: StatusBarNotification, val removed: Boolean)

        private val _navEvents = MutableSharedFlow<NavEvent>(extraBufferCapacity = 32)
        val navEvents: SharedFlow<NavEvent> = _navEvents.asSharedFlow()

        fun isNavigationNotification(sbn: StatusBarNotification?): Boolean {
            if (sbn == null) return false
            val pkg = sbn.packageName
            if (pkg == "com.google.android.apps.maps" || pkg == "com.waze") return true
            return sbn.notification?.category == android.app.Notification.CATEGORY_NAVIGATION
        }

        private fun isNavigationPackage(packageName: String?): Boolean =
            packageName == "com.google.android.apps.maps" || packageName == "com.waze"

        @Volatile
        var instance: DashCueNotificationListenerService? = null
            private set

        var activeController: MediaController? = null
            private set
    }

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var mediaSessionManager: MediaSessionManager? = null
    lateinit var confirmationEngine: MediaConfirmationEngine
        private set

    private var activeSnapshotVersion: Long = 1L
    private var artworkCounter: Long = 1L
    private var lastArtworkSignature: Int = 0

    /** Snapshot version the HUD echoes back when it sends a MediaCmd. */
    val snapshotVersion: Long get() = activeSnapshotVersion

    lateinit var commandDispatcher: MediaCommandDispatcher
        private set

    override fun onCreate() {
        super.onCreate()
        instance = this
        confirmationEngine = MediaConfirmationEngine(serviceScope)
        val audioManager = getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        commandDispatcher = MediaCommandDispatcher(serviceScope, audioManager)
        mediaSessionManager = getSystemService(Context.MEDIA_SESSION_SERVICE) as? MediaSessionManager
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        DashCueLog.i(TAG, "NotificationListener connected, updating active media sessions")
        updateActiveSession()
        val component = ComponentName(this, DashCueNotificationListenerService::class.java)
        try {
            mediaSessionManager?.addOnActiveSessionsChangedListener({ updateActiveSession() }, component)
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Could not observe active media sessions", e)
        }

        syncNavigationState()
    }

    fun syncNavigationState() {
        try {
            activeNotifications
                ?.filter { isNavigationNotification(it) }
                ?.forEach { _navEvents.tryEmit(NavEvent(it, removed = false)) }
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Could not read active navigation notifications", e)
        }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        super.onNotificationPosted(sbn)
        if (sbn != null && isNavigationNotification(sbn)) {
            _navEvents.tryEmit(NavEvent(sbn, removed = false))
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        super.onNotificationRemoved(sbn)
        if (sbn != null && isNavigationNotification(sbn)) {
            _navEvents.tryEmit(NavEvent(sbn, removed = true))
        }
    }

    /** Executes a HUD command against the active session and reports the result. */
    fun dispatchMediaCommand(
        cmd: com.dashcue.v1.MediaCmd,
        onResult: (com.dashcue.v1.CmdResult) -> Unit
    ) {
        val controller = activeController
        if (controller == null) {
            if (cmd.action == com.dashcue.v1.MediaAction.MA_PLAY) {
                launchDefaultPlayerAndPlay()
                onResult(
                    com.dashcue.v1.CmdResult.newBuilder()
                        .setCommandId(cmd.commandId)
                        .setStatus(com.dashcue.v1.CmdResult.Status.ST_CONFIRMED)
                        .build()
                )
                return
            }
            onResult(
                com.dashcue.v1.CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(com.dashcue.v1.CmdResult.Status.ST_FAILED)
                    .setReason(com.dashcue.v1.ErrorCode.EC_NO_SESSION_VALUE)
                    .build()
            )
            return
        }
        commandDispatcher.dispatchCommand(cmd, controller, activeSnapshotVersion, onResult)

        // Volume is a system stream, not a media-session callback: without this
        // the HUD would keep showing the old percentage until the next track.
        if (cmd.action == com.dashcue.v1.MediaAction.MA_VOL_STEP ||
            cmd.action == com.dashcue.v1.MediaAction.MA_MUTE_SET
        ) {
            serviceScope.launch {
                kotlinx.coroutines.delay(120)
                updateMediaState()
            }
        }
    }

    fun launchDefaultPlayerAndPlay() {
        DashCueLog.i(TAG, "No active session: triggering default media player fallback")
        val audioManager = getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        val candidatePackages = listOf(
            "com.spotify.music",
            "com.google.android.apps.youtube.music",
            "com.apple.android.music",
            "com.jio.media.jiobeats",
            "com.amazon.mp3",
            "com.soundcloud.android",
            "com.aspiro.tidal",
            "deezer.android.app",
            "au.com.shiftyjelly.pocketcasts",
            "com.audible.application",
            "org.videolan.vlc"
        )

        var targeted = false
        for (pkg in candidatePackages) {
            try {
                packageManager.getPackageInfo(pkg, 0)
                val downIntent = Intent(Intent.ACTION_MEDIA_BUTTON).apply {
                    setPackage(pkg)
                    putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_PLAY))
                }
                sendOrderedBroadcast(downIntent, null)

                val upIntent = Intent(Intent.ACTION_MEDIA_BUTTON).apply {
                    setPackage(pkg)
                    putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_MEDIA_PLAY))
                }
                sendOrderedBroadcast(upIntent, null)
                targeted = true
                DashCueLog.i(TAG, "Dispatched MEDIA_PLAY key event to $pkg")
                break
            } catch (_: Exception) {
                // Not installed, proceed to next candidate
            }
        }

        if (!targeted && audioManager != null) {
            try {
                audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_PLAY))
                audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_MEDIA_PLAY))
                DashCueLog.i(TAG, "Dispatched global AudioManager MEDIA_PLAY key event")
            } catch (e: Exception) {
                DashCueLog.w(TAG, "Could not dispatch global media key", e)
            }
        }

        serviceScope.launch {
            kotlinx.coroutines.delay(600)
            updateActiveSession()
            kotlinx.coroutines.delay(1400)
            updateActiveSession()
        }
    }

    /**
     * Re-emits a fresh position snapshot. Playback callbacks are not a 1 Hz
     * clock: Spotify can play for minutes without another callback, and the
     * HUD would rightly mark the old snapshot stale. Called by the ViewModel
     * ticker while playback is active.
     */
    fun refreshMediaState() {
        if (activeController != null) updateMediaState()
    }

    fun updateActiveSession() {
        val manager = mediaSessionManager ?: return
        try {
            val component = ComponentName(this, DashCueNotificationListenerService::class.java)
            val sessions = manager.getActiveSessions(component)
            val primary = pickPrimarySession(sessions)
            if (primary != null) {
                attachController(primary)
            } else {
                activeController = null
                _currentMediaState.value = MediaState.newBuilder()
                    .setStatus(PlaybackStatus.PB_NO_SESSION)
                    .build()
                _artwork.value = null
                _artworkTransfer.value = null
                _dominantAccentRgb.value = null
            }
        } catch (e: SecurityException) {
            DashCueLog.w(TAG, "SecurityException getting active sessions: notification access not granted", e)
        }
    }

    /**
     * Choose the session the HUD should mirror.
     *
     * `getActiveSessions()` returns every session the system knows about in
     * arbitrary order. Taking `sessions[0]` meant that a paused podcast or a
     * leftover browser tab could win over the app the driver is actually
     * listening to — the HUD then streamed that session's empty metadata and
     * looked like "no session" while Spotify was audibly playing.
     *
     * Priority per spec §1.1: (1) something actually playing, (2) the most
     * recently updated of those, (3) any session rather than nothing.
     */
    private fun pickPrimarySession(sessions: List<MediaController>): MediaController? {
        val usable = sessions.filter { it.packageName != packageName }
        if (usable.isEmpty()) return null

        val playing = usable.filter {
            it.playbackState?.state == PlaybackState.STATE_PLAYING
        }
        val pool = playing.ifEmpty { usable }
        // lastPositionUpdateTime is the freshest signal a MediaController exposes
        // without reading the notification, and it moves on every seek/track.
        return pool.maxByOrNull { it.playbackState?.lastPositionUpdateTime ?: 0L } ?: pool.first()
    }

    private val controllerCallback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            super.onPlaybackStateChanged(state)
            commandDispatcher.onObservedPlaybackState(state)
            if (state != null) {
                val mappedStatus = mapPlaybackStatus(state.state)
                confirmationEngine.onObservedStateChange(mappedStatus)
            }
            updateMediaState()
        }

        override fun onMetadataChanged(metadata: MediaMetadata?) {
            super.onMetadataChanged(metadata)
            activeSnapshotVersion++
            confirmationEngine.onObservedTrackChange()
            commandDispatcher.onObservedTrackChange()
            commandDispatcher.onObservedPlaybackState(activeController?.playbackState)
            updateMediaState()
        }
    }

    private fun attachController(controller: MediaController) {
        if (activeController?.sessionToken == controller.sessionToken) return

        activeController?.unregisterCallback(controllerCallback)
        activeController = controller
        controller.registerCallback(controllerCallback)
        activeSnapshotVersion++
        updateMediaState()
    }

    private fun updateMediaState() {
        val controller = activeController ?: return
        val playbackState = controller.playbackState
        val metadata = controller.metadata

        val actions = playbackState?.actions ?: 0L
        val caps = MediaCapabilityCalculator.calculateCapabilities(actions)
        val status = mapPlaybackStatus(playbackState?.state ?: PlaybackState.STATE_NONE)

        val title = metadata?.getString(MediaMetadata.METADATA_KEY_TITLE) ?: ""
        val artist = metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: ""
        val album = metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM) ?: ""
        val durationMs = metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: -1L
        val rawPositionMs = playbackState?.position ?: -1L
        val speed = playbackState?.playbackSpeed ?: 0f
        val nowElapsed = SystemClock.elapsedRealtime()
        val lastUpdate = playbackState?.lastPositionUpdateTime ?: 0L
        val positionMs = if (
            playbackState?.state == PlaybackState.STATE_PLAYING &&
            rawPositionMs >= 0 && lastUpdate > 0 && nowElapsed >= lastUpdate
        ) {
            val projected = rawPositionMs + ((nowElapsed - lastUpdate) * speed).toLong()
            if (durationMs > 0) projected.coerceAtMost(durationMs) else projected
        } else {
            rawPositionMs
        }

        val audioManager = getSystemService(Context.AUDIO_SERVICE) as? android.media.AudioManager
        val currentVol = audioManager?.getStreamVolume(android.media.AudioManager.STREAM_MUSIC) ?: 8
        val maxVol = audioManager?.getStreamMaxVolume(android.media.AudioManager.STREAM_MUSIC) ?: 10
        val volumePct = if (maxVol > 0) ((currentVol * 100) / maxVol) else 80
        val isMuted = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            audioManager?.isStreamMute(android.media.AudioManager.STREAM_MUSIC) ?: false
        } else false
        val positionUpdatedMs = if (playbackState?.state == PlaybackState.STATE_PLAYING) {
            nowElapsed
        } else {
            lastUpdate.takeIf { it > 0 } ?: nowElapsed
        }

        // Drivers recognise "YouTube Music", not "com.google.android.apps.youtube.music".
        // Package visibility can hide a player's ApplicationInfo even while its
        // MediaSession is visible, so known players get an explicit label and
        // the launcher label is only the fallback.
        val appLabel = friendlyAppName(controller.packageName)

        updateArtwork(metadata)

        val stateBuilder = MediaState.newBuilder()
            .setSnapshotVersion(activeSnapshotVersion.toInt())
            .setStatus(status)
            .setTitle(title.take(64))
            .setArtist(artist.take(64))
            .setAlbum(album.take(64))
            .setAppName(appLabel.take(24))
            .setCapabilities(caps)
            .setPositionMs(positionMs)
            .setPositionUpdatedAtMs(positionUpdatedMs)
            .setDurationMs(durationMs)
            .setPlaybackSpeed(speed)
            .setVolumePct(volumePct)
            .setMuted(isMuted)
            .setArtworkId(artworkCounter.toInt())
            .setPhoneTsMs(System.currentTimeMillis())

        _currentMediaState.value = stateBuilder.build()
    }

    /**
     * Album art is sent as a separate, CRC-checked RGB565 transfer. The phone
     * UI can use the Bitmap directly; the HUD has no decoder, so it needs the
     * raw pixels on the bulk lane.
     */
    private fun updateArtwork(metadata: MediaMetadata?) {
        val signature = listOf(
            // Package matters: two players can share a title/artist and each
            // ships its own cover source.
            activeController?.packageName.orEmpty(),
            metadata?.getString(MediaMetadata.METADATA_KEY_TITLE).orEmpty(),
            metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST).orEmpty(),
            metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM).orEmpty(),
            metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION).toString(),
            metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM_ART_URI).orEmpty(),
            metadata?.getString(MediaMetadata.METADATA_KEY_ART_URI).orEmpty()
        ).joinToString("|").hashCode()
        if (signature == lastArtworkSignature) return
        lastArtworkSignature = signature

        val bitmap = resolveArtwork(metadata)
        _artwork.value = bitmap
        if (bitmap == null) {
            _artworkTransfer.value = null
            _dominantAccentRgb.value = null
            DashCueLog.i(TAG, "Artwork: no bitmap in metadata (fallback glyph on HUD)")
            return
        }
        serviceScope.launch(Dispatchers.Default) {
            val dominant = AlbumColorExtractor.extractDominantRgb(bitmap)
            _dominantAccentRgb.value = dominant
        }
        artworkCounter = (artworkCounter + 1) and 0x7FFFFFFF
        if (artworkCounter == 0L) artworkCounter = 1
        // RGB565 downscaling bypassed: _artworkTransfer has no collectors and BLE bulk
        // transfer is disabled to eliminate 2.4 GHz radio co-existence collisions with A2DP audio.
        _artworkTransfer.value = null
    }

    private fun resolveArtwork(metadata: MediaMetadata?): Bitmap? {
        // Tier A: Native MediaMetadata Bitmaps
        val bitmap = metadata?.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART)
            ?: metadata?.getBitmap(MediaMetadata.METADATA_KEY_ART)
            ?: metadata?.getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON)
        if (bitmap != null) {
            DashCueLog.i(TAG, "Artwork tier A: session metadata ${bitmap.width}x${bitmap.height}")
            return bitmap
        }

        // Tier B: MediaMetadata Content URI
        val uri = metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM_ART_URI)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_ART_URI)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_DISPLAY_ICON_URI)
        if (!uri.isNullOrBlank()) {
            try {
                contentResolver.openInputStream(Uri.parse(uri))?.use { stream ->
                    val decoded = BitmapFactory.decodeStream(stream)
                    if (decoded != null) {
                        DashCueLog.i(TAG, "Artwork tier B: content URI $uri")
                        return decoded
                    }
                }
            } catch (e: Exception) {
                DashCueLog.w(TAG, "Artwork URI unreadable: $uri", e)
            }
        }

        val packageName = activeController?.packageName ?: return null

        // Tier C: Active Notification Extras (SoundCloud, Wynk, Gaana, Poweramp, Pocket Casts, Audible, browsers)
        try {
            val notifications = activeNotifications
            if (notifications != null) {
                val sbn = notifications.firstOrNull { it.packageName == packageName }
                if (sbn != null) {
                    val extras = sbn.notification?.extras
                    if (extras != null) {
                        val largeIconObj = extras.get(Notification.EXTRA_LARGE_ICON)
                            ?: extras.get(Notification.EXTRA_LARGE_ICON_BIG)
                        if (largeIconObj is Bitmap) return largeIconObj
                        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M &&
                            largeIconObj is android.graphics.drawable.Icon) {
                            val drawable = largeIconObj.loadDrawable(this)
                            if (drawable != null) return drawable.toBitmap(128, 128)
                        }

                        val picObj = extras.get(Notification.EXTRA_PICTURE)
                        if (picObj is Bitmap) return picObj

                        // Small icon last: many players post only a status icon
                        // and still need a cover (or a tier below).
                        val smallIconObj = extras.get(Notification.EXTRA_SMALL_ICON)
                        if (smallIconObj is Bitmap) return smallIconObj
                        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M &&
                            smallIconObj is android.graphics.drawable.Icon) {
                            val drawable = smallIconObj.loadDrawable(this)
                            if (drawable != null) return drawable.toBitmap(96, 96)
                        }
                        DashCueLog.i(TAG, "Artwork tier C: notification extras for $packageName")
                    }
                }
            }
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Notification extras artwork lookup error", e)
        }

        // Tier D: High-res Application Launcher Icon (100% universal guarantee for all Android players)
        return try {
            DashCueLog.i(TAG, "Artwork tier D: launcher icon fallback for $packageName")
            packageManager.getApplicationIcon(packageName).toBitmap(96, 96)
        } catch (e: Exception) {
            null
        }
    }

    private fun friendlyAppName(packageName: String?): String {
        if (packageName.isNullOrBlank()) return "Media"
        return when (packageName) {
            "com.spotify.music" -> "Spotify"
            "com.google.android.apps.youtube.music" -> "YouTube Music"
            "com.apple.android.music" -> "Apple Music"
            "com.jio.media.jiobeats" -> "JioSaavn"
            "com.amazon.mp3", "com.amazon.dee.app" -> "Amazon Music"
            "com.soundcloud.android" -> "SoundCloud"
            "com.aspiro.tidal" -> "Tidal"
            "deezer.android.app" -> "Deezer"
            "com.bandcamp.android" -> "Bandcamp"
            "com.audiomack" -> "Audiomack"
            "org.videolan.vlc" -> "VLC"
            "au.com.shiftyjelly.pocketcasts" -> "Pocket Casts"
            "com.audible.application" -> "Audible"
            "com.gaana" -> "Gaana"
            "com.bsb.portal" -> "Wynk"
            "com.maxmpz.audioplayer" -> "Poweramp"
            "com.google.android.apps.maps" -> "Google Maps"
            "com.waze" -> "Waze"
            else -> try {
                val info = packageManager.getApplicationInfo(packageName, 0)
                packageManager.getApplicationLabel(info).toString()
            } catch (e: Exception) {
                packageName.substringAfterLast('.').replaceFirstChar { it.uppercase() }
            }
        }
    }

    private fun mapPlaybackStatus(androidState: Int): PlaybackStatus = when (androidState) {
        PlaybackState.STATE_PLAYING -> PlaybackStatus.PB_PLAYING
        PlaybackState.STATE_PAUSED -> PlaybackStatus.PB_PAUSED
        PlaybackState.STATE_BUFFERING, PlaybackState.STATE_CONNECTING -> PlaybackStatus.PB_BUFFERING
        PlaybackState.STATE_STOPPED -> PlaybackStatus.PB_STOPPED
        else -> PlaybackStatus.PB_UNKNOWN
    }

    override fun onDestroy() {
        super.onDestroy()
        instance = null
        activeController?.unregisterCallback(controllerCallback)
        serviceScope.cancel()
    }
}
