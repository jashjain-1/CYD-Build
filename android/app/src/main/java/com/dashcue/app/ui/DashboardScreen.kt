package com.dashcue.app.ui

import android.content.Intent
import android.os.SystemClock
import android.provider.Settings
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.ui.graphics.Brush
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.FastRewind
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dashcue.app.DashCueViewModel
import com.dashcue.feature.media.AlbumColorExtractor
import com.dashcue.v1.MediaState
import com.dashcue.v1.PlaybackStatus
import com.dashcue.core.ble.ReconnectionStateMachine
import kotlinx.coroutines.delay

@Composable
fun DashboardScreen(
    vm: DashCueViewModel,
    onPairRequested: () -> Unit
) {
    val context = LocalContext.current
    val connection by vm.connectionState.collectAsStateWithLifecycle()
    val media by vm.mediaState.collectAsStateWithLifecycle()
    val artwork by vm.artwork.collectAsStateWithLifecycle()
    val nav by vm.navState.collectAsStateWithLifecycle()
    val telemetry by vm.telemetry.collectAsStateWithLifecycle()
    val settings by vm.settings.collectAsStateWithLifecycle()
    val locked by vm.drivingLock.collectAsStateWithLifecycle()

    LazyColumn(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 16.dp)
    ) {
        item {
            ConnectionCard(
                state = connection,
                mtu = telemetry.mtu,
                rssiDbm = telemetry.rssiDbm,
                packetsIn = telemetry.packetsIn,
                packetsOut = telemetry.packetsOut,
                notificationAccess = vm.hasNotificationAccess(),
                onPair = onPairRequested,
                onDisconnect = { vm.forgetPairedDevice() },
                onOpenNotificationAccess = {
                    context.startActivity(
                        Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    )
                }
            )
        }

        item {
            MediaCard(
                media = media,
                artwork = artwork,
                onToggle = vm::togglePlayback,
                onNext = vm::skipNext,
                onPrevious = vm::skipPrevious,
                onSeekTo = vm::seekTo,
                onVolume = vm::setVolume
            )
        }

        item {
            NavigationGlanceCard(
                nav = nav,
                imperial = settings.units == 1,
                onEnd = vm::endNavigation,
                onOpenMaps = vm::openGoogleMaps
            )
        }

        item {
            DashCard {
                SectionLabel("Safety")
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            if (locked) "Driving lock active" else "Driving lock idle",
                            color = if (locked) DashCueColors.Warn else DashCueColors.Text,
                            style = MaterialTheme.typography.titleMedium
                        )
                        Text(
                            "The HUD settings buttons lock while the phone reports motion. " +
                                "It is a design aid, not a road-safety certification.",
                            color = DashCueColors.TextDim,
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                    StatusPill(
                        if (locked) "Locked" else "Ready",
                        if (locked) DashCueColors.Warn else DashCueColors.Ok
                    )
                }
            }
        }
    }
}

@Composable
private fun ConnectionCard(
    state: ReconnectionStateMachine.State,
    mtu: Int,
    rssiDbm: Int,
    packetsIn: Long,
    packetsOut: Long,
    notificationAccess: Boolean,
    onPair: () -> Unit,
    onDisconnect: () -> Unit,
    onOpenNotificationAccess: () -> Unit
) {
    val healthy = connectionIsHealthy(state)
    DashCard {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                SectionLabel("HUD link")
                Text(
                    connectionLabel(state),
                    style = MaterialTheme.typography.headlineMedium,
                    color = DashCueColors.Text
                )
            }
            StatusPill(
                if (healthy) "Connected" else connectionLabel(state),
                if (healthy) DashCueColors.Ok else DashCueColors.Warn
            )
        }

        MetricRow("MTU", if (mtu > 0) "$mtu B" else "—")
        MetricRow(
            "Signal",
            if (rssiDbm == 0) "—" else "$rssiDbm dBm · ${signalBars(rssiDbm)}/4"
        )
        MetricRow("Frames", "in $packetsIn · out $packetsOut")

        if (!notificationAccess) {
            Text(
                "Media control needs notification access. Pairing alone is not enough.",
                color = DashCueColors.Warn,
                style = MaterialTheme.typography.bodySmall
            )
            OutlinedButton(
                onClick = onOpenNotificationAccess,
                modifier = Modifier.fillMaxWidth()
            ) { Text("Open notification access") }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(
                onClick = onPair,
                colors = ButtonDefaults.buttonColors(
                    containerColor = DashCueColors.Accent,
                    contentColor = DashCueColors.Bg
                )
            ) { Text("Pair HUD") }
            OutlinedButton(
                onClick = onDisconnect,
                enabled = state != ReconnectionStateMachine.State.DISCONNECTED
            ) { Text("Disconnect") }
        }
    }
}

@Composable
private fun MediaCard(
    media: MediaState?,
    artwork: android.graphics.Bitmap?,
    onToggle: () -> Unit,
    onNext: () -> Unit,
    onPrevious: () -> Unit,
    onSeekTo: (Long) -> Unit,
    onVolume: (Int) -> Unit
) {
    var nowMs by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(media?.snapshotVersion, media?.status) {
        while (true) {
            nowMs = SystemClock.elapsedRealtime()
            delay(1000)
        }
    }

    val position = projectedPositionMs(media, nowMs)
    val duration = media?.durationMs?.coerceAtLeast(0L) ?: 0L
    val fraction = if (duration > 0) (position.toFloat() / duration).coerceIn(0f, 1f) else 0f
    val playing = media?.status == PlaybackStatus.PB_PLAYING
    val hasSession = media != null && media.status != PlaybackStatus.PB_NO_SESSION
    var volume by remember { mutableFloatStateOf((media?.volumePct ?: 70) / 100f) }
    var scrubFraction by remember(media?.snapshotVersion, media?.title) { mutableFloatStateOf(-1f) }
    val shownFraction = if (scrubFraction >= 0f) scrubFraction else fraction

    // Spotify/Android-Auto-style theming: tint this card's accent surfaces from
    // the album art so the phone matches the HUD. Falls back to the brand cyan
    // when there is no art or the art is grey.
    val albumAccent by remember(artwork) {
        mutableLongStateOf(
            (AlbumColorExtractor.extractDominantRgb(artwork)?.toLong() ?: 0L)
        )
    }
    // Lift the album hue toward white before using it as small UI on the dark
    // card: a saturated colour can have low luminance (pure red tops out near
    // L=0.21) and would fail AA at 12 px. The firmware applies the same blend.
    val accent = if (albumAccent != 0L) {
        Color(AlbumColorExtractor.liftForDarkSurface(albumAccent.toInt()) or 0xFF000000.toInt())
    } else {
        DashCueColors.Accent
    }

    val cardModifier = if (albumAccent != 0L) {
        val topTint = Color(albumAccent.toInt() or 0xFF000000.toInt()).copy(alpha = 0.35f)
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(
                Brush.verticalGradient(
                    colors = listOf(topTint, DashCueColors.Surface)
                )
            )
            .border(1.dp, DashCueColors.Border, RoundedCornerShape(12.dp))
    } else {
        Modifier
    }

    DashCard(
        modifier = cardModifier,
        containerColor = if (albumAccent != 0L) Color.Transparent else DashCueColors.Surface
    ) {
        SectionLabel("Now playing")
        Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            Box(
                Modifier
                    .size(108.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(DashCueColors.Surface2),
                contentAlignment = Alignment.Center
            ) {
                if (artwork != null) {
                    Image(
                        bitmap = artwork.asImageBitmap(),
                        contentDescription = "Album artwork",
                        contentScale = ContentScale.Crop,
                        modifier = Modifier.size(108.dp)
                    )
                } else {
                    Icon(
                        Icons.Filled.MusicNote,
                        contentDescription = null,
                        tint = DashCueColors.Accent,
                        modifier = Modifier.size(42.dp)
                    )
                }
            }

            Column(
                Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(3.dp)
            ) {
                Text(
                    media?.title?.takeIf { it.isNotBlank() } ?: "No media playing",
                    style = MaterialTheme.typography.titleLarge,
                    color = DashCueColors.Text,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    media?.artist?.takeIf { it.isNotBlank() } ?: "Start a player on the phone",
                    color = DashCueColors.TextDim,
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                if (media?.appName?.isNotBlank() == true) {
                    Text(
                        media!!.appName,
                        color = accent,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Medium
                    )
                }
                Spacer(Modifier.height(2.dp))
                Slider(
                    value = shownFraction,
                    onValueChange = { scrubFraction = it },
                    onValueChangeFinished = {
                        if (scrubFraction >= 0f && duration > 0) {
                            onSeekTo((scrubFraction * duration).toLong())
                        }
                        scrubFraction = -1f
                    },
                    enabled = hasSession && duration > 0,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(24.dp),
                    colors = SliderDefaults.colors(
                        thumbColor = accent,
                        activeTrackColor = accent,
                        inactiveTrackColor = DashCueColors.Surface2
                    )
                )
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        formatClock(if (scrubFraction >= 0f) (scrubFraction * duration).toLong() else position),
                        color = DashCueColors.TextDim,
                        fontSize = 11.sp
                    )
                    Text(formatClock(duration), color = DashCueColors.TextDim, fontSize = 11.sp)
                }
            }
        }

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceEvenly,
            verticalAlignment = Alignment.CenterVertically
        ) {
            TransportButton(Icons.Filled.SkipPrevious, "Previous track", onPrevious, enabled = hasSession)
            TransportButton(
                if (playing) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                if (playing) "Pause" else "Play",
                onToggle,
                enabled = true,
                accent = true,
                accentColor = accent
            )
            TransportButton(Icons.Filled.SkipNext, "Next track", onNext, enabled = hasSession)
        }

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Volume", color = DashCueColors.TextDim, fontSize = 12.sp)
            Spacer(Modifier.width(12.dp))
            Slider(
                value = volume,
                onValueChange = { value ->
                    volume = value
                    onVolume((value * 100).toInt())
                },
                modifier = Modifier.weight(1f),
                colors = SliderDefaults.colors(
                    thumbColor = accent,
                    activeTrackColor = accent,
                    inactiveTrackColor = DashCueColors.Surface2
                )
            )
        }
    }
}

@Composable
private fun NavigationGlanceCard(
    nav: com.dashcue.v1.NavState?,
    imperial: Boolean,
    onEnd: () -> Unit,
    onOpenMaps: () -> Unit
) {
    DashCard {
        SectionLabel("Navigation")
        if (nav == null || nav.status == com.dashcue.v1.NavStatus.NAV_NONE) {
            Text("No active route", color = DashCueColors.Text, style = MaterialTheme.typography.titleMedium)
            Text(
                "Open the Navigation tab to search for a destination or launch Google Maps directly.",
                color = DashCueColors.TextDim,
                style = MaterialTheme.typography.bodySmall
            )
            Spacer(Modifier.height(8.dp))
            Button(
                onClick = onOpenMaps,
                colors = ButtonDefaults.buttonColors(
                    containerColor = DashCueColors.Accent,
                    contentColor = DashCueColors.Bg
                )
            ) {
                Text("Launch Google Maps")
            }
            return@DashCard
        }

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                Modifier
                    .size(64.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(DashCueColors.Accent.copy(alpha = 0.16f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    maneuverIcon(nav.next),
                    contentDescription = maneuverLabel(nav.next),
                    tint = DashCueColors.Accent,
                    modifier = Modifier.size(40.dp)
                )
            }
            Column(Modifier.weight(1f)) {
                Text(
                    maneuverLabel(nav.next).uppercase(),
                    color = DashCueColors.Accent,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 1.sp
                )
                Text(
                    formatDistance(nav.distToNextM, imperial),
                    style = MaterialTheme.typography.headlineMedium,
                    color = DashCueColors.Text
                )
                Text(
                    nav.street.ifBlank { "Follow route" },
                    color = DashCueColors.TextDim,
                    style = MaterialTheme.typography.bodyMedium,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    remainingLabel(nav.remainingS),
                    color = DashCueColors.TextDim,
                    fontSize = 12.sp
                )
                Text(
                    formatDistance(nav.remainingM, imperial),
                    color = DashCueColors.TextDim,
                    fontSize = 12.sp
                )
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            StatusPill(nav.status.name.removePrefix("NAV_"), DashCueColors.Accent)
            OutlinedButton(onClick = onEnd) { Text("End navigation") }
        }
    }
}
