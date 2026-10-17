package com.dashcue.app.ui

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

object DashCueColors {
    var isDark by mutableStateOf(true)

    // Dark Tokens (spec §3.1)
    private val DarkBg = Color(0xFF050608)
    private val DarkSurface = Color(0xFF111317)
    private val DarkSurface2 = Color(0xFF1A1D22)
    private val DarkBorder = Color(0xFF242831)
    private val DarkText = Color(0xFFF3F4F6)
    private val DarkTextDim = Color(0xFF848B98)
    private val DarkAccent = Color(0xFFE58B42) // Swedish Amber
    private val DarkAccentAlt = Color(0xFF38BDF8) // Glacier Blue
    private val DarkOk = Color(0xFF10B981)
    private val DarkWarn = Color(0xFFF59E0B)
    private val DarkDanger = Color(0xFFEF4444)

    // Light Tokens (WCAG 2.1 AA certified by tools/contrast_check.py)
    private val LightBg = Color(0xFFF8FAFC)
    private val LightSurface = Color(0xFFE2E8F0)
    private val LightSurface2 = Color(0xFFCBD5E1)
    private val LightBorder = Color(0xFF94A3B8)
    private val LightText = Color(0xFF0B1220)
    private val LightTextDim = Color(0xFF334155)
    private val LightAccent = Color(0xFF0369A1) // Deep Ocean Blue
    private val LightAccentAlt = Color(0xFF0284C7)
    private val LightOk = Color(0xFF047857)
    private val LightWarn = Color(0xFFB45309)
    private val LightDanger = Color(0xFFB91C1C)

    val Bg: Color get() = if (isDark) DarkBg else LightBg
    val Surface: Color get() = if (isDark) DarkSurface else LightSurface
    val Surface2: Color get() = if (isDark) DarkSurface2 else LightSurface2
    val Border: Color get() = if (isDark) DarkBorder else LightBorder
    val Text: Color get() = if (isDark) DarkText else LightText
    val TextDim: Color get() = if (isDark) DarkTextDim else LightTextDim
    val Accent: Color get() = if (isDark) DarkAccent else LightAccent
    val AccentAlt: Color get() = if (isDark) DarkAccentAlt else LightAccentAlt
    val Ok: Color get() = if (isDark) DarkOk else LightOk
    val Warn: Color get() = if (isDark) DarkWarn else LightWarn
    val Danger: Color get() = if (isDark) DarkDanger else LightDanger
}

private val DashCueDarkScheme = darkColorScheme(
    primary = Color(0xFFE58B42),
    onPrimary = Color(0xFF050608),
    secondary = Color(0xFF10B981),
    onSecondary = Color(0xFF050608),
    background = Color(0xFF050608),
    onBackground = Color(0xFFF3F4F6),
    surface = Color(0xFF111317),
    onSurface = Color(0xFFF3F4F6),
    surfaceVariant = Color(0xFF1A1D22),
    onSurfaceVariant = Color(0xFF848B98),
    error = Color(0xFFEF4444),
    onError = Color(0xFF050608),
    outline = Color(0xFF1A1D22)
)

private val DashCueLightScheme = lightColorScheme(
    primary = Color(0xFF0369A1),
    onPrimary = Color(0xFFF8FAFC),
    secondary = Color(0xFF047857),
    onSecondary = Color(0xFFF8FAFC),
    background = Color(0xFFF8FAFC),
    onBackground = Color(0xFF0B1220),
    surface = Color(0xFFE2E8F0),
    onSurface = Color(0xFF0B1220),
    surfaceVariant = Color(0xFFCBD5E1),
    onSurfaceVariant = Color(0xFF334155),
    error = Color(0xFFB91C1C),
    onError = Color(0xFFF8FAFC),
    outline = Color(0xFFCBD5E1)
)

private val DashCueShapes = Shapes(
    extraSmall = RoundedCornerShape(3.dp),
    small = RoundedCornerShape(4.dp),
    medium = RoundedCornerShape(6.dp),
    large = RoundedCornerShape(8.dp),
    extraLarge = RoundedCornerShape(10.dp)
)

@Composable
fun DashCueTheme(
    darkTheme: Boolean = true,
    content: @Composable () -> Unit
) {
    DashCueColors.isDark = darkTheme
    val scheme = if (darkTheme) DashCueDarkScheme else DashCueLightScheme
    MaterialTheme(
        colorScheme = scheme,
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
