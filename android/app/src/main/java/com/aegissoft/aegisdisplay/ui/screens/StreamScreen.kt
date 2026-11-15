package com.aegissoft.aegisdisplay.ui.screens

import android.annotation.SuppressLint
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewConfiguration
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.AspectRatio
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Fullscreen
import androidx.compose.material.icons.rounded.FullscreenExit
import androidx.compose.material.icons.rounded.Key
import androidx.compose.material.icons.rounded.Mouse
import androidx.compose.material.icons.rounded.Remove
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface as ComposeSurface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.aegissoft.aegisdisplay.R
import com.aegissoft.aegisdisplay.input.PointerInputMapper
import com.aegissoft.aegisdisplay.input.ScaleMode
import com.aegissoft.aegisdisplay.protocol.AdspConstants
import com.aegissoft.aegisdisplay.protocol.MouseEventData
import com.aegissoft.aegisdisplay.protocol.PenEventData
import com.aegissoft.aegisdisplay.protocol.TouchContactData
import com.aegissoft.aegisdisplay.ui.viewmodel.DisplayUiState
import com.aegissoft.aegisdisplay.ui.viewmodel.TouchInputMode
import kotlinx.coroutines.delay
import kotlin.math.hypot
import kotlin.math.roundToInt

@SuppressLint("ClickableViewAccessibility")
@Composable
fun StreamScreen(
    state: DisplayUiState,
    onSurfaceAvailable: (Surface) -> Unit,
    onSurfaceDestroyed: () -> Unit,
    onTouchBatch: (List<TouchContactData>) -> Unit,
    onPenEvent: (PenEventData) -> Unit,
    onMouseEvent: (MouseEventData) -> Unit,
    onRequestKeyframe: () -> Unit,
    onBitrateChange: (Int) -> Unit,
    onCycleScaleMode: () -> Unit,
    onCycleInputMode: () -> Unit,
    onToggleImmersive: () -> Unit,
    onSetHudVisible: (Boolean) -> Unit,
    onDisconnect: () -> Unit
) {
    val context = LocalContext.current
    val touchSlopPx = remember(context) { ViewConfiguration.get(context).scaledTouchSlop.toFloat() }

    val latestState by rememberUpdatedState(state)
    val latestOnTouchBatch by rememberUpdatedState(onTouchBatch)
    val latestOnPenEvent by rememberUpdatedState(onPenEvent)
    val latestOnMouseEvent by rememberUpdatedState(onMouseEvent)
    val latestOnSetHudVisible by rememberUpdatedState(onSetHudVisible)

    var hudInteractionTick by remember { mutableIntStateOf(0) }
    var showRestoreHint by remember { mutableStateOf(false) }
    var prevHudVisible by remember { mutableStateOf(state.hudVisible) }

    BackHandler(enabled = true) {
        latestOnSetHudVisible(!latestState.hudVisible)
    }

    LaunchedEffect(state.hudVisible, state.hudAutoHide, hudInteractionTick) {
        if (state.hudVisible && state.hudAutoHide) {
            delay(4000L)
            latestOnSetHudVisible(false)
        }
    }

    LaunchedEffect(state.hudVisible) {
        if (!state.hudVisible && (prevHudVisible || state.hudStartHidden)) {
            showRestoreHint = true
            delay(2600L)
            showRestoreHint = false
        } else if (state.hudVisible) {
            showRestoreHint = false
        }
        prevHudVisible = state.hudVisible
    }

    val trackpadState = remember { TrackpadGestureTracker(touchSlopPx) }

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
            .clipToBounds()
    ) {
        val density = LocalDensity.current
        val viewW = with(density) { maxWidth.toPx() }.coerceAtLeast(1f)
        val viewH = with(density) { maxHeight.toPx() }.coerceAtLeast(1f)
        val streamW = state.streamWidth.coerceAtLeast(2).toFloat()
        val streamH = state.streamHeight.coerceAtLeast(2).toFloat()

        val scale = when (state.scaleMode) {
            ScaleMode.FIT -> minOf(viewW / streamW, viewH / streamH)
            ScaleMode.FILL -> maxOf(viewW / streamW, viewH / streamH)
            ScaleMode.NATIVE_ONE_TO_ONE -> 1.0f
        }
        val contentWidthDp = with(density) { (streamW * scale).toDp() }
        val contentHeightDp = with(density) { (streamH * scale).toDp() }

        // Transparent input container at full viewport dimensions so PointerInputMapper coordinates
        // match the centered SurfaceView identically.
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            AndroidView(
                modifier = Modifier
                    .width(contentWidthDp)
                    .height(contentHeightDp),
                factory = { ctx ->
                    SurfaceView(ctx).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                onSurfaceAvailable(holder.surface)
                            }

                            override fun surfaceChanged(
                                holder: SurfaceHolder,
                                format: Int,
                                width: Int,
                                height: Int
                            ) {
                                // Configured by LowLatencyVideoDecoder on surfaceCreated
                            }

                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                onSurfaceDestroyed()
                            }
                        })
                    }
                }
            )

            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { ctx ->
                    View(ctx).apply {
                        isClickable = true
                        isFocusable = true
                        setOnTouchListener { view, event ->
                            val cur = latestState

                            // 3-finger quick tap always toggles the floating HUD (even in VIEW_ONLY)
                            if (trackpadState.detectThreeFingerTap(event)) {
                                latestOnSetHudVisible(!cur.hudVisible)
                                return@setOnTouchListener true
                            }

                            when (cur.touchInputMode) {
                                TouchInputMode.VIEW_ONLY -> true
                                TouchInputMode.TRACKPAD -> {
                                    trackpadState.processMotionEvent(
                                        event = event,
                                        sensitivity = cur.trackpadSensitivity,
                                        emit = latestOnMouseEvent
                                    )
                                    true
                                }
                                TouchInputMode.DIRECT_TOUCH -> {
                                    val toolType = if (event.pointerCount > 0) {
                                        event.getToolType(0)
                                    } else {
                                        MotionEvent.TOOL_TYPE_FINGER
                                    }
                                    if (toolType == MotionEvent.TOOL_TYPE_STYLUS ||
                                        toolType == MotionEvent.TOOL_TYPE_ERASER
                                    ) {
                                        PointerInputMapper.extractPenEvent(
                                            event = event,
                                            viewWidth = view.width,
                                            viewHeight = view.height,
                                            streamWidth = cur.streamWidth,
                                            streamHeight = cur.streamHeight,
                                            scaleMode = cur.scaleMode
                                        )?.let(latestOnPenEvent)
                                    } else {
                                        val contacts = PointerInputMapper.extractTouchContacts(
                                            event = event,
                                            viewWidth = view.width,
                                            viewHeight = view.height,
                                            streamWidth = cur.streamWidth,
                                            streamHeight = cur.streamHeight,
                                            scaleMode = cur.scaleMode
                                        )
                                        if (contacts.isNotEmpty()) {
                                            latestOnTouchBatch(contacts)
                                        }
                                    }
                                    true
                                }
                            }
                        }
                    }
                }
            )
        }

        // Completely absent from layout/composition (0 pixels) when hudVisible == false
        AnimatedVisibility(
            visible = state.hudVisible,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier
                .align(Alignment.TopCenter)
                .safeDrawingPadding()
                .padding(horizontal = 12.dp, vertical = 10.dp)
        ) {
            ComposeSurface(
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.88f),
                tonalElevation = 6.dp,
                shadowElevation = 8.dp
            ) {
                Row(
                    modifier = Modifier
                        .horizontalScroll(rememberScrollState())
                        .padding(horizontal = 12.dp, vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Text(
                        text = stringResource(
                            R.string.hud_metrics_format,
                            state.measuredFps.roundToInt(),
                            state.measuredRttMs.roundToInt(),
                            state.activeTransportLabel
                        ),
                        style = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.padding(end = 6.dp)
                    )

                    IconButton(
                        onClick = {
                            hudInteractionTick++
                            onBitrateChange(state.bitrateKbps - 4000)
                        },
                        modifier = Modifier.size(34.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Remove,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                    }

                    Text(
                        text = "${state.bitrateKbps / 1000}M",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Bold
                    )

                    IconButton(
                        onClick = {
                            hudInteractionTick++
                            onBitrateChange(state.bitrateKbps + 4000)
                        },
                        modifier = Modifier.size(34.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Add,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                    }

                    IconButton(
                        onClick = {
                            hudInteractionTick++
                            onRequestKeyframe()
                        }
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Key,
                            contentDescription = stringResource(R.string.action_request_keyframe)
                        )
                    }

                    IconButton(
                        onClick = {
                            hudInteractionTick++
                            onCycleInputMode()
                        }
                    ) {
                        val inputIcon = when (state.touchInputMode) {
                            TouchInputMode.DIRECT_TOUCH -> Icons.Rounded.TouchApp
                            TouchInputMode.TRACKPAD -> Icons.Rounded.Mouse
                            TouchInputMode.VIEW_ONLY -> Icons.Rounded.Visibility
                        }
                        Icon(
                            imageVector = inputIcon,
                            contentDescription = stringResource(R.string.hud_toggle_input)
                        )
                    }

                    IconButton(
                        onClick = {
                            hudInteractionTick++
                            onCycleScaleMode()
                        }
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.AspectRatio,
                            contentDescription = stringResource(R.string.hud_toggle_scale)
                        )
                    }

                    IconButton(
                        onClick = {
                            hudInteractionTick++
                            onToggleImmersive()
                        }
                    ) {
                        Icon(
                            imageVector = if (state.immersiveFullscreen) {
                                Icons.Rounded.FullscreenExit
                            } else {
                                Icons.Rounded.Fullscreen
                            },
                            contentDescription = stringResource(R.string.hud_immersive_toggle)
                        )
                    }

                    IconButton(
                        onClick = { onSetHudVisible(false) }
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.VisibilityOff,
                            contentDescription = stringResource(R.string.hud_hide_completely)
                        )
                    }

                    IconButton(
                        onClick = onDisconnect,
                        colors = IconButtonDefaults.iconButtonColors(
                            contentColor = MaterialTheme.colorScheme.error
                        )
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Close,
                            contentDescription = stringResource(R.string.action_disconnect)
                        )
                    }
                }
            }
        }

        AnimatedVisibility(
            visible = showRestoreHint && !state.hudVisible,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .safeDrawingPadding()
                .padding(bottom = 18.dp, start = 16.dp, end = 16.dp)
        ) {
            ComposeSurface(
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.85f),
                tonalElevation = 4.dp
            ) {
                Text(
                    text = stringResource(R.string.hud_restore_hint),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp)
                )
            }
        }
    }
}

private class TrackpadGestureTracker(private val touchSlopPx: Float) {
    private var threeDownTimeMs = 0L
    private var threeGestureArmed = false
    private var threeTriggered = false

    private var gestureStartTimeMs = 0L
    private var maxPointers = 0
    private var totalDistancePx = 0f
    private var lastPrimaryX = 0f
    private var lastPrimaryY = 0f
    private var remX = 0f
    private var remY = 0f
    private var remScroll = 0f

    fun detectThreeFingerTap(event: MotionEvent): Boolean {
        val masked = event.actionMasked
        if (masked == MotionEvent.ACTION_DOWN) {
            threeGestureArmed = false
            threeTriggered = false
        }
        if (event.pointerCount == 3 && !threeGestureArmed && !threeTriggered) {
            threeGestureArmed = true
            threeDownTimeMs = System.currentTimeMillis()
        }
        if (threeGestureArmed && !threeTriggered &&
            (masked == MotionEvent.ACTION_POINTER_UP || masked == MotionEvent.ACTION_UP)
        ) {
            threeGestureArmed = false
            val dt = System.currentTimeMillis() - threeDownTimeMs
            if (dt in 0L..380L) {
                threeTriggered = true
                return true
            }
        }
        if (masked == MotionEvent.ACTION_CANCEL) {
            threeGestureArmed = false
        }
        return false
    }

    fun processMotionEvent(
        event: MotionEvent,
        sensitivity: Float,
        emit: (MouseEventData) -> Unit
    ) {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                gestureStartTimeMs = System.currentTimeMillis()
                maxPointers = 1
                totalDistancePx = 0f
                lastPrimaryX = event.x
                lastPrimaryY = event.y
                remX = 0f
                remY = 0f
                remScroll = 0f
            }

            MotionEvent.ACTION_POINTER_DOWN -> {
                if (event.pointerCount > maxPointers) {
                    maxPointers = event.pointerCount
                }
                lastPrimaryX = centroidX(event)
                lastPrimaryY = centroidY(event)
            }

            MotionEvent.ACTION_MOVE -> {
                val cx = if (event.pointerCount >= 2) centroidX(event) else event.x
                val cy = if (event.pointerCount >= 2) centroidY(event) else event.y
                val dx = cx - lastPrimaryX
                val dy = cy - lastPrimaryY
                lastPrimaryX = cx
                lastPrimaryY = cy

                totalDistancePx += hypot(dx, dy)

                if (event.pointerCount == 1 && maxPointers == 1) {
                    remX += dx * sensitivity
                    remY += dy * sensitivity
                    val ix = remX.roundToInt()
                    val iy = remY.roundToInt()
                    if (ix != 0 || iy != 0) {
                        remX -= ix
                        remY -= iy
                        emit(
                            MouseEventData(
                                flags = AdspConstants.MOUSE_MOVE or AdspConstants.MOUSE_RELATIVE,
                                xValue = ix,
                                yValue = iy
                            )
                        )
                    }
                } else if (event.pointerCount == 2) {
                    remScroll += dy * 2.5f
                    val ticks = remScroll.roundToInt()
                    if (ticks != 0) {
                        remScroll -= ticks
                        emit(
                            MouseEventData(
                                flags = AdspConstants.MOUSE_WHEEL or AdspConstants.MOUSE_RELATIVE,
                                xValue = 0,
                                yValue = 0,
                                wheelDelta = ticks.coerceIn(-32000, 32000).toShort()
                            )
                        )
                    }
                }
            }

            MotionEvent.ACTION_UP -> {
                val duration = System.currentTimeMillis() - gestureStartTimeMs
                if (totalDistancePx < touchSlopPx * 1.5f && duration < 280L) {
                    if (maxPointers == 1) {
                        emit(MouseEventData(AdspConstants.MOUSE_LEFT_DOWN or AdspConstants.MOUSE_RELATIVE, 0, 0))
                        emit(MouseEventData(AdspConstants.MOUSE_LEFT_UP or AdspConstants.MOUSE_RELATIVE, 0, 0))
                    } else if (maxPointers == 2) {
                        emit(MouseEventData(AdspConstants.MOUSE_RIGHT_DOWN or AdspConstants.MOUSE_RELATIVE, 0, 0))
                        emit(MouseEventData(AdspConstants.MOUSE_RIGHT_UP or AdspConstants.MOUSE_RELATIVE, 0, 0))
                    }
                }
                maxPointers = 0
            }
        }
    }

    private fun centroidX(event: MotionEvent): Float {
        var sum = 0f
        for (i in 0 until event.pointerCount) sum += event.getX(i)
        return sum / event.pointerCount.coerceAtLeast(1)
    }

    private fun centroidY(event: MotionEvent): Float {
        var sum = 0f
        for (i in 0 until event.pointerCount) sum += event.getY(i)
        return sum / event.pointerCount.coerceAtLeast(1)
    }
}
