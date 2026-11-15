package com.aegissoft.aegisdisplay

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.aegissoft.aegisdisplay.service.DisplaySessionService
import com.aegissoft.aegisdisplay.ui.screens.HomeScreen
import com.aegissoft.aegisdisplay.ui.screens.StreamScreen
import com.aegissoft.aegisdisplay.ui.theme.AegisDisplayTheme
import com.aegissoft.aegisdisplay.ui.viewmodel.ConnectionState
import com.aegissoft.aegisdisplay.ui.viewmodel.DisplayViewModel

class MainActivity : ComponentActivity() {

    private val viewModel: DisplayViewModel by viewModels()

    private val sessionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            when (intent?.action) {
                DisplaySessionService.ACTION_DISCONNECT_BROADCAST -> viewModel.disconnect()
                DisplaySessionService.ACTION_TOGGLE_HUD_BROADCAST -> viewModel.toggleHudVisible()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        WindowCompat.setDecorFitsSystemWindows(window, false)

        val filter = IntentFilter().apply {
            addAction(DisplaySessionService.ACTION_DISCONNECT_BROADCAST)
            addAction(DisplaySessionService.ACTION_TOGGLE_HUD_BROADCAST)
        }
        ContextCompat.registerReceiver(
            this,
            sessionReceiver,
            filter,
            ContextCompat.RECEIVER_NOT_EXPORTED
        )

        setContent {
            AegisDisplayTheme {
                val state by viewModel.uiState.collectAsState()
                val isStreaming = state.connectionState == ConnectionState.STREAMING

                LaunchedEffect(isStreaming, state.immersiveFullscreen, state.keepScreenOn) {
                    if (isStreaming && state.keepScreenOn) {
                        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    } else {
                        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    }
                    setImmersiveSystemBars(isStreaming && state.immersiveFullscreen)
                }

                if (isStreaming) {
                    StreamScreen(
                        state = state,
                        onSurfaceAvailable = viewModel::onVideoSurfaceAvailable,
                        onSurfaceDestroyed = viewModel::onVideoSurfaceDestroyed,
                        onTouchBatch = viewModel::sendTouchBatch,
                        onPenEvent = viewModel::sendPenEvent,
                        onMouseEvent = viewModel::sendMouseEvent,
                        onRequestKeyframe = viewModel::requestKeyframe,
                        onBitrateChange = viewModel::updateBitrateKbps,
                        onCycleScaleMode = viewModel::cycleScaleMode,
                        onCycleInputMode = viewModel::cycleTouchInputMode,
                        onToggleImmersive = {
                            viewModel.updateImmersiveFullscreen(!state.immersiveFullscreen)
                        },
                        onSetHudVisible = viewModel::setHudVisible,
                        onDisconnect = viewModel::disconnect
                    )
                } else {
                    HomeScreen(
                        state = state,
                        onScanHosts = viewModel::scanHosts,
                        onConnectHost = viewModel::connectToHost,
                        onManualIpChanged = viewModel::updateManualIp,
                        onResolutionSelected = viewModel::updateResolutionPreset,
                        onCustomDimensionsApply = viewModel::updateCustomDimensions,
                        onRefreshSelected = viewModel::updateRefreshHz,
                        onBitrateSelected = viewModel::updateBitrateKbps,
                        onCodecSelected = viewModel::updatePreferHevc,
                        onTransportSelected = viewModel::updateTransportPref,
                        onScaleModeSelected = viewModel::updateScaleMode,
                        onInputModeSelected = viewModel::updateTouchInputMode,
                        onTrackpadSensitivitySelected = viewModel::updateTrackpadSensitivity,
                        onHudAutoHideChange = viewModel::updateHudAutoHide,
                        onHudStartHiddenChange = viewModel::updateHudStartHidden,
                        onLowLatencyChange = viewModel::updateLowLatencyMode,
                        onImmersiveChange = viewModel::updateImmersiveFullscreen,
                        onKeepScreenOnChange = viewModel::updateKeepScreenOn,
                        onRequestBatteryExemption = {
                            runCatching {
                                startActivity(DisplaySessionService.buildBatteryExemptionIntent(this))
                            }
                        }
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        viewModel.refreshBatteryStatus()
    }

    override fun onDestroy() {
        unregisterReceiver(sessionReceiver)
        super.onDestroy()
    }

    private fun setImmersiveSystemBars(hide: Boolean) {
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        if (hide) {
            controller.hide(WindowInsetsCompat.Type.systemBars())
        } else {
            controller.show(WindowInsetsCompat.Type.systemBars())
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes = window.attributes.apply {
                layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
        }
    }
}
