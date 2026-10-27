package com.dashcue.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.CallMerge
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ForkLeft
import androidx.compose.material.icons.filled.ForkRight
import androidx.compose.material.icons.filled.Navigation
import androidx.compose.material.icons.filled.RoundaboutLeft
import androidx.compose.material.icons.filled.TurnLeft
import androidx.compose.material.icons.filled.TurnRight
import androidx.compose.material.icons.filled.UTurnLeft
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dashcue.app.DashCueViewModel
import com.dashcue.core.ble.ReconnectionStateMachine
import com.dashcue.v1.Maneuver
import com.dashcue.v1.NavState
import com.dashcue.v1.NavStatus
import com.dashcue.v1.TrafficLevel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.roundToInt

/**
 * Phone-side navigation is a mirror, not a second router.
 *
 * Google Maps and Waze own the route, map and traffic feed. Android does not
 * expose that route to a companion app, so this screen shows the same cue the
 * driver already sees on the phone — next turn, distance, ETA, traffic band,
 * speed and speed limit — with no fake map and no second set of directions.
 */
@Composable
fun NavigationScreen(vm: DashCueViewModel) {
    val nav by vm.navState.collectAsStateWithLifecycle()
    val source by vm.navSource.collectAsStateWithLifecycle()
    val connection by vm.connectionState.collectAsStateWithLifecycle()
    val settings by vm.settings.collectAsStateWithLifecycle()
    val imperial = settings.units == 1

    val active = nav != null &&
        nav!!.status != NavStatus.NAV_NONE &&
        nav!!.status != NavStatus.NAV_ARRIVED

    val standby = nav != null && nav!!.status == NavStatus.NAV_NONE

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        SourceStrip(
            source = source,
            active = active,
            standby = standby,
            healthy = connectionIsHealthy(connection)
        )

        if (active) {
            ActiveNavigationCard(nav!!, source, imperial)
        } else if (standby) {
            StandbyDrivingCard(
                nav = nav!!,
                imperial = imperial,
                onOpenMaps = vm::openGoogleMaps,
                onOpenWaze = vm::openWaze
            )
        } else {
            MirrorEmptyState(
                connection = connection,
                notificationAccess = vm.hasNotificationAccess(),
                onOpenMaps = vm::openGoogleMaps,
                onOpenWaze = vm::openWaze
            )
        }

        if (active) {
            TrafficLegend(nav!!.traffic)
        }

        Spacer(Modifier.height(4.dp))
        Text(
            "DashCue never invents a route. If Maps/Waze stops publishing its " +
                "navigation card, this screen says so instead of guessing.",
            color = DashCueColors.TextDim,
            style = MaterialTheme.typography.bodySmall
        )
    }
}

@Composable
private fun SourceStrip(source: String?, active: Boolean, standby: Boolean, healthy: Boolean) {
    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(8.dp)
                    .background(
                        when {
                            active && healthy -> DashCueColors.Ok
                            standby && healthy -> DashCueColors.Accent
                            else -> DashCueColors.TextDim
                        },
                        CircleShape
                    )
            )
            Spacer(Modifier.width(8.dp))
            Text(
                source ?: if (standby) "STANDBY TELEMETRY" else "NAVIGATION",
                color = DashCueColors.Text,
                fontSize = 12.sp,
                fontWeight = FontWeight.SemiBold,
                letterSpacing = 1.sp
            )
        }
        Text(
            when {
                !healthy -> "HUD OFFLINE"
                active -> "MIRRORING"
                standby -> "STANDBY"
                else -> "WAITING"
            },
            color = when {
                !healthy -> DashCueColors.Warn
                active -> DashCueColors.Ok
                standby -> DashCueColors.Accent
                else -> DashCueColors.TextDim
            },
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold,
            letterSpacing = 0.8.sp
        )
    }
}

@Composable
private fun StandbyDrivingCard(
    nav: NavState,
    imperial: Boolean,
    onOpenMaps: () -> Unit,
    onOpenWaze: () -> Unit
) {
    DashCard {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text(
                    "STANDBY DRIVING COCKPIT",
                    color = DashCueColors.Accent,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 1.sp
                )
                Text(
                    nav.street.ifBlank { "Driving Standby" },
                    color = DashCueColors.Text,
                    style = MaterialTheme.typography.titleLarge,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Spacer(Modifier.width(8.dp))
            val limit = formatSpeedLimit(nav.speedLimitKmh, imperial)
            val overspeed = nav.speedLimitKmh > 0 && (nav.speedKmhX10 / 10) > nav.speedLimitKmh
            Box(
                modifier = Modifier
                    .size(48.dp)
                    .clip(CircleShape)
                    .background(Color.White)
                    .border(3.dp, if (overspeed) DashCueColors.Danger else Color(0xFFCC0000), CircleShape),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    limit,
                    color = Color.Black,
                    fontWeight = FontWeight.Bold,
                    fontSize = 15.sp
                )
            }
        }

        Spacer(Modifier.height(6.dp))
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text("CURRENT SPEED", color = DashCueColors.TextDim, fontSize = 10.sp, letterSpacing = 0.8.sp)
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        formatSpeed(nav.speedKmhX10, imperial),
                        style = MaterialTheme.typography.headlineMedium,
                        color = DashCueColors.Text,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(Modifier.width(4.dp))
                    Text(
                        speedUnit(imperial),
                        color = DashCueColors.TextDim,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Medium,
                        modifier = Modifier.padding(bottom = 4.dp)
                    )
                }
            }
            Column(Modifier.weight(1f)) {
                Text("DIGITAL COMPASS", color = DashCueColors.TextDim, fontSize = 10.sp, letterSpacing = 0.8.sp)
                Text(
                    formatHeading(nav.headingDeg),
                    style = MaterialTheme.typography.headlineMedium,
                    color = DashCueColors.Accent,
                    fontWeight = FontWeight.SemiBold
                )
            }
        }

        Spacer(Modifier.height(8.dp))
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Button(
                onClick = onOpenMaps,
                modifier = Modifier.weight(1f),
                colors = ButtonDefaults.buttonColors(
                    containerColor = DashCueColors.Accent,
                    contentColor = DashCueColors.Bg
                )
            ) {
                Text("Google Maps")
            }
            OutlinedButton(
                onClick = onOpenWaze,
                modifier = Modifier.weight(1f)
            ) {
                Text("Waze")
            }
        }
    }
}

@Composable
private fun ActiveNavigationCard(nav: NavState, source: String?, imperial: Boolean = false) {
    val trafficColor = trafficColor(nav.traffic)
    val speed = if (nav.speedKmhX10 >= 0) nav.speedKmhX10 / 10 else -1
    val limit = nav.speedLimitKmh

    Column(
        Modifier
            .fillMaxWidth()
            .background(DashCueColors.Surface, RoundedCornerShape(6.dp))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(92.dp)
                    .background(DashCueColors.Accent.copy(alpha = 0.12f), RoundedCornerShape(6.dp)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    maneuverIcon(nav.next),
                    contentDescription = maneuverLabel(nav.next),
                    tint = DashCueColors.Accent,
                    modifier = Modifier.size(58.dp)
                )
            }
            Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    if (nav.distToNextM > 0) formatDistance(nav.distToNextM, imperial) else "NEXT",
                    color = DashCueColors.Text,
                    fontSize = if (nav.distToNextM > 0) 38.sp else 22.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = (-0.6).sp
                )
                Text(
                    nav.street.ifBlank { nav.next.name.replace("MV_", "").replace('_', ' ') },
                    color = DashCueColors.Text,
                    fontSize = 19.sp,
                    fontWeight = FontWeight.SemiBold,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                if (nav.following != Maneuver.MV_NONE && nav.distToFollowingM > 0) {
                    Text(
                        "then ${formatDistance(nav.distToFollowingM, imperial)}",
                        color = DashCueColors.TextDim,
                        fontSize = 13.sp
                    )
                }
            }
        }

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    "ETA",
                    color = DashCueColors.TextDim,
                    fontSize = 11.sp,
                    letterSpacing = 1.sp,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    if (nav.etaEpochS > 0) formatEta(nav.etaEpochS.toLong()) else "--:--",
                    color = trafficColor,
                    fontSize = 28.sp,
                    fontWeight = FontWeight.Bold
                )
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    "${formatDuration(nav.remainingS)} · ${formatDistance(nav.remainingM, imperial)}",
                    color = DashCueColors.Text,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.Medium
                )
                Text(
                    when (nav.traffic) {
                        TrafficLevel.TL_CLEAR -> "Clear traffic"
                        TrafficLevel.TL_MODERATE -> "Moderate traffic"
                        TrafficLevel.TL_HEAVY -> "Heavy traffic"
                        TrafficLevel.TL_SEVERE -> "Severe delays"
                        else -> "Traffic unknown"
                    },
                    color = trafficColor,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.SemiBold
                )
            }
        }

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            SpeedReadout(speed = speed, limit = limit, imperial = imperial)
            Column(Modifier.weight(1f)) {
                Text(
                    if (source != null) "Mirrored from $source" else "Live navigation",
                    color = DashCueColors.Text,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Medium
                )
                Text(
                    if (nav.hasLiveTraffic) {
                        "Traffic band comes from the navigation app's own cue."
                    } else {
                        "Traffic band is an estimate from distance and time; it is not a traffic feed."
                    },
                    color = DashCueColors.TextDim,
                    fontSize = 11.sp
                )
            }
            OutlinedButton(
                onClick = {},
                enabled = false,
                shape = RoundedCornerShape(6.dp)
            ) { Text("END ON PHONE", fontSize = 10.sp, letterSpacing = 0.6.sp) }
        }
    }
}

@Composable
private fun SpeedReadout(speed: Int, limit: Int, imperial: Boolean = false) {
    val over = limit > 0 && speed > limit + 5
    val near = limit > 0 && speed > limit
    val displaySpeed = if (imperial && speed >= 0) {
        (speed / 1.609344).roundToInt()
    } else {
        speed
    }
    val displayLimit = if (imperial && limit > 0) {
        (limit / 1.609344).roundToInt()
    } else {
        limit
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Column {
            Text(
                if (displaySpeed >= 0) "$displaySpeed" else "--",
                color = when {
                    over -> DashCueColors.Danger
                    near -> DashCueColors.Warn
                    else -> DashCueColors.Text
                },
                fontSize = 34.sp,
                fontWeight = FontWeight.Bold
            )
            Text(if (imperial) "MPH" else "KM/H", color = DashCueColors.TextDim, fontSize = 9.sp, letterSpacing = 1.sp)
        }
        Spacer(Modifier.width(10.dp))
        Box(
            Modifier
                .size(44.dp)
                .background(Color.White, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            Text(
                if (displayLimit > 0) "$displayLimit" else "--",
                color = Color.Black,
                fontSize = 18.sp,
                fontWeight = FontWeight.Bold
            )
        }
    }
}

@Composable
private fun MirrorEmptyState(
    connection: ReconnectionStateMachine.State,
    notificationAccess: Boolean,
    onOpenMaps: () -> Unit,
    onOpenWaze: () -> Unit
) {
    Column(
        Modifier
            .fillMaxWidth()
            .background(DashCueColors.Surface, RoundedCornerShape(6.dp))
            .padding(18.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Icon(
            Icons.Filled.Navigation,
            contentDescription = null,
            tint = DashCueColors.Accent,
            modifier = Modifier.size(34.dp)
        )
        Text(
            "Start a route in Google Maps or Waze",
            color = DashCueColors.Text,
            style = MaterialTheme.typography.titleLarge
        )
        Text(
            "DashCue mirrors the live navigation card: next turn, distance, ETA, " +
                "traffic band and speed. It does not draw a second map or invent turns.",
            color = DashCueColors.TextDim,
            style = MaterialTheme.typography.bodyMedium
        )
        if (!notificationAccess) {
            Text(
                "Notification access is required to read the navigation cue.",
                color = DashCueColors.Warn,
                style = MaterialTheme.typography.bodySmall
            )
        }
        if (!connectionIsHealthy(connection)) {
            Text(
                "The HUD link is ${connectionLabel(connection)}. The phone screen still works; the HUD will catch up when the link is ready.",
                color = DashCueColors.Warn,
                style = MaterialTheme.typography.bodySmall
            )
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Button(
                onClick = onOpenMaps,
                colors = ButtonDefaults.buttonColors(
                    containerColor = DashCueColors.Accent,
                    contentColor = DashCueColors.Bg
                ),
                shape = RoundedCornerShape(6.dp)
            ) { Text("OPEN MAPS") }
            OutlinedButton(onClick = onOpenWaze, shape = RoundedCornerShape(6.dp)) {
                Text("OPEN WAZE")
            }
        }
    }
}

@Composable
private fun TrafficLegend(level: TrafficLevel) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        LegendDot("CLEAR", TrafficLevel.TL_CLEAR)
        LegendDot("MODERATE", TrafficLevel.TL_MODERATE)
        LegendDot("HEAVY", TrafficLevel.TL_HEAVY)
    }
}

@Composable
private fun LegendDot(label: String, level: TrafficLevel) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(6.dp).background(trafficColor(level), CircleShape))
        Spacer(Modifier.width(5.dp))
        Text(label, color = DashCueColors.TextDim, fontSize = 9.sp, letterSpacing = 0.6.sp)
    }
}

@Composable
private fun trafficColor(level: TrafficLevel): Color = when (level) {
    TrafficLevel.TL_CLEAR -> DashCueColors.Ok
    TrafficLevel.TL_MODERATE -> DashCueColors.Warn
    TrafficLevel.TL_HEAVY, TrafficLevel.TL_SEVERE -> DashCueColors.Danger
    else -> DashCueColors.TextDim
}

private fun formatEta(epochSeconds: Long): String {
    if (epochSeconds <= 0) return "--:--"
    return SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(epochSeconds * 1000L))
}
