package com.dashcue.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.dashcue.core.ble.ReconnectionStateMachine
import kotlinx.coroutines.delay
import java.time.LocalTime
import java.time.format.DateTimeFormatter

private val clockFormat: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm")

/**
 * Mirrors the HUD status bar: wordmark on the left, clock in the middle,
 * link state on the right. Keeping the same vocabulary and colours on both
 * screens is what makes the pair feel like one cockpit rather than two apps.
 */
@Composable
fun CockpitTopBar(
    connection: ReconnectionStateMachine.State,
    modifier: Modifier = Modifier
) {
    var now by remember { mutableStateOf(LocalTime.now()) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(30_000)
            now = LocalTime.now()
        }
    }

    val healthy = connectionIsHealthy(connection)
    Row(
        modifier
            .fillMaxWidth()
            .height(56.dp)
            .background(DashCueColors.Bg)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            Modifier
                .size(10.dp)
                .background(
                    if (healthy) DashCueColors.Accent else DashCueColors.TextDim,
                    CircleShape
                )
        )
        Spacer(Modifier.width(9.dp))
        Text(
            "DashCue",
            color = DashCueColors.Text,
            fontSize = 19.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = (-0.2).sp
        )
        Spacer(Modifier.weight(1f))
        Text(
            now.format(clockFormat),
            color = DashCueColors.Text,
            fontSize = 15.sp,
            fontWeight = FontWeight.Medium
        )
        Spacer(Modifier.width(14.dp))
        StatusPill(
            connectionLabel(connection),
            when {
                healthy -> DashCueColors.Accent
                connection == ReconnectionStateMachine.State.TERMINAL_ERROR -> DashCueColors.Danger
                connection == ReconnectionStateMachine.State.DISCONNECTED -> DashCueColors.TextDim
                else -> DashCueColors.Warn
            }
        )
    }
}
