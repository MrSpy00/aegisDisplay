package com.aegissoft.aegisdisplay.ui.screens

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BatteryChargingFull
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Computer
import androidx.compose.material.icons.rounded.DesktopWindows
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Usb
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aegissoft.aegisdisplay.R
import com.aegissoft.aegisdisplay.input.ScaleMode
import com.aegissoft.aegisdisplay.network.DiscoveredHost
import com.aegissoft.aegisdisplay.protocol.AdspConstants
import com.aegissoft.aegisdisplay.ui.viewmodel.ConnectionState
import com.aegissoft.aegisdisplay.ui.viewmodel.DisplayUiState
import com.aegissoft.aegisdisplay.ui.viewmodel.ResolutionPreset
import com.aegissoft.aegisdisplay.ui.viewmodel.TouchInputMode
import kotlin.math.roundToInt

@Composable
fun HomeScreen(
    state: DisplayUiState,
    onScanHosts: () -> Unit,
    onConnectHost: (ip: String, label: String, port: Int) -> Unit,
    onManualIpChanged: (String) -> Unit,
    onResolutionSelected: (ResolutionPreset) -> Unit,
    onCustomDimensionsApply: (Int, Int) -> Unit,
    onRefreshSelected: (Int) -> Unit,
    onBitrateSelected: (Int) -> Unit,
    onCodecSelected: (Boolean) -> Unit,
    onTransportSelected: (Int) -> Unit,
    onScaleModeSelected: (ScaleMode) -> Unit,
    onInputModeSelected: (TouchInputMode) -> Unit,
    onTrackpadSensitivitySelected: (Float) -> Unit,
    onHudAutoHideChange: (Boolean) -> Unit,
    onHudStartHiddenChange: (Boolean) -> Unit,
    onLowLatencyChange: (Boolean) -> Unit,
    onImmersiveChange: (Boolean) -> Unit,
    onKeepScreenOnChange: (Boolean) -> Unit,
    onRequestBatteryExemption: () -> Unit
) {
    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .safeDrawingPadding(),
        containerColor = MaterialTheme.colorScheme.background
    ) { innerPadding ->
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
        ) {
            val isWideTablet = maxWidth >= 700.dp
            val scrollState = rememberScrollState()

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(scrollState)
                    .padding(horizontal = 16.dp, vertical = 14.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                HeaderBanner(state = state)

                if (isWideTablet) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(16.dp)
                        ) {
                            HostsDiscoveryCard(
                                state = state,
                                onScanHosts = onScanHosts,
                                onConnectHost = onConnectHost
                            )
                            DirectConnectCard(
                                state = state,
                                onManualIpChanged = onManualIpChanged,
                                onConnectHost = onConnectHost
                            )
                            PowerGuardCard(
                                isExempt = state.isBatteryExempt,
                                onRequestExemption = onRequestBatteryExemption
                            )
                        }
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(16.dp)
                        ) {
                            DisplayProfileCard(
                                state = state,
                                onResolutionSelected = onResolutionSelected,
                                onCustomDimensionsApply = onCustomDimensionsApply,
                                onRefreshSelected = onRefreshSelected,
                                onBitrateSelected = onBitrateSelected,
                                onCodecSelected = onCodecSelected,
                                onTransportSelected = onTransportSelected,
                                onScaleModeSelected = onScaleModeSelected
                            )
                            AdvancedSettingsCard(
                                state = state,
                                onInputModeSelected = onInputModeSelected,
                                onTrackpadSensitivitySelected = onTrackpadSensitivitySelected,
                                onHudAutoHideChange = onHudAutoHideChange,
                                onHudStartHiddenChange = onHudStartHiddenChange,
                                onLowLatencyChange = onLowLatencyChange,
                                onImmersiveChange = onImmersiveChange,
                                onKeepScreenOnChange = onKeepScreenOnChange
                            )
                        }
                    }
                } else {
                    HostsDiscoveryCard(
                        state = state,
                        onScanHosts = onScanHosts,
                        onConnectHost = onConnectHost
                    )
                    DirectConnectCard(
                        state = state,
                        onManualIpChanged = onManualIpChanged,
                        onConnectHost = onConnectHost
                    )
                    DisplayProfileCard(
                        state = state,
                        onResolutionSelected = onResolutionSelected,
                        onCustomDimensionsApply = onCustomDimensionsApply,
                        onRefreshSelected = onRefreshSelected,
                        onBitrateSelected = onBitrateSelected,
                        onCodecSelected = onCodecSelected,
                        onTransportSelected = onTransportSelected,
                        onScaleModeSelected = onScaleModeSelected
                    )
                    AdvancedSettingsCard(
                        state = state,
                        onInputModeSelected = onInputModeSelected,
                        onTrackpadSensitivitySelected = onTrackpadSensitivitySelected,
                        onHudAutoHideChange = onHudAutoHideChange,
                        onHudStartHiddenChange = onHudStartHiddenChange,
                        onLowLatencyChange = onLowLatencyChange,
                        onImmersiveChange = onImmersiveChange,
                        onKeepScreenOnChange = onKeepScreenOnChange
                    )
                    PowerGuardCard(
                        isExempt = state.isBatteryExempt,
                        onRequestExemption = onRequestBatteryExemption
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))
            }
        }
    }
}

@Composable
private fun HeaderBanner(state: DisplayUiState) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        colors = CardDefaults.elevatedCardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.72f)
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(18.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.surface,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)),
                    modifier = Modifier.size(50.dp)
                ) {
                    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        Image(
                            painter = painterResource(R.drawable.ic_launcher_background),
                            contentDescription = null,
                            contentScale = ContentScale.Crop,
                            modifier = Modifier.fillMaxSize()
                        )
                        Image(
                            painter = painterResource(R.drawable.ic_launcher_foreground),
                            contentDescription = stringResource(R.string.app_name),
                            contentScale = ContentScale.Crop,
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                }
                Spacer(modifier = Modifier.width(14.dp))
                Column {
                    Text(
                        text = stringResource(R.string.app_name),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(R.string.brand_subtitle),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.width(12.dp))

            val statusText = when (state.connectionState) {
                ConnectionState.IDLE -> stringResource(R.string.status_idle)
                ConnectionState.CONNECTING -> stringResource(R.string.status_connecting)
                ConnectionState.STREAMING -> stringResource(R.string.status_streaming)
                ConnectionState.ERROR -> state.statusError ?: stringResource(R.string.status_error)
            }
            val badgeColor = when (state.connectionState) {
                ConnectionState.ERROR -> MaterialTheme.colorScheme.error
                ConnectionState.CONNECTING -> MaterialTheme.colorScheme.tertiary
                else -> MaterialTheme.colorScheme.primary
            }

            Surface(
                shape = RoundedCornerShape(999.dp),
                color = badgeColor.copy(alpha = 0.16f),
                border = BorderStroke(1.dp, badgeColor.copy(alpha = 0.45f))
            ) {
                Text(
                    text = statusText,
                    style = MaterialTheme.typography.labelMedium,
                    color = badgeColor,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp)
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun HostsDiscoveryCard(
    state: DisplayUiState,
    onScanHosts: () -> Unit,
    onConnectHost: (String, String, Int) -> Unit
) {
    val usbLabel = stringResource(R.string.action_connect_usb)

    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = stringResource(R.string.section_discovered_hosts),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f)
                )
                OutlinedButton(
                    onClick = onScanHosts,
                    enabled = !state.isScanning
                ) {
                    if (state.isScanning) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp
                        )
                    } else {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(text = stringResource(R.string.action_scan_hosts), maxLines = 1)
                }
            }

            if (state.hosts.isEmpty()) {
                Text(
                    text = stringResource(R.string.empty_hosts_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    state.hosts.forEach { host ->
                        DiscoveredHostRow(
                            host = host,
                            enabled = state.connectionState != ConnectionState.CONNECTING,
                            onClick = { onConnectHost(host.ipAddress, host.hostname, host.tcpPort) }
                        )
                    }
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = {
                        onConnectHost("127.0.0.1", usbLabel, AdspConstants.DEFAULT_CONTROL_PORT)
                    },
                    enabled = state.connectionState != ConnectionState.CONNECTING,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.secondary
                    )
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Usb,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(text = usbLabel, maxLines = 1)
                }
            }

            Text(
                text = stringResource(R.string.usb_adb_tip),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun DiscoveredHostRow(
    host: DiscoveredHost,
    enabled: Boolean,
    onClick: () -> Unit
) {
    Surface(
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.weight(1f)
            ) {
                Icon(
                    imageVector = Icons.Rounded.Computer,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.width(12.dp))
                Column {
                    Text(
                        text = host.hostname,
                        style = MaterialTheme.typography.bodyLarge,
                        fontWeight = FontWeight.Medium
                    )
                    Text(
                        text = "${host.ipAddress} · ${stringResource(R.string.label_port_value, host.tcpPort, host.udpPort)}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.width(8.dp))
            Button(onClick = onClick, enabled = enabled) {
                Text(text = stringResource(R.string.action_connect), maxLines = 1)
            }
        }
    }
}

@Composable
private fun DirectConnectCard(
    state: DisplayUiState,
    onManualIpChanged: (String) -> Unit,
    onConnectHost: (String, String, Int) -> Unit
) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = stringResource(R.string.section_manual_connect),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedTextField(
                    value = state.manualIp,
                    onValueChange = onManualIpChanged,
                    label = { Text(stringResource(R.string.hint_host_ip)) },
                    singleLine = true,
                    modifier = Modifier.weight(1f)
                )
                Button(
                    onClick = {
                        onConnectHost(
                            state.manualIp,
                            state.manualIp,
                            AdspConstants.DEFAULT_CONTROL_PORT
                        )
                    },
                    enabled = state.manualIp.isNotBlank() &&
                        state.connectionState != ConnectionState.CONNECTING
                ) {
                    Text(text = stringResource(R.string.action_connect), maxLines = 1)
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun DisplayProfileCard(
    state: DisplayUiState,
    onResolutionSelected: (ResolutionPreset) -> Unit,
    onCustomDimensionsApply: (Int, Int) -> Unit,
    onRefreshSelected: (Int) -> Unit,
    onBitrateSelected: (Int) -> Unit,
    onCodecSelected: (Boolean) -> Unit,
    onTransportSelected: (Int) -> Unit,
    onScaleModeSelected: (ScaleMode) -> Unit
) {
    var widthInput by remember(state.customWidth) { mutableStateOf(state.customWidth.toString()) }
    var heightInput by remember(state.customHeight) { mutableStateOf(state.customHeight.toString()) }

    LaunchedEffect(state.customWidth, state.customHeight) {
        widthInput = state.customWidth.toString()
        heightInput = state.customHeight.toString()
    }

    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    imageVector = Icons.Rounded.Tune,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = stringResource(R.string.section_display_profile),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
            }

            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    text = stringResource(R.string.label_resolution_preset),
                    style = MaterialTheme.typography.labelLarge
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    val items = listOf(
                        ResolutionPreset.NATIVE to R.string.preset_native,
                        ResolutionPreset.P1080 to R.string.preset_1080p,
                        ResolutionPreset.P1440 to R.string.preset_1440p,
                        ResolutionPreset.CUSTOM to R.string.preset_custom
                    )
                    items.forEach { (preset, labelRes) ->
                        FilterChip(
                            selected = state.resolutionPreset == preset,
                            onClick = { onResolutionSelected(preset) },
                            label = { Text(stringResource(labelRes), maxLines = 1) }
                        )
                    }
                }
            }

            if (state.resolutionPreset == ResolutionPreset.CUSTOM) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    OutlinedTextField(
                        value = widthInput,
                        onValueChange = { s -> widthInput = s.filter(Char::isDigit).take(4) },
                        label = { Text(stringResource(R.string.label_custom_width)) },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f)
                    )
                    OutlinedTextField(
                        value = heightInput,
                        onValueChange = { s -> heightInput = s.filter(Char::isDigit).take(4) },
                        label = { Text(stringResource(R.string.label_custom_height)) },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f)
                    )
                    OutlinedButton(
                        onClick = {
                            val w = widthInput.toIntOrNull() ?: state.customWidth
                            val h = heightInput.toIntOrNull() ?: state.customHeight
                            onCustomDimensionsApply(w, h)
                        }
                    ) {
                        Text(text = stringResource(R.string.action_apply_custom_res), maxLines = 1)
                    }
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    text = stringResource(R.string.label_refresh_rate),
                    style = MaterialTheme.typography.labelLarge
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(30, 60, 90, 120).forEach { hz ->
                        FilterChip(
                            selected = state.refreshHz == hz,
                            onClick = { onRefreshSelected(hz) },
                            label = { Text(stringResource(R.string.hz_format, hz), maxLines = 1) }
                        )
                    }
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Rounded.Speed,
                            contentDescription = null,
                            modifier = Modifier.size(16.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = stringResource(R.string.label_bitrate, state.bitrateKbps / 1000),
                            style = MaterialTheme.typography.labelLarge
                        )
                    }
                }
                Slider(
                    value = (state.bitrateKbps / 1000f),
                    onValueChange = { mbps ->
                        val snapped = ((mbps / 2f).roundToInt() * 2).coerceIn(4, 60)
                        onBitrateSelected(snapped * 1000)
                    },
                    valueRange = 4f..60f,
                    steps = 27
                )
            }

            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    text = stringResource(R.string.label_codec),
                    style = MaterialTheme.typography.labelLarge
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    FilterChip(
                        selected = state.preferHevc,
                        onClick = { onCodecSelected(true) },
                        label = { Text(stringResource(R.string.codec_hevc), maxLines = 1) }
                    )
                    FilterChip(
                        selected = !state.preferHevc,
                        onClick = { onCodecSelected(false) },
                        label = { Text(stringResource(R.string.codec_h264), maxLines = 1) }
                    )
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    text = stringResource(R.string.label_transport),
                    style = MaterialTheme.typography.labelLarge
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(
                        AdspConstants.TRANSPORT_AUTO to R.string.transport_auto,
                        AdspConstants.TRANSPORT_UDP to R.string.transport_udp,
                        AdspConstants.TRANSPORT_TCP to R.string.transport_tcp
                    ).forEach { (pref, labelRes) ->
                        FilterChip(
                            selected = state.transportPref == pref,
                            onClick = { onTransportSelected(pref) },
                            label = { Text(stringResource(labelRes), maxLines = 1) }
                        )
                    }
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    text = stringResource(R.string.label_scale_mode),
                    style = MaterialTheme.typography.labelLarge
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(
                        ScaleMode.FIT to R.string.scale_fit,
                        ScaleMode.FILL to R.string.scale_fill,
                        ScaleMode.NATIVE_ONE_TO_ONE to R.string.scale_native
                    ).forEach { (mode, labelRes) ->
                        FilterChip(
                            selected = state.scaleMode == mode,
                            onClick = { onScaleModeSelected(mode) },
                            label = { Text(stringResource(labelRes), maxLines = 1) }
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AdvancedSettingsCard(
    state: DisplayUiState,
    onInputModeSelected: (TouchInputMode) -> Unit,
    onTrackpadSensitivitySelected: (Float) -> Unit,
    onHudAutoHideChange: (Boolean) -> Unit,
    onHudStartHiddenChange: (Boolean) -> Unit,
    onLowLatencyChange: (Boolean) -> Unit,
    onImmersiveChange: (Boolean) -> Unit,
    onKeepScreenOnChange: (Boolean) -> Unit
) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    imageVector = Icons.Rounded.Settings,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = stringResource(R.string.section_advanced_settings),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
            }

            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    text = stringResource(R.string.label_input_mode),
                    style = MaterialTheme.typography.labelLarge
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(
                        TouchInputMode.DIRECT_TOUCH to R.string.input_mode_direct,
                        TouchInputMode.TRACKPAD to R.string.input_mode_trackpad,
                        TouchInputMode.VIEW_ONLY to R.string.input_mode_view_only
                    ).forEach { (mode, labelRes) ->
                        FilterChip(
                            selected = state.touchInputMode == mode,
                            onClick = { onInputModeSelected(mode) },
                            label = { Text(stringResource(labelRes), maxLines = 1) }
                        )
                    }
                }
            }

            if (state.touchInputMode == TouchInputMode.TRACKPAD) {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = stringResource(
                            R.string.label_trackpad_sensitivity,
                            state.trackpadSensitivity
                        ),
                        style = MaterialTheme.typography.labelLarge
                    )
                    Slider(
                        value = state.trackpadSensitivity,
                        onValueChange = { raw ->
                            val snapped = ((raw * 10f).roundToInt() / 10f).coerceIn(0.5f, 3.5f)
                            onTrackpadSensitivitySelected(snapped)
                        },
                        valueRange = 0.5f..3.5f
                    )
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

            SettingSwitchRow(
                title = stringResource(R.string.setting_hud_auto_hide),
                description = stringResource(R.string.setting_hud_auto_hide_desc),
                checked = state.hudAutoHide,
                onCheckedChange = onHudAutoHideChange
            )

            SettingSwitchRow(
                title = stringResource(R.string.setting_hud_start_hidden),
                description = stringResource(R.string.setting_hud_start_hidden_desc),
                checked = state.hudStartHidden,
                onCheckedChange = onHudStartHiddenChange
            )

            SettingSwitchRow(
                title = stringResource(R.string.setting_low_latency),
                description = null,
                checked = state.lowLatencyMode,
                onCheckedChange = onLowLatencyChange
            )

            SettingSwitchRow(
                title = stringResource(R.string.setting_immersive),
                description = null,
                checked = state.immersiveFullscreen,
                onCheckedChange = onImmersiveChange
            )

            SettingSwitchRow(
                title = stringResource(R.string.setting_keep_screen_on),
                description = null,
                checked = state.keepScreenOn,
                onCheckedChange = onKeepScreenOnChange
            )
        }
    }
}

@Composable
private fun SettingSwitchRow(
    title: String,
    description: String?,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column(
            modifier = Modifier
                .weight(1f)
                .padding(end = 12.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Medium
            )
            if (description != null) {
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange
        )
    }
}

@Composable
private fun PowerGuardCard(
    isExempt: Boolean,
    onRequestExemption: () -> Unit
) {
    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    imageVector = if (isExempt) Icons.Rounded.CheckCircle else Icons.Rounded.Warning,
                    contentDescription = null,
                    tint = if (isExempt) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.tertiary
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = stringResource(R.string.section_power_guard),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
            }

            Text(
                text = stringResource(
                    if (isExempt) R.string.power_guard_exempt else R.string.power_guard_restricted
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            if (!isExempt) {
                OutlinedButton(onClick = onRequestExemption) {
                    Icon(
                        imageVector = Icons.Rounded.BatteryChargingFull,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(text = stringResource(R.string.action_grant_unrestricted), maxLines = 1)
                }
            }
        }
    }
}
