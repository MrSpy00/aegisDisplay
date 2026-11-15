package com.aegissoft.aegisdisplay.ui.viewmodel

import android.app.Application
import android.os.Build
import android.provider.Settings
import android.view.Surface
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aegissoft.aegisdisplay.data.PersistedSettings
import com.aegissoft.aegisdisplay.data.SettingsRepository
import com.aegissoft.aegisdisplay.input.ScaleMode
import com.aegissoft.aegisdisplay.network.DiscoveredHost
import com.aegissoft.aegisdisplay.network.DiscoveryClient
import com.aegissoft.aegisdisplay.network.SessionCallbacks
import com.aegissoft.aegisdisplay.network.StreamParams
import com.aegissoft.aegisdisplay.network.StreamSessionClient
import com.aegissoft.aegisdisplay.protocol.AdspConstants
import com.aegissoft.aegisdisplay.protocol.AssembledVideoFrame
import com.aegissoft.aegisdisplay.protocol.HelloAck
import com.aegissoft.aegisdisplay.protocol.MouseEventData
import com.aegissoft.aegisdisplay.protocol.PenEventData
import com.aegissoft.aegisdisplay.protocol.TouchContactData
import com.aegissoft.aegisdisplay.service.DisplaySessionService
import com.aegissoft.aegisdisplay.video.LowLatencyVideoDecoder
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

enum class ResolutionPreset {
    NATIVE,
    P1080,
    P1440,
    CUSTOM
}

enum class TouchInputMode {
    DIRECT_TOUCH,
    TRACKPAD,
    VIEW_ONLY
}

enum class ConnectionState {
    IDLE,
    CONNECTING,
    STREAMING,
    ERROR
}

data class DisplayUiState(
    val hosts: List<DiscoveredHost> = emptyList(),
    val isScanning: Boolean = false,
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
    val hudVisible: Boolean = true,
    val immersiveFullscreen: Boolean = true,
    val keepScreenOn: Boolean = true,
    val lowLatencyMode: Boolean = true,
    val connectionState: ConnectionState = ConnectionState.IDLE,
    val activeHostLabel: String = "",
    val activeTransportLabel: String = "",
    val streamWidth: Int = 1920,
    val streamHeight: Int = 1080,
    val streamCodec: Int = AdspConstants.CODEC_HEVC,
    val measuredFps: Double = 0.0,
    val measuredRttMs: Double = 0.0,
    val statusError: String? = null,
    val isBatteryExempt: Boolean = true
)

class DisplayViewModel(app: Application) : AndroidViewModel(app) {

    private val repo = SettingsRepository(app)
    private val discoveryClient = DiscoveryClient()
    private val initial = repo.load()

    private val _uiState = MutableStateFlow(
        DisplayUiState(
            manualIp = initial.manualIp,
            resolutionPreset = initial.resolutionPreset,
            customWidth = initial.customWidth,
            customHeight = initial.customHeight,
            refreshHz = initial.refreshHz,
            bitrateKbps = initial.bitrateKbps,
            preferHevc = initial.preferHevc,
            transportPref = initial.transportPref,
            scaleMode = initial.scaleMode,
            touchInputMode = initial.touchInputMode,
            trackpadSensitivity = initial.trackpadSensitivity,
            hudAutoHide = initial.hudAutoHide,
            hudStartHidden = initial.hudStartHidden,
            hudVisible = !initial.hudStartHidden,
            immersiveFullscreen = initial.immersiveFullscreen,
            keepScreenOn = initial.keepScreenOn,
            lowLatencyMode = initial.lowLatencyMode,
            isBatteryExempt = DisplaySessionService.isBatteryOptimizationExempt(app)
        )
    )
    val uiState: StateFlow<DisplayUiState> = _uiState.asStateFlow()

    @Volatile
    private var attachedSurface: Surface? = null

    private val decoder: LowLatencyVideoDecoder = LowLatencyVideoDecoder(
        onNeedKeyframe = { sessionClient.requestKeyframe(1) },
        onFrameRendered = { fps ->
            _uiState.update { it.copy(measuredFps = fps) }
        }
    )

    private val sessionClient: StreamSessionClient = StreamSessionClient(object : SessionCallbacks {
        override fun onHandshakeAccepted(ack: HelloAck, isTcpTunnel: Boolean) {
            val appCtx = getApplication<Application>()
            val codecName = if (ack.negotiatedCodec == AdspConstants.CODEC_HEVC) "HEVC" else "H.264"
            val modeLabel = if (isTcpTunnel) "TCP / USB ($codecName)" else "UDP ($codecName)"

            val st = _uiState.value
            _uiState.update {
                it.copy(
                    connectionState = ConnectionState.STREAMING,
                    streamWidth = ack.width,
                    streamHeight = ack.height,
                    streamCodec = ack.negotiatedCodec,
                    activeTransportLabel = modeLabel,
                    hudVisible = !it.hudStartHidden,
                    statusError = null
                )
            }
            DisplaySessionService.start(appCtx, st.activeHostLabel, modeLabel)
            // Immediately push user's preferred bitrate/lowLatency config
            sessionClient.sendDisplayConfigUpdate(
                width = ack.width,
                height = ack.height,
                refreshHz = ack.refreshHz,
                bitrateKbps = st.bitrateKbps,
                codec = ack.negotiatedCodec,
                scaleMode = st.scaleMode.wireId,
                lowLatency = st.lowLatencyMode
            )
            attachedSurface?.let { surf ->
                decoder.configure(surf, ack.width, ack.height, ack.negotiatedCodec)
            }
        }

        override fun onVideoFrameReceived(frame: AssembledVideoFrame) {
            val t0 = System.nanoTime()
            decoder.submitFrame(frame)
            val decodeUs = ((System.nanoTime() - t0) / 1000L).toInt().coerceAtLeast(1)
            if ((frame.frameId and 0x03) == 0 || frame.isIdr) {
                sessionClient.sendFrameAck(frame.frameId, decodeUs)
            }
        }

        override fun onRttUpdated(rttMs: Double) {
            _uiState.update { it.copy(measuredRttMs = rttMs) }
        }

        override fun onDisconnected(reason: String) {
            decoder.release()
            DisplaySessionService.stop(getApplication())
            _uiState.update {
                val isIntentional = reason == "Disconnected" || reason == "Host closed session"
                it.copy(
                    connectionState = if (isIntentional) ConnectionState.IDLE else ConnectionState.ERROR,
                    statusError = if (isIntentional) null else reason,
                    measuredFps = 0.0
                )
            }
        }
    })

    init {
        scanHosts()
    }

    private fun persistCurrent(st: DisplayUiState) {
        repo.save(
            PersistedSettings(
                manualIp = st.manualIp,
                resolutionPreset = st.resolutionPreset,
                customWidth = st.customWidth,
                customHeight = st.customHeight,
                refreshHz = st.refreshHz,
                bitrateKbps = st.bitrateKbps,
                preferHevc = st.preferHevc,
                transportPref = st.transportPref,
                scaleMode = st.scaleMode,
                touchInputMode = st.touchInputMode,
                trackpadSensitivity = st.trackpadSensitivity,
                hudAutoHide = st.hudAutoHide,
                hudStartHidden = st.hudStartHidden,
                immersiveFullscreen = st.immersiveFullscreen,
                keepScreenOn = st.keepScreenOn,
                lowLatencyMode = st.lowLatencyMode
            )
        )
    }

    private fun updateAndPersist(transform: (DisplayUiState) -> DisplayUiState) {
        var latest: DisplayUiState? = null
        _uiState.update { prev ->
            transform(prev).also { latest = it }
        }
        latest?.let { persistCurrent(it) }
    }

    fun refreshBatteryStatus() {
        val exempt = DisplaySessionService.isBatteryOptimizationExempt(getApplication())
        _uiState.update { it.copy(isBatteryExempt = exempt) }
    }

    fun scanHosts() {
        if (_uiState.value.isScanning) return
        viewModelScope.launch {
            _uiState.update { it.copy(isScanning = true) }
            val map = linkedMapOf<String, DiscoveredHost>()
            discoveryClient.listenAndProbe(2200L) { host ->
                map[host.hostId] = host
                _uiState.update { it.copy(hosts = map.values.toList()) }
            }
            _uiState.update { it.copy(isScanning = false) }
        }
    }

    fun updateManualIp(ip: String) = updateAndPersist { it.copy(manualIp = ip) }
    fun updateResolutionPreset(p: ResolutionPreset) = updateAndPersist { it.copy(resolutionPreset = p) }
    fun updateCustomDimensions(w: Int, h: Int) {
        val evenW = (w.coerceIn(640, 3840) / 2) * 2
        val evenH = (h.coerceIn(480, 2560) / 2) * 2
        updateAndPersist { it.copy(customWidth = evenW, customHeight = evenH) }
    }

    fun updateRefreshHz(hz: Int) = updateAndPersist { it.copy(refreshHz = hz.coerceIn(30, 120)) }
    fun updatePreferHevc(hevc: Boolean) = updateAndPersist { it.copy(preferHevc = hevc) }
    fun updateTransportPref(pref: Int) = updateAndPersist { it.copy(transportPref = pref) }
    fun updateScaleMode(mode: ScaleMode) = updateAndPersist { it.copy(scaleMode = mode) }
    fun updateTouchInputMode(mode: TouchInputMode) = updateAndPersist { it.copy(touchInputMode = mode) }
    fun updateTrackpadSensitivity(sens: Float) = updateAndPersist {
        it.copy(trackpadSensitivity = sens.coerceIn(0.5f, 3.5f))
    }

    fun updateHudAutoHide(autoHide: Boolean) = updateAndPersist { it.copy(hudAutoHide = autoHide) }
    fun updateHudStartHidden(hidden: Boolean) = updateAndPersist { it.copy(hudStartHidden = hidden) }
    fun updateImmersiveFullscreen(immersive: Boolean) = updateAndPersist { it.copy(immersiveFullscreen = immersive) }
    fun updateKeepScreenOn(keepOn: Boolean) = updateAndPersist { it.copy(keepScreenOn = keepOn) }
    fun updateLowLatencyMode(lowLat: Boolean) = updateAndPersist { it.copy(lowLatencyMode = lowLat) }

    fun setHudVisible(visible: Boolean) = _uiState.update { it.copy(hudVisible = visible) }
    fun toggleHudVisible() = _uiState.update { it.copy(hudVisible = !it.hudVisible) }

    fun updateBitrateKbps(kbps: Int) {
        val clamped = kbps.coerceIn(4000, 60000)
        updateAndPersist { it.copy(bitrateKbps = clamped) }
        val st = _uiState.value
        if (st.connectionState == ConnectionState.STREAMING) {
            sessionClient.sendDisplayConfigUpdate(
                width = st.streamWidth,
                height = st.streamHeight,
                refreshHz = st.refreshHz,
                bitrateKbps = clamped,
                codec = st.streamCodec,
                scaleMode = st.scaleMode.wireId,
                lowLatency = st.lowLatencyMode
            )
        }
    }

    fun cycleScaleMode() {
        val next = when (_uiState.value.scaleMode) {
            ScaleMode.FIT -> ScaleMode.FILL
            ScaleMode.FILL -> ScaleMode.NATIVE_ONE_TO_ONE
            ScaleMode.NATIVE_ONE_TO_ONE -> ScaleMode.FIT
        }
        updateScaleMode(next)
    }

    fun cycleTouchInputMode() {
        val next = when (_uiState.value.touchInputMode) {
            TouchInputMode.DIRECT_TOUCH -> TouchInputMode.TRACKPAD
            TouchInputMode.TRACKPAD -> TouchInputMode.VIEW_ONLY
            TouchInputMode.VIEW_ONLY -> TouchInputMode.DIRECT_TOUCH
        }
        updateTouchInputMode(next)
    }

    fun connectToHost(hostIp: String, hostLabel: String, tcpPort: Int = AdspConstants.DEFAULT_CONTROL_PORT) {
        val app = getApplication<Application>()
        val dm = app.resources.displayMetrics
        val longSide = maxOf(dm.widthPixels, dm.heightPixels)
        val shortSide = minOf(dm.widthPixels, dm.heightPixels)
        val even = { v: Int -> (v / 2) * 2 }

        val cur = _uiState.value
        val (targetW, targetH) = when (cur.resolutionPreset) {
            ResolutionPreset.NATIVE -> even(longSide).coerceIn(640, 3840) to even(shortSide).coerceIn(480, 2400)
            ResolutionPreset.P1080 -> 1920 to 1080
            ResolutionPreset.P1440 -> 2560 to 1440
            ResolutionPreset.CUSTOM -> even(cur.customWidth).coerceIn(640, 3840) to even(cur.customHeight).coerceIn(480, 2560)
        }

        val androidId = Settings.Secure.getString(app.contentResolver, Settings.Secure.ANDROID_ID) ?: "android-client"
        val codecMask = if (cur.preferHevc) {
            AdspConstants.CODEC_MASK_HEVC or AdspConstants.CODEC_MASK_H264
        } else {
            AdspConstants.CODEC_MASK_H264
        }

        _uiState.update {
            it.copy(
                connectionState = ConnectionState.CONNECTING,
                activeHostLabel = hostLabel,
                statusError = null
            )
        }

        sessionClient.connect(
            StreamParams(
                hostIp = hostIp.trim(),
                tcpPort = tcpPort,
                deviceId = androidId,
                deviceName = "${Build.MANUFACTURER} ${Build.MODEL}".trim(),
                width = targetW,
                height = targetH,
                refreshHz = cur.refreshHz,
                dpi = dm.densityDpi,
                codecMask = codecMask,
                transportPref = cur.transportPref
            )
        )
    }

    fun onVideoSurfaceAvailable(surface: Surface) {
        attachedSurface = surface
        val s = _uiState.value
        if (s.connectionState == ConnectionState.STREAMING) {
            decoder.configure(surface, s.streamWidth, s.streamHeight, s.streamCodec)
            sessionClient.requestKeyframe(2)
        }
    }

    fun onVideoSurfaceDestroyed() {
        attachedSurface = null
        decoder.release()
    }

    fun sendTouchBatch(contacts: List<TouchContactData>) = sessionClient.sendTouchBatch(contacts)
    fun sendPenEvent(pen: PenEventData) = sessionClient.sendPenEvent(pen)
    fun sendMouseEvent(mouse: MouseEventData) = sessionClient.sendMouseEvent(mouse)
    fun requestKeyframe() = sessionClient.requestKeyframe(3)

    fun disconnect() {
        sessionClient.disconnect(sendBye = true)
        decoder.release()
        DisplaySessionService.stop(getApplication())
        _uiState.update {
            it.copy(connectionState = ConnectionState.IDLE, measuredFps = 0.0, statusError = null)
        }
    }

    override fun onCleared() {
        decoder.release()
        sessionClient.destroy()
        DisplaySessionService.stop(getApplication())
        super.onCleared()
    }
}
