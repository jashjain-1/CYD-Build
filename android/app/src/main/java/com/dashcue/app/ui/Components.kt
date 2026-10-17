package com.dashcue.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.CallMerge
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ForkLeft
import androidx.compose.material.icons.filled.ForkRight
import androidx.compose.material.icons.filled.RoundaboutLeft
import androidx.compose.material.icons.filled.TurnLeft
import androidx.compose.material.icons.filled.TurnRight
import androidx.compose.material.icons.filled.UTurnLeft
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.dashcue.v1.Maneuver

@Composable
fun DashCard(
    modifier: Modifier = Modifier,
    containerColor: Color = DashCueColors.Surface,
    content: @Composable ColumnScope.() -> Unit
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = containerColor),
        shape = RoundedCornerShape(12.dp)
    ) {
        Column(
            Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
            content = content
        )
    }
}

@Composable
fun SectionLabel(text: String) {
    Text(
        text.uppercase(),
        color = DashCueColors.TextDim,
        fontSize = 11.sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 1.sp
    )
}

@Composable
fun StatusPill(text: String, color: Color) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .background(color.copy(alpha = 0.08f), CircleShape)
            .padding(horizontal = 10.dp, vertical = 4.dp)
            .semantics(mergeDescendants = true) {
                contentDescription = "Status: $text"
            }
    ) {
        Spacer(
            Modifier
                .size(8.dp)
                .background(color, CircleShape)
        )
        Spacer(Modifier.width(6.dp))
        Text(text, color = color, fontSize = 12.sp, fontWeight = FontWeight.Medium)
    }
}

@Composable
fun MetricRow(label: String, value: String) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = DashCueColors.TextDim, style = MaterialTheme.typography.bodySmall)
        Text(value, color = DashCueColors.Text, style = MaterialTheme.typography.bodyMedium)
    }
}

/**
 * Turn cue icon for both the dashboard glance card and the navigation screen.
 * A shared mapping keeps the two surfaces telling the driver the same thing.
 */
fun maneuverIcon(maneuver: Maneuver): ImageVector = when (maneuver) {
    Maneuver.MV_TURN_LEFT, Maneuver.MV_SLIGHT_LEFT, Maneuver.MV_SHARP_LEFT -> Icons.Filled.TurnLeft
    Maneuver.MV_TURN_RIGHT, Maneuver.MV_SLIGHT_RIGHT, Maneuver.MV_SHARP_RIGHT -> Icons.Filled.TurnRight
    Maneuver.MV_UTURN -> Icons.Filled.UTurnLeft
    Maneuver.MV_ROUNDABOUT -> Icons.Filled.RoundaboutLeft
    Maneuver.MV_MERGE -> Icons.Filled.CallMerge
    Maneuver.MV_FORK_LEFT, Maneuver.MV_RAMP_LEFT -> Icons.Filled.ForkLeft
    Maneuver.MV_FORK_RIGHT, Maneuver.MV_RAMP_RIGHT -> Icons.Filled.ForkRight
    Maneuver.MV_ARRIVE, Maneuver.MV_ARRIVE_LEFT, Maneuver.MV_ARRIVE_RIGHT -> Icons.Filled.Check
    Maneuver.MV_DEPART -> Icons.AutoMirrored.Filled.ArrowBack
    else -> Icons.Filled.ArrowUpward
}

/** Plain-language turn caption, matching the HUD's maneuver label. */
fun maneuverLabel(maneuver: Maneuver): String = when (maneuver) {
    Maneuver.MV_TURN_LEFT -> "Turn left"
    Maneuver.MV_SLIGHT_LEFT -> "Keep left"
    Maneuver.MV_SHARP_LEFT -> "Sharp left"
    Maneuver.MV_FORK_LEFT -> "Keep left at the fork"
    Maneuver.MV_RAMP_LEFT -> "Exit left"
    Maneuver.MV_TURN_RIGHT -> "Turn right"
    Maneuver.MV_SLIGHT_RIGHT -> "Keep right"
    Maneuver.MV_SHARP_RIGHT -> "Sharp right"
    Maneuver.MV_FORK_RIGHT -> "Keep right at the fork"
    Maneuver.MV_RAMP_RIGHT -> "Exit right"
    Maneuver.MV_UTURN -> "Make a U-turn"
    Maneuver.MV_ROUNDABOUT -> "Enter the roundabout"
    Maneuver.MV_MERGE -> "Merge"
    Maneuver.MV_ARRIVE, Maneuver.MV_ARRIVE_LEFT, Maneuver.MV_ARRIVE_RIGHT -> "Arrive at destination"
    Maneuver.MV_CONTINUE, Maneuver.MV_DEPART -> "Continue straight"
    else -> "Follow the route"
}

@Composable
fun TransportButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    enabled: Boolean = true,
    accent: Boolean = false,
    accentColor: Color = DashCueColors.Accent
) {
    FilledIconButton(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.size(if (accent) 56.dp else 48.dp),
        colors = IconButtonDefaults.filledIconButtonColors(
            containerColor = if (accent) accentColor else DashCueColors.Surface2,
            contentColor = if (accent) DashCueColors.Bg else DashCueColors.Text,
            disabledContainerColor = DashCueColors.Surface2.copy(alpha = 0.4f),
            disabledContentColor = DashCueColors.TextDim
        )
    ) {
        Icon(icon, contentDescription, modifier = Modifier.size(if (accent) 30.dp else 22.dp))
    }
}
