package com.dashcue.app.ui

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

object DashCueColors {
    // Polestar Scandinavian cockpit palette (spec §3.1), "Building from Black":
    // near-black base, grayscale hierarchy, one warm brand accent (Swedish
    // Amber) reserved for the primary action, plus a cool Glacier-Blue alt for
    // status/telemetry. Contrast ratios verified by tools/contrast_check.py.
    val Bg = Color(0xFF050608)
    val Surface = Color(0xFF111317)
    val Surface2 = Color(0xFF1A1D22)
    val Border = Color(0xFF242831)
    val Text = Color(0xFFF3F4F6)
    val TextDim = Color(0xFF848B98)
    val Accent = Color(0xFFE58B42) // Swedish Amber (primary action)
    val AccentAlt = Color(0xFF38BDF8) // Glacier Blue (status/telemetry)
    val Ok = Color(0xFF10B981)
    val Warn = Color(0xFFF59E0B)
    val Danger = Color(0xFFEF4444)
}

private val DashCueScheme = darkColorScheme(
    primary = DashCueColors.Accent,
    onPrimary = DashCueColors.Bg,
    secondary = DashCueColors.Ok,
    onSecondary = DashCueColors.Bg,
    background = DashCueColors.Bg,
    onBackground = DashCueColors.Text,
    surface = DashCueColors.Surface,
    onSurface = DashCueColors.Text,
    surfaceVariant = DashCueColors.Surface2,
    onSurfaceVariant = DashCueColors.TextDim,
    error = DashCueColors.Danger,
    onError = DashCueColors.Bg,
    outline = DashCueColors.Surface2
)

private val DashCueShapes = Shapes(
    extraSmall = RoundedCornerShape(3.dp),
    small = RoundedCornerShape(4.dp),
    medium = RoundedCornerShape(6.dp),
    large = RoundedCornerShape(8.dp),
    extraLarge = RoundedCornerShape(10.dp)
)

@Composable
fun DashCueTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DashCueScheme,
        shapes = DashCueShapes,
        typography = MaterialTheme.typography.copy(
            headlineMedium = TextStyle(
                fontSize = 28.sp,
                fontWeight = FontWeight.Bold,
                letterSpacing = (-0.4).sp
            ),
            titleLarge = TextStyle(fontSize = 21.sp, fontWeight = FontWeight.SemiBold),
            titleMedium = TextStyle(fontSize = 16.sp, fontWeight = FontWeight.SemiBold),
            bodyMedium = TextStyle(fontSize = 14.sp, lineHeight = 20.sp),
            bodySmall = TextStyle(fontSize = 12.sp, lineHeight = 16.sp),
            labelSmall = TextStyle(fontSize = 11.sp, fontWeight = FontWeight.Medium, letterSpacing = 0.6.sp)
        ),
        content = content
    )
}
