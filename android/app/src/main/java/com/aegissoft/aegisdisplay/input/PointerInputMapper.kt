package com.aegissoft.aegisdisplay.input

import android.view.MotionEvent
import com.aegissoft.aegisdisplay.protocol.AdspConstants
import com.aegissoft.aegisdisplay.protocol.PenEventData
import com.aegissoft.aegisdisplay.protocol.TouchContactData
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

enum class ScaleMode(val wireId: Int) {
    FIT(0),
    FILL(1),
    NATIVE_ONE_TO_ONE(2)
}

data class MappedPoint(val xNorm: Int, val yNorm: Int)

object PointerInputMapper {

    fun mapViewToNormalized(
        viewX: Float,
        viewY: Float,
        viewWidth: Int,
        viewHeight: Int,
        streamWidth: Int,
        streamHeight: Int,
        scaleMode: ScaleMode
    ): MappedPoint {
        if (viewWidth <= 0 || viewHeight <= 0 || streamWidth <= 0 || streamHeight <= 0) {
            return MappedPoint(0, 0)
        }

        val vw = viewWidth.toFloat()
        val vh = viewHeight.toFloat()
        val sw = streamWidth.toFloat()
        val sh = streamHeight.toFloat()

        val scale = when (scaleMode) {
            ScaleMode.FIT -> minOf(vw / sw, vh / sh)
            ScaleMode.FILL -> maxOf(vw / sw, vh / sh)
            ScaleMode.NATIVE_ONE_TO_ONE -> 1.0f
        }

        val contentW = sw * scale
        val contentH = sh * scale
        val offsetX = (vw - contentW) * 0.5f
        val offsetY = (vh - contentH) * 0.5f

        val u = ((viewX - offsetX) / contentW).coerceIn(0f, 1f)
        val v = ((viewY - offsetY) / contentH).coerceIn(0f, 1f)

        return MappedPoint(
            xNorm = (u * 65535f).roundToInt().coerceIn(0, 65535),
            yNorm = (v * 65535f).roundToInt().coerceIn(0, 65535)
        )
    }

    fun extractTouchContacts(
        event: MotionEvent,
        viewWidth: Int,
        viewHeight: Int,
        streamWidth: Int,
        streamHeight: Int,
        scaleMode: ScaleMode
    ): List<TouchContactData> {
        val masked = event.actionMasked
        val actionIndex = event.actionIndex
        val count = minOf(event.pointerCount, 10)
        val out = ArrayList<TouchContactData>(count)

        for (i in 0 until count) {
            val tool = event.getToolType(i)
            if (tool == MotionEvent.TOOL_TYPE_STYLUS || tool == MotionEvent.TOOL_TYPE_ERASER) {
                continue
            }

            val mappedAction = when (masked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN ->
                    if (i == actionIndex) AdspConstants.TOUCH_DOWN else AdspConstants.TOUCH_MOVE
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP ->
                    if (i == actionIndex) AdspConstants.TOUCH_UP else AdspConstants.TOUCH_MOVE
                MotionEvent.ACTION_CANCEL -> AdspConstants.TOUCH_CANCEL
                MotionEvent.ACTION_MOVE -> AdspConstants.TOUCH_MOVE
                else -> continue
            }

            val pt = mapViewToNormalized(
                event.getX(i), event.getY(i),
                viewWidth, viewHeight, streamWidth, streamHeight, scaleMode
            )
            val pressureNorm = (event.getPressure(i).coerceIn(0f, 1f) * 65535f).roundToInt()
            val sizeNorm = (event.getSize(i).coerceIn(0f, 1f) * 65535f).roundToInt()

            out.add(
                TouchContactData(
                    pointerId = event.getPointerId(i),
                    action = mappedAction,
                    xNorm = pt.xNorm,
                    yNorm = pt.yNorm,
                    pressure = pressureNorm,
                    contactSize = sizeNorm
                )
            )
        }
        return out
    }

    fun extractPenEvent(
        event: MotionEvent,
        viewWidth: Int,
        viewHeight: Int,
        streamWidth: Int,
        streamHeight: Int,
        scaleMode: ScaleMode
    ): PenEventData? {
        if (event.pointerCount == 0) return null
        val tool = event.getToolType(0)
        if (tool != MotionEvent.TOOL_TYPE_STYLUS && tool != MotionEvent.TOOL_TYPE_ERASER) {
            return null
        }

        val action = when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> AdspConstants.PEN_DOWN
            MotionEvent.ACTION_MOVE -> AdspConstants.PEN_MOVE
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> AdspConstants.PEN_UP
            MotionEvent.ACTION_HOVER_MOVE, MotionEvent.ACTION_HOVER_ENTER, MotionEvent.ACTION_HOVER_EXIT ->
                AdspConstants.PEN_HOVER
            else -> return null
        }

        var flags = AdspConstants.PEN_FLAG_IN_RANGE
        if (action == AdspConstants.PEN_DOWN || action == AdspConstants.PEN_MOVE) {
            flags = flags or AdspConstants.PEN_FLAG_IN_CONTACT
        }
        if ((event.buttonState and MotionEvent.BUTTON_STYLUS_PRIMARY) != 0) {
            flags = flags or AdspConstants.PEN_FLAG_BARREL
        }
        if (tool == MotionEvent.TOOL_TYPE_ERASER) {
            flags = flags or AdspConstants.PEN_FLAG_ERASER
        }

        val pt = mapViewToNormalized(
            event.x, event.y,
            viewWidth, viewHeight, streamWidth, streamHeight, scaleMode
        )

        val tiltRad = event.getAxisValue(MotionEvent.AXIS_TILT).coerceIn(0f, (PI / 2).toFloat())
        val orientRad = event.getAxisValue(MotionEvent.AXIS_ORIENTATION)
        val tiltXDeg = ((-sin(orientRad) * tiltRad) * (180f / PI.toFloat())).roundToInt().coerceIn(-90, 90).toByte()
        val tiltYDeg = ((cos(orientRad) * tiltRad) * (180f / PI.toFloat())).roundToInt().coerceIn(-90, 90).toByte()
        val pressureNorm = (event.pressure.coerceIn(0f, 1f) * 65535f).roundToInt()
        val rotDeg = (((orientRad * (180f / PI.toFloat())).roundToInt() % 360) + 360) % 360

        return PenEventData(
            action = action,
            flags = flags,
            xNorm = pt.xNorm,
            yNorm = pt.yNorm,
            pressure = pressureNorm,
            rotationDeg = rotDeg,
            tiltXDeg = tiltXDeg,
            tiltYDeg = tiltYDeg
        )
    }
}
