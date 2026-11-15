package com.aegissoft.aegisdisplay.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val AegisDarkColors = darkColorScheme(
    primary = Color(0xFF00C8E0),
    onPrimary = Color(0xFF002026),
    primaryContainer = Color(0xFF004F5C),
    onPrimaryContainer = Color(0xFF9DF0FF),
    secondary = Color(0xFF4FD8EB),
    onSecondary = Color(0xFF00363D),
    background = Color(0xFF0A1018),
    onBackground = Color(0xFFE6EEF5),
    surface = Color(0xFF101926),
    onSurface = Color(0xFFE6EEF5),
    surfaceVariant = Color(0xFF182436),
    onSurfaceVariant = Color(0xFFAEC0D4),
    outline = Color(0xFF2D3F56),
    error = Color(0xFFFF6B6B),
    onError = Color(0xFF330000)
)

private val AegisLightColors = lightColorScheme(
    primary = Color(0xFF007D8F),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFC5F4FA),
    onPrimaryContainer = Color(0xFF002930),
    secondary = Color(0xFF2A6670),
    onSecondary = Color(0xFFFFFFFF),
    background = Color(0xFFF3F7FA),
    onBackground = Color(0xFF0F1A24),
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF0F1A24),
    surfaceVariant = Color(0xFFE5EEF4),
    onSurfaceVariant = Color(0xFF425568),
    outline = Color(0xFFBCCAD6),
    error = Color(0xFFD32F2F),
    onError = Color(0xFFFFFFFF)
)

@Composable
fun AegisDisplayTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = if (darkTheme) AegisDarkColors else AegisLightColors,
        content = content
    )
}
