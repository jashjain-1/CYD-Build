package com.dashcue.app.ui

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Immutable
data class DashCueColorScheme(
    val bg: Color,
    val surface: Color,
    val surface2: Color,
    val border: Color,
    val text: Color,
    val textDim: Color,
    val accent: Color,
    val accentAlt: Color,
    val ok: Color,
    val warn: Color,
    val danger: Color,
    val isDark: Boolean = true
)

// Dark Tokens (spec §3.1)
val DashCueDarkColors = DashCueColorScheme(
    bg = Color(0xFF050608),
    surface = Color(0xFF111317),
    surface2 = Color(0xFF1A1D22),
    border = Color(0xFF242831),
    text = Color(0xFFF3F4F6),
    textDim = Color(0xFF848B98),
    accent = Color(0xFFE58B42), // Swedish Amber
    accentAlt = Color(0xFF38BDF8), // Glacier Blue
    ok = Color(0xFF10B981),
    warn = Color(0xFFF59E0B),
    danger = Color(0xFFEF4444),
    isDark = true
)

// Light Tokens (WCAG 2.1 AA certified by tools/contrast_check.py)
val DashCueLightColors = DashCueColorScheme(
    bg = Color(0xFFF8FAFC),
    surface = Color(0xFFE2E8F0),
    surface2 = Color(0xFFCBD5E1),
    border = Color(0xFF94A3B8),
    text = Color(0xFF0B1220),
    textDim = Color(0xFF334155),
    accent = Color(0xFF0369A1), // Deep Ocean Blue
    accentAlt = Color(0xFF0284C7),
    ok = Color(0xFF047857),
    warn = Color(0xFFB45309),
    danger = Color(0xFFB91C1C),
    isDark = false
)

val LocalDashCueColors = staticCompositionLocalOf { DashCueDarkColors }

object DashCueColors {
    val current: DashCueColorScheme
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current

    val isDark: Boolean
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.isDark

    val Bg: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.bg

    val Surface: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.surface

    val Surface2: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.surface2

    val Border: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.border

    val Text: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.text

    val TextDim: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.textDim

    val Accent: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.accent

    val AccentAlt: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.accentAlt

    val Ok: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.ok

    val Warn: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.warn

    val Danger: Color
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current.danger
}

object DashCueTheme {
    val colors: DashCueColorScheme
        @Composable
        @ReadOnlyComposable
        get() = LocalDashCueColors.current
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
    val colors = if (darkTheme) DashCueDarkColors else DashCueLightColors
    val scheme = if (darkTheme) DashCueDarkScheme else DashCueLightScheme
    CompositionLocalProvider(LocalDashCueColors provides colors) {
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
}
