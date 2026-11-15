package com.aegissoft.aegisdisplay.data

import android.content.Context
import com.aegissoft.aegisdisplay.input.ScaleMode
import com.aegissoft.aegisdisplay.protocol.AdspConstants
import com.aegissoft.aegisdisplay.ui.viewmodel.ResolutionPreset
import com.aegissoft.aegisdisplay.ui.viewmodel.TouchInputMode

data class PersistedSettings(
    val manualIp: String = "127.0.0.1",
    val resolutionPreset: ResolutionPreset = ResolutionPreset.NATIVE,
    val customWidth: Int = 1920,
    val customHeight: Int = 1080,
    val refreshHz: Int = 60,
    val bitrateKbps: Int = 22000,
    val preferHevc: Boolean = true,
    val transportPref: Int = AdspConstants.TRANSPORT_AUTO,
    val scaleMode: ScaleMode = ScaleMode.FIT,
    val touchInputMode: TouchInputMode = TouchInputMode.DIRECT_TOUCH,
    val trackpadSensitivity: Float = 1.6f,
    val hudAutoHide: Boolean = true,
    val hudStartHidden: Boolean = false,
    val immersiveFullscreen: Boolean = true,
    val keepScreenOn: Boolean = true,
    val lowLatencyMode: Boolean = true
)

class SettingsRepository(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun load(): PersistedSettings {
        val def = PersistedSettings()
        val presetName = prefs.getString(KEY_RES_PRESET, def.resolutionPreset.name) ?: def.resolutionPreset.name
        val preset = runCatching { ResolutionPreset.valueOf(presetName) }.getOrDefault(def.resolutionPreset)

        val scaleName = prefs.getString(KEY_SCALE_MODE, def.scaleMode.name) ?: def.scaleMode.name
        val scale = runCatching { ScaleMode.valueOf(scaleName) }.getOrDefault(def.scaleMode)

        val touchName = prefs.getString(KEY_TOUCH_MODE, def.touchInputMode.name) ?: def.touchInputMode.name
        val touchMode = runCatching { TouchInputMode.valueOf(touchName) }.getOrDefault(def.touchInputMode)

        return PersistedSettings(
            manualIp = prefs.getString(KEY_MANUAL_IP, def.manualIp) ?: def.manualIp,
            resolutionPreset = preset,
            customWidth = prefs.getInt(KEY_CUSTOM_W, def.customWidth).coerceIn(640, 3840),
            customHeight = prefs.getInt(KEY_CUSTOM_H, def.customHeight).coerceIn(480, 2560),
            refreshHz = prefs.getInt(KEY_REFRESH_HZ, def.refreshHz).coerceIn(30, 120),
            bitrateKbps = prefs.getInt(KEY_BITRATE_KBPS, def.bitrateKbps).coerceIn(4000, 60000),
            preferHevc = prefs.getBoolean(KEY_PREFER_HEVC, def.preferHevc),
            transportPref = prefs.getInt(KEY_TRANSPORT_PREF, def.transportPref),
            scaleMode = scale,
            touchInputMode = touchMode,
            trackpadSensitivity = prefs.getFloat(KEY_TRACKPAD_SENS, def.trackpadSensitivity).coerceIn(0.5f, 3.5f),
            hudAutoHide = prefs.getBoolean(KEY_HUD_AUTO_HIDE, def.hudAutoHide),
            hudStartHidden = prefs.getBoolean(KEY_HUD_START_HIDDEN, def.hudStartHidden),
            immersiveFullscreen = prefs.getBoolean(KEY_IMMERSIVE, def.immersiveFullscreen),
            keepScreenOn = prefs.getBoolean(KEY_KEEP_SCREEN_ON, def.keepScreenOn),
            lowLatencyMode = prefs.getBoolean(KEY_LOW_LATENCY, def.lowLatencyMode)
        )
    }

    fun save(s: PersistedSettings) {
        prefs.edit()
            .putString(KEY_MANUAL_IP, s.manualIp)
            .putString(KEY_RES_PRESET, s.resolutionPreset.name)
            .putInt(KEY_CUSTOM_W, s.customWidth)
            .putInt(KEY_CUSTOM_H, s.customHeight)
            .putInt(KEY_REFRESH_HZ, s.refreshHz)
            .putInt(KEY_BITRATE_KBPS, s.bitrateKbps)
            .putBoolean(KEY_PREFER_HEVC, s.preferHevc)
            .putInt(KEY_TRANSPORT_PREF, s.transportPref)
            .putString(KEY_SCALE_MODE, s.scaleMode.name)
            .putString(KEY_TOUCH_MODE, s.touchInputMode.name)
            .putFloat(KEY_TRACKPAD_SENS, s.trackpadSensitivity)
            .putBoolean(KEY_HUD_AUTO_HIDE, s.hudAutoHide)
            .putBoolean(KEY_HUD_START_HIDDEN, s.hudStartHidden)
            .putBoolean(KEY_IMMERSIVE, s.immersiveFullscreen)
            .putBoolean(KEY_KEEP_SCREEN_ON, s.keepScreenOn)
            .putBoolean(KEY_LOW_LATENCY, s.lowLatencyMode)
            .apply()
    }

    private companion object {
        const val PREFS_NAME = "aegis_display_prefs_v1"
        const val KEY_MANUAL_IP = "manual_ip"
        const val KEY_RES_PRESET = "res_preset"
        const val KEY_CUSTOM_W = "custom_w"
        const val KEY_CUSTOM_H = "custom_h"
        const val KEY_REFRESH_HZ = "refresh_hz"
        const val KEY_BITRATE_KBPS = "bitrate_kbps"
        const val KEY_PREFER_HEVC = "prefer_hevc"
        const val KEY_TRANSPORT_PREF = "transport_pref"
        const val KEY_SCALE_MODE = "scale_mode"
        const val KEY_TOUCH_MODE = "touch_mode"
        const val KEY_TRACKPAD_SENS = "trackpad_sens"
        const val KEY_HUD_AUTO_HIDE = "hud_auto_hide"
        const val KEY_HUD_START_HIDDEN = "hud_start_hidden"
        const val KEY_IMMERSIVE = "immersive"
        const val KEY_KEEP_SCREEN_ON = "keep_screen_on"
        const val KEY_LOW_LATENCY = "low_latency"
    }
}
